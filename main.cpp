#include <iostream>
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
    std::unordered_map<int, Connection> connMap; // replace w/ events.data.ptr
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
            if (ev.data.fd == server.fd()) {
                Socket sock = server.accept();
                if (!sock) {
                    spdlog::error("Server accept failed");
                    continue;
                }
                int fd = sock.fd();
                epollFd.register_fd(fd);
                connMap[fd] = Connection{std::move(sock)};
                spdlog::info("Client accepted: {}", fd);
            } else {
                auto connIt = connMap.find(ev.data.fd);
                if (connIt == connMap.end()) continue;
                char chunk[4096];
                auto n = recv(ev.data.fd, chunk, sizeof(chunk), 0);
                if (n == -1 && errno == EAGAIN) continue;
                Connection& connection = connIt->second;
                if (n <= 0) {
                    epollFd.unregister_fd(connection.sock.fd());
                    connMap.erase(connection.sock.fd());
                    continue;
                }
                connection.buf.append(chunk, n);
                if (parse_and_send(connection, router) == -1) {
                    epollFd.unregister_fd(connection.sock.fd());
                    connMap.erase(connection.sock.fd());
                }
            }
        }
    }


    return 0;
}