#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

struct Connection;

class Socket {
private:
    int fd_{-1};

public:
    explicit Socket(int fd) noexcept;
    Socket() noexcept;
    ~Socket();
    
    Socket(Socket&&) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(Socket&&) noexcept;
    Socket& operator=(const Socket&) = delete;

    [[nodiscard]] int fd() const { return fd_; }

    explicit operator bool() const noexcept { return fd_ != -1; }
};


class ServerSocket {
    int fd_{ -1 };
public:
    explicit ServerSocket(int port);
    ~ServerSocket();
    ServerSocket(const ServerSocket&) = delete;
    ServerSocket& operator=(const ServerSocket&) = delete;
    ServerSocket(ServerSocket&&) = delete;
    ServerSocket& operator=(ServerSocket&&) = delete;

    [[nodiscard]] Socket accept() const;
    [[nodiscard]] int fd() const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ != -1; }
};


class EpollFd {
    int epfd_;
public:
    EpollFd();
    ~EpollFd();

    EpollFd(const EpollFd&) = delete;
    EpollFd& operator=(const EpollFd&) = delete;
    EpollFd(EpollFd&&) = delete;
    EpollFd& operator=(EpollFd&&) = delete;

    int register_connection(Connection*) const;
    int register_fd(int) const;
    int unregister_connection(Connection*) const;

    [[nodiscard]] int fd() const noexcept { return epfd_; }
    explicit operator bool() const noexcept { return epfd_ != -1; }
};