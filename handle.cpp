// parse std string as vector of commands
// command structs w/ name and args

#include <string_view>
#include <vector>
#include <optional>
#include <string>
#include <charconv>
#include <iostream>
#include "Socket.h"
#include "Router.h"

struct Command {
    std::string_view name;
    std::vector<std::string_view> args;
};

enum class ParseResponseType {
    COMPLETE,
    INCOMPLETE,
    ERROR,
};

struct CommandArrayState {
    std::vector<Command> commands;
    int start_idx {0};
    int commands_remaining {-1};
    int end_idx {0};
    // parsed commands, remaining command ct, start idx, end to know bytes consumed
    // won't include buffer due to pipelining; should be filled by parse_commands until parsing complete (ie got all n remaining)
};

// eventually return parse response object w/ optional error (to indicate non retryable); true/false represents is ready
ParseResponseType parse_commands(std::string_view buf, CommandArrayState& state) {
// check if asterisk; parse n then get start else keep default
    if (state.commands_remaining == -1) { // no progress yet
        if (buf[0] == '*') {
            int count_end = buf.find("\r\n");
            if (count_end == std::string::npos) return ParseResponseType::INCOMPLETE;
            auto count_sv = buf.substr(1, count_end - 1);
            auto result = std::from_chars(count_sv.data(), count_sv.data() + count_end - 1, state.commands_remaining);
            if (result.ec == std::errc::invalid_argument) return ParseResponseType::ERROR;
            state.start_idx = count_end + 2;
        } else {
            state.commands_remaining = 1;
        }
    }

    int remaining { state.commands_remaining }; // so we can decrement state's counter w/o interfering in loop
    for (int i = 0; i < remaining; ++i) {
        if (buf.size() <= state.start_idx) return ParseResponseType::INCOMPLETE; // out of bounds
        // find delimit; if len doesn't match, throw; if no delim incomp
        if (buf[state.start_idx] != '$') return ParseResponseType::ERROR;
        int size_end = buf.find("\r\n", state.start_idx);
        if (size_end == std::string::npos) return ParseResponseType::INCOMPLETE;

        int command_size;
        auto size_sv = buf.substr(state.start_idx + 1, size_end - (state.start_idx + 1));
        auto result = std::from_chars(size_sv.data(), size_sv.data() + size_end - (state.start_idx + 1), command_size);
        if (result.ec == std::errc::invalid_argument) return ParseResponseType::ERROR;

        // parse body
        int body_start { size_end + 2 };
        int body_end = buf.find("\r\n", body_start);
        if (body_end == std::string::npos) return ParseResponseType::INCOMPLETE;
        if (body_end - body_start != command_size) return ParseResponseType::ERROR; // body sz doesn't match
        if (command_size == 0) continue; // skip empty command

        Command command;
        int start = body_start, end = buf.find(' ', start);
        while (end != std::string::npos) {
            if (command.name.empty()) {
                command.name = buf.substr(start, end - start);
            } else {
                command.args.push_back(buf.substr(start, end - start));
            }
            start = end + 1;
            end = buf.find(' ', start);
        }

        state.commands.push_back(std::move(command));
        --state.commands_remaining;
        state.start_idx = body_end + 2;
    }

    state.end_idx = state.start_idx; // should be sz of all consumed bytes

    return ParseResponseType::COMPLETE;
}


void handle_client(Socket&& s, Router& router) {
    std::string buf;
    CommandArrayState state;

    while (true) {
        char chunk[4096];
        ssize_t n = recv(s.fd(), chunk, sizeof(chunk), MSG_DONTWAIT);
        if (n <= 0) return;
        buf.append(chunk, n);

        ParseResponseType res = parse_commands(buf, state);
        if (res == ParseResponseType::ERROR) return;
        if (res == ParseResponseType::COMPLETE) {
            for (auto& command : state.commands) {
                router.dispatch(command.name, command.args);
            }
            buf.erase(0, state.end_idx);
            state = CommandArrayState{};
        }
    }
}