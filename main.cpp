#include <unordered_map>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <functional>
#include "Socket.h"
#include "Router.h"
#include "Storage.h"
#include "handle.h"

constexpr int LISTEN_PORT { 6379 };
constexpr int MAX_EVENTS { 4096 };

void worker_loop(const EpollFd& epoll_fd, const ServerSocket& server, Storage& storage, Router& router);

int main(){
    Storage storage;
    Router router(storage);
    router.add("PING", handle_ping);
    router.add("GET", handle_get);
    router.add("SET", handle_set);
    router.add("EXISTS", handle_exists);
    router.add("DEL", handle_del);
    router.add("LPUSH", handle_lpush);
    router.add("RPUSH", handle_rpush);
    router.add("LPOP", handle_lpop);
    router.add("RPOP", handle_rpop);
    router.add("LLEN", handle_llen);
    router.add("LRANGE", handle_lrange);
    router.add("EXPIRE", handle_expire);
    router.add("PERSIST", handle_persist);
    router.add("TTL", handle_ttl);
    router.add("INFO", handle_info);

    const ServerSocket server(LISTEN_PORT);
    if (!server) {
        spdlog::error("Server socket failed to start");
        return 1;
    }

    const EpollFd epoll_fd;
    if (!epoll_fd) {
        spdlog::error("Epoll fd failed to start");
        return 1;
    }
    epoll_fd.register_fd(server.fd());

    const unsigned int N = 8; // std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i + 1 < N; ++i) {
        threads.emplace_back(worker_loop, std::ref(epoll_fd), std::ref(server), std::ref(storage), std::ref(router));
        threads[i].detach();
    }
    spdlog::info("Started listening on {} threads", N);
    worker_loop(epoll_fd, server, storage, router);

    return 0;
}


void worker_loop(const EpollFd& epoll_fd, const ServerSocket& server, Storage& storage, Router& router) {
    epoll_event events[MAX_EVENTS];
    while (true) {
        int n = epoll_wait(epoll_fd.fd(), events, MAX_EVENTS, -1);
        if (n == -1 && errno != EINTR) {
            spdlog::error("epoll_wait error: {}", errno);
            return;
        }
        for (int i = 0; i < n; ++i) {
            epoll_event& ev = events[i];
            if (ev.data.ptr == nullptr) {
                while (true) {
                    Socket sock = server.accept();
                    if (!sock && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                    if (!sock) {
                        spdlog::error("Server accept failed");
                        continue;
                    }
                    int fd = sock.fd();
                    Connection* conn_ptr { new(std::nothrow) Connection{std::move(sock)} };
                    if (conn_ptr == nullptr) {
                        spdlog::error("new Connection allocation failed");
                        continue;
                    }
                    epoll_fd.register_connection(conn_ptr);
                    //spdlog::info("Client accepted: {}", fd);
                    storage.stats.clients_connected.fetch_add(1, std::memory_order_relaxed);
                }
                if (epoll_fd.rearm_fd(server.fd()) == -1) {
                    spdlog::error("Server rearm failed!");
                }
            } else {
                bool unrecoverable {false};
                Connection* connection = static_cast<Connection*>(ev.data.ptr);
                while (true) {
                    char chunk[4096];
                    auto n = recv(connection->sock.fd(), chunk, sizeof(chunk), MSG_DONTWAIT);
                    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                    if (n <= 0) {
                        unrecoverable = true;
                        epoll_fd.unregister_connection(connection);
                        storage.stats.clients_connected.fetch_add(-1, std::memory_order_relaxed);
                        break;
                    }
                    connection->buf.append(chunk, n); // todo: write directly to buf
                }
                if (unrecoverable) continue;
                auto result = parse_and_send(*connection, router);
                storage.stats.bytes_read.fetch_add(result.bytes_read, std::memory_order_relaxed);
                storage.stats.bytes_written.fetch_add(result.bytes_sent, std::memory_order_relaxed);
                storage.stats.total_commands.fetch_add(result.commands, std::memory_order_relaxed);
                if (result.error != ParseSendError::OK || epoll_fd.rearm_connection(connection) == -1) {
                    spdlog::info("parse or rearm failure; unreg client {}", connection->sock.fd());
                    epoll_fd.unregister_connection(connection);
                    storage.stats.clients_connected.fetch_add(-1, std::memory_order_relaxed);
                }
            }
        }
    }
}