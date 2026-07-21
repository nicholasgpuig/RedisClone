#pragma once
#include <string>
#include <optional>
#include "Socket.h"

Socket initialize_replica(char* host, uint32_t port);
std::optional<std::string> master_handshake(int master_fd);
void replica_worker_loop(Socket master_sock, Router& router);
std::pair<std::string, HandlerAction> replica_handle_set(const std::vector<std::string>& args, Storage& storage);
std::pair<std::string, HandlerAction> replica_handle_rpush(const std::vector<std::string>& args, Storage& storage);