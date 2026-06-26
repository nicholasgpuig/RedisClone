#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

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