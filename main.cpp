#include <iostream>
#include <thread>
#include "Socket.h"
#include "Router.h"
#include "Storage.h"

struct Command {
    std::string_view name;
    std::vector<std::string_view> args;
};

enum class ParseResponseType {
    COMPLETE,
    INCOMPLETE,
    ERROR,
};

struct CommandArrayState {
    std::vector<Command> commands;
    int start_idx {0};
    int commands_remaining {-1};
    int end_idx {0};
    // parsed commands, remaining command ct, start idx, end to know bytes consumed
    // won't include buffer due to pipelining; should be filled by parse_commands until parsing complete (ie got all n remaining)
};

constexpr int LISTEN_PORT { 6379 };

ParseResponseType parse_commands(std::string_view buf, CommandArrayState& state);
void handle_client(Socket&& s, Router& router);
#include <vector>

int main(){
    ServerSocket server(LISTEN_PORT);
    Storage storage;
    Router router(storage);

    if (!server) return 1;

    while (true)
    {
        Socket sock = server.accept();
        

    }
    // std::vector<Command> bleh;
    // parse_commands("*51\r\nlsdfj\r\n", bleh, 0, -1);
    // parse_commands("51\r\nlsdfj\r\n", bleh, 0, -1);
    // parse_commands("*ab\r\nlsdfj\r\n", bleh, 0, -1);

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