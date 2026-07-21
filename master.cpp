#include <sys/epoll.h>
#include <sys/socket.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/uio.h>
#include "Socket.h"
#include "Router.h"
#include "Storage.h"
#include "handle.h"
#include "replica.h"
#include "Arena.h"

constexpr int MAX_EVENTS { 4096 };

void replica_sync_loop(std::unordered_map<uint32_t, Replica>& replicas, std::mutex& replicas_lock);

size_t arena_write(Arena& arena, std::string_view sv, size_t offset) {
    size_t capacity = sizeof(arena.buf) - (size_t)(arena.cursor - arena.buf);
    if (capacity == 0) return offset;
    size_t bytes_to_write = std::min(capacity, sv.size() - offset);
    memcpy(arena.cursor, sv.data() + offset, bytes_to_write);
    arena.cursor += bytes_to_write;
    return offset + bytes_to_write; // new offset
}

int arena_send(int replica_fd, Arena& arena) {
    size_t to_send = arena.cursor - arena.buf;
    size_t total = 0;
    while (total < to_send) {
        auto sent = send(replica_fd, arena.buf + total, to_send - total, 0);
        if (sent == -1) {
            spdlog::error("Send to replica failed");
            return -1;
        }
        total += sent;
    }
    arena.cursor = arena.buf;
    return 0;
}

int write_and_send(std::string_view sv, Arena& arena, int replica_fd) {

    size_t offset = arena_write(arena, sv, 0);
    while (offset < sv.size()) { // didn't write full sv the first time
        if (arena_send(replica_fd, arena) == -1) return -1;
        offset = arena_write(arena, sv, offset);
    }
    return 0;
}

void replica_fullsync(Socket replica_socket, uint32_t replica_id, Storage& storage) {
    int flags = fcntl(replica_socket.fd(), F_GETFL, 0);
    fcntl(replica_socket.fd(), F_SETFL, flags & ~O_NONBLOCK);
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<std::pair<std::string, std::deque<std::string>>> lists;
    Arena arena;
    pairs.reserve(4096);
    lists.reserve(4096);
    char len_buf[20];
    for (int i = 0; i < storage.get_num_shards(); ++i) {
        storage.serialize_shard(i, replica_id, pairs, lists);

        // *3\r\n$3\r\nSET\r\n$keylen\r\nkey\r\n$vallen\r\nval\r\n
        for (auto& [key, value] : pairs) {
            if (write_and_send("*3\r\n$3\r\nSET\r\n$", arena, replica_socket.fd()) == -1) return;

            auto [ptr, ec] = std::to_chars(len_buf, len_buf + sizeof(len_buf), key.size());
            std::string_view len_sv(len_buf, ptr - len_buf);
            if (write_and_send(len_sv, arena, replica_socket.fd()) == -1) return;
            if (write_and_send("\r\n", arena, replica_socket.fd()) == -1) return;
            if (write_and_send(key, arena, replica_socket.fd()) == -1) return;
            if (write_and_send("\r\n$", arena, replica_socket.fd()) == -1) return;

            auto [p, e] = std::to_chars(len_buf, len_buf + sizeof(len_buf), value.size());
            std::string_view val_len_sv(len_buf, p - len_buf);
            if (write_and_send(val_len_sv, arena, replica_socket.fd()) == -1) return;
            if (write_and_send("\r\n", arena, replica_socket.fd()) == -1) return;
            if (write_and_send(value, arena, replica_socket.fd()) == -1) return;
            if (write_and_send("\r\n", arena, replica_socket.fd()) == -1) return;
        }
        if (arena.buf != arena.cursor) arena_send(replica_socket.fd(), arena);
        pairs.clear();

        for (auto& [key, value_deq] : lists) {
            for (std::string_view item : value_deq) {
                if (write_and_send("*3\r\n$5\r\nRPUSH\r\n$", arena, replica_socket.fd()) == -1) return;

                auto [ptr, ec] = std::to_chars(len_buf, len_buf + sizeof(len_buf), key.size());
                std::string_view len_sv(len_buf, ptr - len_buf);
                if (write_and_send(len_sv, arena, replica_socket.fd()) == -1) return;
                if (write_and_send("\r\n", arena, replica_socket.fd()) == -1) return;
                if (write_and_send(key, arena, replica_socket.fd()) == -1) return;
                if (write_and_send("\r\n$", arena, replica_socket.fd()) == -1) return;

                auto [p, e] = std::to_chars(len_buf, len_buf + sizeof(len_buf), item.size());
                std::string_view val_len_sv(len_buf, p - len_buf);
                if (write_and_send(val_len_sv, arena, replica_socket.fd()) == -1) return;
                if (write_and_send("\r\n", arena, replica_socket.fd()) == -1) return;
                if (write_and_send(item, arena, replica_socket.fd()) == -1) return;
                if (write_and_send("\r\n", arena, replica_socket.fd()) == -1) return;
            }
        }
        if (arena.buf != arena.cursor) arena_send(replica_socket.fd(), arena);
        lists.clear();
    }

    

    int replica_fd = replica_socket.fd();
    storage.add_replica(replica_id, std::move(replica_socket));
    for (int i = 0; i < storage.get_num_shards(); ++i) {
        if (auto* buffer_ptr = storage.get_shard_buffer(i, replica_id)) {
            std::vector<struct iovec> iov;
            iov.reserve((*buffer_ptr).size());
            for (const auto& cmd : *buffer_ptr) {
                iov.push_back({const_cast<char*>(cmd.data()), cmd.size()});
            }
            writev(replica_fd, iov.data(), iov.size());

            storage.delete_shard_buffer(i, replica_id);
        }
    }

    // background thread every 200ms to clear all buffers; use shared mutex on global map; single for each replica
    
}

void master_worker_loop(const EpollFd& epoll_fd, const ServerSocket& server, Storage& storage, Router& router) {
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
                auto result = parse_and_send(*connection, router, storage);
                storage.stats.bytes_read.fetch_add(result.bytes_read, std::memory_order_relaxed);
                storage.stats.bytes_written.fetch_add(result.bytes_sent, std::memory_order_relaxed);
                storage.stats.total_commands.fetch_add(result.commands, std::memory_order_relaxed);
                if (result.error != ParseSendError::OK || epoll_fd.rearm_connection(connection) == -1) {
                    spdlog::error("parse or rearm failure; unregistered client {}", connection->sock.fd());
                    epoll_fd.unregister_connection(connection);
                    storage.stats.clients_connected.fetch_add(-1, std::memory_order_relaxed);
                }

                for (auto action : result.actions) {
                    switch (action) {
                        case HandlerAction::INITREPLICA:
                            const uint32_t replica_id = storage.get_new_replica_id();
                            std::thread sync_thread(replica_fullsync, std::move(connection->sock), replica_id, std::ref(storage));
                            sync_thread.detach();
                            spdlog::info("Replicating to replica {}", replica_id);
                            static std::once_flag replication_thread_flag;
                            std::call_once(replication_thread_flag, [&storage]() {
                                std::thread(replica_sync_loop, std::ref(storage.get_replicas()), std::ref(storage.get_replicas_lock())).detach();
                            });

                            epoll_fd.unregister_connection(connection);
                            storage.stats.clients_connected.fetch_add(-1, std::memory_order_relaxed);
                    }
                }
            }
        }
    }
}


void replica_sync_loop(std::unordered_map<uint32_t, Replica>& replicas, std::mutex& replicas_lock) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::lock_guard lg(replicas_lock);
        for (auto& [id, replica] : replicas) {
            std::lock_guard rlg(replica.buffer.lock);
            std::vector<struct iovec> iov;
            iov.reserve(replica.buffer.commands.size());
            for (const auto& cmd : replica.buffer.commands) {
                spdlog::info(cmd);
                iov.push_back({const_cast<char*>(cmd.data()), cmd.size()});
            }
            writev(replica.replica_socket.fd(), iov.data(), iov.size());
            replica.buffer.commands.clear();
        }
    }
}