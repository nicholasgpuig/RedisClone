#pragma once
#include <vector>
#include <string_view>
#include <unordered_set>
#include "Socket.h"
#include "Storage.h"

class Router;

inline const std::string ERROR_INVALID_ARG_COUNT{"-Error Invalid argument count\r\n"};
inline const std::string ERROR_WRONG_TYPE{"-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"};

enum class ParseResponseType {
    COMPLETE,
    INCOMPLETE,
    ERROR,
};

enum class ParseSendError {
    PARSE,
    SEND,
    OK
};

enum class HandlerAction {
    INITREPLICA,
    SENDTOREPLICAS,
    NONE
};

struct ParseSendResult {
    uint64_t bytes_read;
    uint64_t bytes_sent;
    uint64_t commands;
    ParseSendError error;
    std::unordered_set<HandlerAction> actions;
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
ParseSendResult parse_and_send(Connection& connection, Router& router, Storage& storage);
std::pair<std::string, HandlerAction> handle_ping(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_set(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_get(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_exists(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_del(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_lpush(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_rpush(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_lpop(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_rpop(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_llen(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_lrange(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_expire(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_persist(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_ttl(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_unknown();
std::pair<std::string, HandlerAction> handle_info(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> handle_psync(const std::vector<std::string>& args, Storage& storage);