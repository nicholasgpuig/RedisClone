// parse std string as vector of commands
// command structs w/ name and args

#include <string_view>
#include <vector>
#include <optional>
#include <string>
#include <charconv>
#include <iostream>
#include <mutex>
#include "Socket.h"
#include "Router.h"
#include "handle.h"

// eventually return parse response object w/ optional error (to indicate non retryable); true/false represents is ready
ParseResponseType parse_commands(std::string_view buf, ClientState& state) {
    if (buf.empty()) return ParseResponseType::INCOMPLETE;
    
    if (state.strings_remaining == -1) { // no progress yet
        if (buf[0] == '*') {
            auto count_end = buf.find("\r\n");
            if (count_end == std::string::npos) return ParseResponseType::INCOMPLETE;
            auto count_sv = buf.substr(1, count_end - 1);
            auto result = std::from_chars(count_sv.data(), count_sv.data() + count_end - 1, state.strings_remaining);
            if (result.ec == std::errc::invalid_argument) return ParseResponseType::ERROR;
            state.start_idx = count_end + 2;
        } else {
            return ParseResponseType::ERROR; // not a bulk string array
        }
    }

    const int remaining { state.strings_remaining }; // so we can decrement state's counter w/o interfering in loop
    for (int i = 0; i < remaining; ++i) {
        if (buf.size() <= state.start_idx) return ParseResponseType::INCOMPLETE; // out of bounds
        
        // find delimit; if len doesn't match, throw; if no delim incomp
        if (buf[state.start_idx] != '$') return ParseResponseType::ERROR;
        auto size_end = buf.find("\r\n", state.start_idx);
        if (size_end == std::string::npos) return ParseResponseType::INCOMPLETE;

        int body_size;
        auto size_sv = buf.substr(state.start_idx + 1, size_end - (state.start_idx + 1));
        auto result = std::from_chars(size_sv.data(), size_sv.data() + size_end - (state.start_idx + 1), body_size);
        if (result.ec == std::errc::invalid_argument) return ParseResponseType::ERROR;

        // parse body
        size_t body_start { size_end + 2 };
        auto body_end = buf.find("\r\n", body_start);
        if (body_end == std::string::npos) return ParseResponseType::INCOMPLETE;
        if (body_end - body_start != body_size) return ParseResponseType::ERROR; // body sz doesn't match

        if (state.command_name.empty()) {
            state.command_name = buf.substr(body_start, body_end - body_start);
        } else {
            state.command_args.push_back(std::string(buf.substr(body_start, body_end - body_start)));
        }

        --state.strings_remaining;
        state.start_idx = body_end + 2;
    }

    return ParseResponseType::COMPLETE;
}


void handle_client(Socket&& s, Router& router) {
    std::string buf;
    ClientState state;

    while (true) {
        char chunk[4096];
        ssize_t n = recv(s.fd(), chunk, sizeof(chunk), 0);
        if (n <= 0) return;
        buf.append(chunk, n);

        ParseResponseType res = parse_commands(buf, state);
        while (res == ParseResponseType::COMPLETE) {
            std::string response = router.dispatch(state.command_name, state.command_args);
            size_t total = 0;
            while (total < response.size()) {
                auto sent = send(s.fd(), response.data() + total, response.size() - total, 0); // assume sends all bytes for now
                if (sent == -1) return;
                total += sent;
            }
            buf.erase(0, state.start_idx); // start_idx should be size of all consumed bytes (start of next command if there)
            state = ClientState{state.start_idx};
            res = parse_commands(buf, state);
        }
        if (res == ParseResponseType::ERROR) {
            std::cout << "Parse Error\n";
            return;
        }
    }
}

std::string serialize_to_bulk_string(std::string_view sv) {
    return '$' + std::to_string(sv.size()) + "\r\n" + std::string(sv) + "\r\n";
}

std::string handle_ping(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() > 1) {
        return ERROR_INVALID_ARG_COUNT;
    } else if (args.size() == 1) {
        return serialize_to_bulk_string(args[0]);
    }
    return "+PONG\r\n";
}

std::string handle_set(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 2) return ERROR_INVALID_ARG_COUNT;

    std::lock_guard lock(storage.lock);
    storage.m_[args[0]] = args[1];
    return "+OK\r\n";
}

std::string handle_get(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return ERROR_INVALID_ARG_COUNT; // handling 1 key for now

    std::optional<std::string> value;
    {
        std::lock_guard lock(storage.lock);
        auto it = storage.m_.find(args[0]);
        if (it != storage.m_.end()) {
            value = it->second;
        }
    }
    if (value) {
        return serialize_to_bulk_string(*value);
    }
    return "$-1\r\n";
}

std::string handle_exists(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return ERROR_INVALID_ARG_COUNT;

    std::lock_guard lock(storage.lock);
    if (storage.m_.find(args[0]) != storage.m_.end())
        return ":1\r\n";
    return ":0\r\n";
}

std::string handle_del(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return ERROR_INVALID_ARG_COUNT;

    std::lock_guard lock(storage.lock);
    auto n = storage.m_.erase(args[0]);
    return n ? ":1\r\n" : ":0\r\n";
}

std::string handle_unknown() {
    return "-Error unknown command\r\n";
}