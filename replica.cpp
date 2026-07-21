#include <optional>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "Socket.h"
#include "handle.h"
#include "Router.h"
#include "spdlog/spdlog.h"

Socket initialize_replica(char* host, uint32_t port) {
    int fd_ = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        spdlog::error("Invalid master ip. Must be valid ipv4 address: ex. 127.0.0.1");
        return Socket(-1);
    }

    if (connect(fd_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        spdlog::error("Connection to master failed");
        return Socket(-1);
    }

    return Socket(fd_);
}

std::optional<std::string> master_handshake(int master_fd) {
    std::string payload = "*1\r\n$4\r\nPING\r\n";
    int sent = send(master_fd, payload.data(), payload.size(), 0);
    if (sent == -1) return "Failed to send PING";

    char chunk[4096];
    int n = recv(master_fd, chunk, 4096, 0);
    if (n <= 0) return "Failed to receive PONG";
    std::string response = chunk;

    if (response != "+PONG\r\n") return "Invalid response from PING";

    payload = "*3\r\n$5\r\nPSYNC\r\n$1\r\n?\r\n$2\r\n-1\r\n";
    sent = send(master_fd, payload.data(), payload.size(), 0);
    if (sent == -1) return "Failed to send PSYNC";

    n = recv(master_fd, chunk, 4096, 0);
    if (n <= 0) return "Failed to receive FULLRESYNC";
    response = chunk;
    spdlog::info(response);
    if (response != "+FULLRESYNC 0 0\r\n") return "Invalid response from PSYNC";

    return std::nullopt;
}

void replica_worker_loop(Socket master_sock, Router& router) {
    std::string buf; // replace w/ ring buffer
    ClientState state;
    while (true) {
        char chunk[8192];
        auto n = recv(master_sock.fd(), chunk, sizeof(chunk), 0);
        if (n == -1) continue;
        buf.append(chunk, n);
        ParseResponseType res = parse_commands(buf, state);
        while (res == ParseResponseType::COMPLETE) {
            auto [response, action] = router.dispatch(state.command_name, state.command_args);
            router.print();

            // if (action != HandlerAction::NONE) result.actions.insert(action);
            buf.erase(0, state.start_idx);
            state = ClientState{};
            res = parse_commands(buf, state);
        }
        if (res == ParseResponseType::ERROR) {
            spdlog::error("Parsing failure");
            return;
        }
    }
}


std::pair<std::string, HandlerAction> replica_handle_set(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 2) return {};

    storage.set(args[0], args[1]);
    return {};
}

std::pair<std::string, HandlerAction> replica_handle_rpush(const std::vector<std::string>& args, Storage& storage) {
    if (args.size() != 2) return {};

    const StorageResult<size_t> res = storage.deque_push(args[0], args[1], false);
    if (res.error == StorageError::WrongType) return {};
    return {};
}