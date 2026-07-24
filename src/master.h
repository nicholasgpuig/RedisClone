#pragma once
#include "Router.h"
#include "Socket.h"
#include "Storage.h"

void master_worker_loop(const EpollFd& epoll_fd, const ServerSocket& server, Storage& storage, Router& router);
void replica_fullsync(Socket replica_socket, uint32_t replica_id);