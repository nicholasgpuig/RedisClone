#include <iostream>
#include <thread>
#include "Socket.h"

constexpr int LISTEN_PORT { 6379 };

int main(){
    ServerSocket server(LISTEN_PORT);
    if (!server) return 1;

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