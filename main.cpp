#include <iostream>
#include <thread>
#include "Socket.h"
#include "Router.h"
#include "Storage.h"
#include "handle.h"

constexpr int LISTEN_PORT { 6379 };

int main(){
    ServerSocket server(LISTEN_PORT);
    if (!server) return 1;

    Storage storage;
    Router router(storage);
    router.add("PING", handle_ping);
    router.add("GET", handle_get);
    router.add("SET", handle_set);
    router.add("EXISTS", handle_exists);
    router.add("DEL", handle_del);

    while (true)
    {
        Socket sock = server.accept();
        if (!sock) continue;
        auto t = std::thread([&router, sock = std::move(sock)]() mutable {
            handle_client(std::move(sock), router);
        });
        t.detach();

    }

    return 0;
}


// GET bulk string value of key; null bulk string
// SET +OK\r\n
// DEL :+1\r\n if key removed else 0
// EXISTS :+1\r\n if found else 0
// PING +PONG\r\n if no arg; else bulk string

// *n\r\n<val1>\r\n

// storage class
// parsing logic
// listening loop w/ raii sockets
// router w/ dispatch to handler
// return handler response

// server socket listens, spawns thread on each accept
    // thread owns client, parses recv until valid
    // dispatches to router
    // sends response