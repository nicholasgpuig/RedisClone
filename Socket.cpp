#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "Socket.h"
#include "handle.h"


Socket::Socket(int fd) noexcept : fd_(fd) {}

Socket::Socket() noexcept {}

Socket::~Socket() {
    if (fd_ != -1) close(fd_);
}

Socket::Socket(Socket&& sock) noexcept : fd_(sock.fd()) {
	sock.fd_ = -1;
}

Socket& Socket::operator=(Socket&& sock) noexcept {
    if (this == &sock) return *this;
    if (fd_ != -1) close(fd_);
    fd_ = sock.fd();
	sock.fd_ = -1;
    return *this;
}



ServerSocket::ServerSocket(int port) {
	fd_ = socket(AF_INET, SOCK_STREAM, 0);

	if (fd_ == -1) { return; }

	int opt = 1;
	setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	
	if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
		close(fd_); fd_ = -1; return;
	}

	if (listen(fd_, SOMAXCONN) == -1) {
		close(fd_); fd_ = -1;
	}
}

ServerSocket::~ServerSocket() {
	if (fd_ != -1) { close(fd_); }
}

Socket ServerSocket::accept() const {
	int fd = ::accept(fd_, nullptr, nullptr);
	if (fd != -1)
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return Socket(fd);
}


EpollFd::EpollFd() : epfd_(epoll_create1(EPOLL_CLOEXEC)) {}

EpollFd::~EpollFd() {
	if (epfd_ != -1) close(epfd_);
}

int EpollFd::register_connection(Connection* conn_ptr) const {
	epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = conn_ptr;
	return epoll_ctl(epfd_, EPOLL_CTL_ADD, conn_ptr->sock.fd(), &event);
}

// for registering server fd which must have .data.ptr == nullptr
int EpollFd::register_fd(int fd) const {
	epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = nullptr;
	return epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &event);
}

int EpollFd::unregister_connection(Connection* conn_ptr) const {
	int res = epoll_ctl(epfd_, EPOLL_CTL_DEL, conn_ptr->sock.fd(), nullptr);
	delete conn_ptr;
	return res;
}