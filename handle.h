#pragma once
#include <vector>
#include <string_view>
#include "Socket.h"
#include "Router.h"
#include "Storage.h"

inline const std::string ERROR_INVALID_ARG_COUNT{"-Error Invalid argument count\r\n"};

enum class ParseResponseType {
    COMPLETE,
    INCOMPLETE,
    ERROR,
};

struct ClientState {
    int start_idx {0};
    int strings_remaining {-1};
    std::string command_name;
    std::vector<std::string> command_args;
};

ParseResponseType parse_commands(std::string_view buf, ClientState& state);
void handle_client(Socket&& s, Router& router);
std::string handle_ping(const std::vector<std::string>& args, Storage& storage);
std::string handle_set(const std::vector<std::string>& args, Storage& storage);
std::string handle_get(const std::vector<std::string>& args, Storage& storage);
std::string handle_exists(const std::vector<std::string>& args, Storage& storage);
std::string handle_del(const std::vector<std::string>& args, Storage& storage);
std::string handle_unknown();