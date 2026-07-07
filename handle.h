#pragma once
#include <vector>
#include <string_view>
#include "Socket.h"
#include "Router.h"
#include "Storage.h"

inline const std::string ERROR_INVALID_ARG_COUNT{"-Error Invalid argument count\r\n"};
inline const std::string ERROR_WRONG_TYPE{"-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"};

enum class ParseResponseType {
    COMPLETE,
    INCOMPLETE,
    ERROR,
};

struct ClientState {
    size_t start_idx {0};
    int strings_remaining {-1};
    std::string command_name;
    std::vector<std::string> command_args;
};

struct Connection {
    Socket sock;
    std::string buf;
    ClientState state;
};

ParseResponseType parse_commands(std::string_view buf, ClientState& state);
int parse_and_send(Connection& connection, Router& router);
std::string handle_ping(const std::vector<std::string>& args, Storage& storage);
std::string handle_set(const std::vector<std::string>& args, Storage& storage);
std::string handle_get(const std::vector<std::string>& args, Storage& storage);
std::string handle_exists(const std::vector<std::string>& args, Storage& storage);
std::string handle_del(const std::vector<std::string>& args, Storage& storage);
std::string handle_lpush(const std::vector<std::string>& args, Storage& storage);
std::string handle_rpush(const std::vector<std::string>& args, Storage& storage);
std::string handle_lpop(const std::vector<std::string>& args, Storage& storage);
std::string handle_rpop(const std::vector<std::string>& args, Storage& storage);
std::string handle_llen(const std::vector<std::string>& args, Storage& storage);
std::string handle_lrange(const std::vector<std::string>& args, Storage& storage);
std::string handle_unknown();