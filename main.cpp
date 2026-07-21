#include <unordered_map>
#include <spdlog/spdlog.h>
#include <thread>
#include <functional>
#include <optional>
#include <charconv>
#include "Socket.h"
#include "Router.h"
#include "Storage.h"
#include "handle.h"
#include "replica.h"
#include "master.h"

struct CmdOptions {
    uint32_t listen_port { 6379 };
    uint32_t threads { 8 };
    std::optional<char*> master_ipv4;
    std::optional<int> master_port;
};

std::optional<std::string> parse_cmd_options(int argc, char* argv[], CmdOptions& options);

int main(int argc, char* argv[]){
    CmdOptions options;
    auto err = parse_cmd_options(argc, argv, options);
    if (err) {
        spdlog::error(*err);
        return 1;
    }
    Storage storage;
    Router router(storage);

    const ServerSocket server(options.listen_port);
    if (!server) {
        spdlog::error("Server socket failed to start");
        return 1;
    }
    if (options.master_ipv4) {
        Socket master_sock = initialize_replica(*options.master_ipv4, *options.master_port);
        if (!master_sock) return 1;
        spdlog::info("Connected to master at {}:{}", *options.master_ipv4, *options.master_port);

        auto err = master_handshake(master_sock.fd());
        if (err) {
            spdlog::error(*err);
            return 1;
        }
        router.add("SET", replica_handle_set);
        router.add("RPUSH", replica_handle_rpush);
        spdlog::info("Listening for commands");

        replica_worker_loop(std::move(master_sock), router);
    } else {
        router.add_all(); // all master commands available

        const EpollFd epoll_fd;
        if (!epoll_fd) {
            spdlog::error("Epoll fd failed to start");
            return 1;
        }
        epoll_fd.register_fd(server.fd());

        std::vector<std::thread> threads;
        for (uint32_t i = 0; i + 1 < options.threads; ++i) {
            threads.emplace_back(master_worker_loop, std::ref(epoll_fd), std::ref(server), std::ref(storage), std::ref(router));
            threads[i].detach();
        }
        spdlog::info("Started listening on port {} with {} threads", options.listen_port, options.threads);
        master_worker_loop(epoll_fd, server, storage, router);
    }

    return 0;
}


std::optional<std::string> parse_cmd_options(int argc, char* argv[], CmdOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-p") {
            if (i + 1 >= argc) return "Incorrect arg count for -p";
            ++i;
            uint32_t port;
            auto result = std::from_chars(argv[i], argv[i] + strlen(argv[i]), port);
            if (result.ec == std::errc::invalid_argument || port == 0) return "Invalid argument for -p: invalid port number";
            options.listen_port = port;
        } else if (arg == "-t") {
            if (i + 1 >= argc) return "Incorrect arg count for -t";
            ++i;
            uint32_t threads;
            auto result = std::from_chars(argv[i], argv[i] + strlen(argv[i]), threads);
            if (result.ec == std::errc::invalid_argument || threads == 0) return "Invalid argument for -t: invalid number of threads";
            options.threads = threads;
        } else if (arg == "--replica") {
            if (i + 2 >= argc) return "Incorrect arg count for --replica";
            ++i;
            options.master_ipv4 = argv[i];
            ++i;
            uint32_t master_port;
            auto result = std::from_chars(argv[i], argv[i] + strlen(argv[i]), master_port);
            if (result.ec == std::errc::invalid_argument || master_port == 0) return "Invalid argument for --replica: invalid master port";
            options.master_port = master_port;
        } else {
            return "Invalid argument";
        }
    }
    return std::nullopt;
}