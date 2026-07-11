#include <unordered_map>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <spdlog/spdlog.h>
#include "Socket.h"
#include "Router.h"
#include "Storage.h"
#include "handle.h"

constexpr int LISTEN_PORT { 6379 };
constexpr int MAX_EVENTS { 4096 };

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

    ServerSocket server(LISTEN_PORT);
    if (!server) {
        spdlog::error("Server socket failed to start");
        return 1;
    }

    EpollFd epollFd;
    if (!epollFd) {
        spdlog::error("Epoll fd failed to start");
        return 1;
    }
    epollFd.register_fd(server.fd());
    epoll_event events[MAX_EVENTS];

    spdlog::info("Started listening");
    while (true) {
        int n = epoll_wait(epollFd.fd(), events, MAX_EVENTS, -1);
        if (n == -1 && errno != EINTR) {
            spdlog::error("epoll_wait error: {}", errno);
            return 1;
        }
        for (int i = 0; i < n; ++i) {
            epoll_event& ev = events[i];
            if (ev.data.ptr == nullptr) {
                Socket sock = server.accept();
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
                epollFd.register_connection(conn_ptr);
                spdlog::info("Client accepted: {}", fd);
                storage.stats.clients_connected.fetch_add(1, std::memory_order_relaxed);
            } else {
                Connection* connection = static_cast<Connection*>(ev.data.ptr);
                char chunk[4096];
                auto n = recv(connection->sock.fd(), chunk, sizeof(chunk), 0);
                if (n == -1 && errno == EAGAIN) continue;
                if (n <= 0) {
                    epollFd.unregister_connection(connection);
                    storage.stats.clients_connected.fetch_add(-1, std::memory_order_relaxed);
                    continue;
                }
                connection->buf.append(chunk, n);
                auto result = parse_and_send(*connection, router);
                storage.stats.bytes_read.fetch_add(result.bytes_read, std::memory_order_relaxed);
                storage.stats.bytes_written.fetch_add(result.bytes_sent, std::memory_order_relaxed);
                storage.stats.total_commands.fetch_add(result.commands, std::memory_order_relaxed);
                if (result.error != ParseSendError::OK) {
                    epollFd.unregister_connection(connection);
                    storage.stats.clients_connected.fetch_add(-1, std::memory_order_relaxed);
                }
            }
        }
    }


    return 0;
}