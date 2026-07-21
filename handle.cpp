#include <string_view>
#include <vector>
#include <optional>
#include <string>
#include <charconv>
#include <iostream>
#include <mutex>
#include <format>
#include <spdlog/spdlog.h>
#include "Socket.h"
#include "Router.h"
#include "handle.h"

ParseResponseType parse_commands(std::string_view buf, ClientState& state) {
    if (buf.empty()) return ParseResponseType::INCOMPLETE;
    
    if (state.strings_remaining == -1) { // no progress yet
        if (buf[0] == '*') {
            size_t count_end = buf.find("\r\n");
            if (count_end == std::string::npos) return ParseResponseType::INCOMPLETE;
            auto count_sv = buf.substr(1, count_end - 1);
            auto result = std::from_chars(count_sv.data(), count_sv.data() + count_end - 1, state.strings_remaining);
            if (result.ec == std::errc::invalid_argument) return ParseResponseType::ERROR;
            state.start_idx = count_end + 2;
        } else {
            spdlog::error("Failed to parse buffer: not a bulk string");
            return ParseResponseType::ERROR;
        }
    }

    const int remaining { state.strings_remaining }; // so we can decrement state's counter w/o interfering in loop
    for (int i = 0; i < remaining; ++i) {
        if (buf.size() <= state.start_idx) return ParseResponseType::INCOMPLETE; // out of bounds
        
        // find delimit; if len doesn't match, throw; if no delim incomp
        if (buf[state.start_idx] != '$') return ParseResponseType::ERROR;
        size_t size_end = buf.find("\r\n", state.start_idx);
        if (size_end == std::string::npos) return ParseResponseType::INCOMPLETE;

        int body_size;
        auto size_sv = buf.substr(state.start_idx + 1, size_end - (state.start_idx + 1));
        auto result = std::from_chars(size_sv.data(), size_sv.data() + size_end - (state.start_idx + 1), body_size);
        if (result.ec == std::errc::invalid_argument) return ParseResponseType::ERROR;

        // parse body
        size_t body_start { size_end + 2 };
        size_t body_end = buf.find("\r\n", body_start);
        if (body_end == std::string::npos) return ParseResponseType::INCOMPLETE;
        if (body_end - body_start != body_size) return ParseResponseType::ERROR; // body sz doesn't match

        if (state.command_name.empty()) {
            state.command_name = buf.substr(body_start, body_end - body_start);
        } else {
            state.command_args.push_back(std::string(buf.substr(body_start, body_end - body_start))); // examine instructions for this; see if constructing in vector?
        }

        --state.strings_remaining;
        state.start_idx = body_end + 2;
    }

    return ParseResponseType::COMPLETE;
}


ParseSendResult parse_and_send(Connection& connection, Router& router, Storage& storage) {
    ParseSendResult result {0, 0, 0, ParseSendError::OK};
    ParseResponseType res = parse_commands(connection.buf, connection.state);
    while (res == ParseResponseType::COMPLETE) {
        result.bytes_read += connection.state.start_idx;
        ++result.commands;
        auto [response, action] = router.dispatch(connection.state.command_name, connection.state.command_args);
        size_t total = 0;
        while (total < response.size()) {
            auto sent = send(connection.sock.fd(), response.data() + total, response.size() - total, MSG_DONTWAIT); // todo: make MSG_DONTWAIT and epoll for eagain
            if (sent == -1) {
                spdlog::error("Send failure");
                result.error = ParseSendError::SEND;
                return result;
            }
            total += sent;
            result.bytes_sent += sent;
        }
        if (action == HandlerAction::SENDTOREPLICAS) storage.send_to_replicas({connection.buf.data(), connection.state.start_idx});
        if (action == HandlerAction::INITREPLICA) result.actions.insert(action);
        connection.buf.erase(0, connection.state.start_idx); // start_idx should be size of all consumed bytes (start of next command if there)
        connection.state = ClientState{};
        res = parse_commands(connection.buf, connection.state);
    }
    if (res == ParseResponseType::ERROR) {
        spdlog::error("Parsing failure");
        result.error = ParseSendError::PARSE;
    }
    return result;
}

std::string serialize_to_bulk_string(std::string_view sv) {
    return std::format("${}\r\n{}\r\n", sv.size(), sv);
}

std::string serialize_to_string_array(const std::vector<std::string>& strings) {
    std::string out;
    out += std::format("*{}\r\n", strings.size());
    for (auto& s : strings) {
        out += std::format("${}\r\n{}\r\n", s.size(), s);
    }
    return out;
}

template <typename T>
std::string serialize_to_integer_reply(T num) { // concept in future to ensure integral?
    return std::format(":{}\r\n", num);
}
/*
    Key value string handlers
*/

std::pair<std::string, HandlerAction> handle_set(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 2) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    storage.set(args[0], args[1]);
    return {"+OK\r\n", HandlerAction::SENDTOREPLICAS};
}

std::pair<std::string, HandlerAction> handle_get(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE}; // handling 1 key for now

    const StorageResult res = storage.get(args[0]);
    if (!res.ok()) {
        if (res.error == StorageError::NotFound) return {"$-1\r\n", HandlerAction::NONE};
        return {ERROR_WRONG_TYPE, HandlerAction::NONE};
    }
    return {serialize_to_bulk_string(res.value), HandlerAction::NONE};
}

/*
    List handlers
*/

std::pair<std::string, HandlerAction> handle_lpush(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 2) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    const StorageResult<size_t> res = storage.deque_push(args[0], args[1], true);
    if (res.error == StorageError::WrongType) return {ERROR_WRONG_TYPE, HandlerAction::NONE};
    return {serialize_to_integer_reply(res.value), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_rpush(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 2) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    const StorageResult<size_t> res = storage.deque_push(args[0], args[1], false);
    if (res.error == StorageError::WrongType) return {ERROR_WRONG_TYPE, HandlerAction::NONE};
    return {serialize_to_integer_reply(res.value), HandlerAction::SENDTOREPLICAS};
}

std::pair<std::string, HandlerAction> handle_lpop(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    const StorageResult<std::string> res = storage.deque_pop(args[0], true);
    if (res.error == StorageError::WrongType) return {ERROR_WRONG_TYPE, HandlerAction::NONE};
    if (res.error == StorageError::NotFound) return {"$-1\r\n", HandlerAction::NONE};
    return {serialize_to_bulk_string(res.value), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_rpop(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    const StorageResult<std::string> res = storage.deque_pop(args[0], false);
    if (res.error == StorageError::WrongType) return {ERROR_WRONG_TYPE, HandlerAction::NONE};
    if (res.error == StorageError::NotFound) return {"$-1\r\n", HandlerAction::NONE};
    return {serialize_to_bulk_string(res.value), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_llen(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    const StorageResult<size_t> res = storage.deque_len(args[0]);
    if (res.error == StorageError::WrongType) return {ERROR_WRONG_TYPE, HandlerAction::NONE};
    return {serialize_to_integer_reply(res.value), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_lrange(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 3) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    ssize_t lIndex;
    auto lresult = std::from_chars(args[1].data(), args[1].data() + args[1].size(), lIndex);
    if (lresult.ec == std::errc::invalid_argument) return {"-ERR value is not an integer or out of range\r\n", HandlerAction::NONE};
    ssize_t rIndex;
    auto rresult = std::from_chars(args[2].data(), args[2].data() + args[2].size(), rIndex);
    if (rresult.ec == std::errc::invalid_argument) return {"-ERR value is not an integer or out of range\r\n", HandlerAction::NONE};

    const StorageResult<std::vector<std::string>> res = storage.deque_range(args[0], lIndex, rIndex);
    if (res.error == StorageError::WrongType) return {ERROR_WRONG_TYPE, HandlerAction::NONE};
    return {serialize_to_string_array(res.value), HandlerAction::NONE};
}

/*
    Misc handlers
*/

std::pair<std::string, HandlerAction> handle_exists(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    return {storage.exists(args[0]) ? ":1\r\n" : ":0\r\n", HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_del(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    const auto n = storage.del(args[0]);
    return {serialize_to_integer_reply(n), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_expire(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 2) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    int seconds;
    auto parseResult = std::from_chars(args[1].data(), args[1].data() + args[1].size(), seconds);
    if (parseResult.ec == std::errc::invalid_argument || seconds <= 0) return {"-ERR value is not an integer or out of range\r\n", HandlerAction::NONE};

    const size_t n = storage.expire(args[0], seconds);
    return {serialize_to_integer_reply(n), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_persist(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    return {serialize_to_integer_reply(storage.persist(args[0])), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_ttl(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 1) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    return {serialize_to_integer_reply(storage.ttl(args[0])), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_unknown() {
    return {"-Error unknown command\r\n", HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_ping(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() > 1) {
        return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};
    } else if (args.size() == 1) {
        return {serialize_to_bulk_string(args[0]), HandlerAction::NONE};
    }
    return {"+PONG\r\n", HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_info(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() > 0) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    return {serialize_to_bulk_string(storage.info()), HandlerAction::NONE};
}

std::pair<std::string, HandlerAction> handle_psync(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 2) return {ERROR_INVALID_ARG_COUNT, HandlerAction::NONE};

    return {"+FULLRESYNC 0 0\r\n", HandlerAction::INITREPLICA};
}