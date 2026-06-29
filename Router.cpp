
#include <string>
#include <string_view>
#include <vector>
#include "Storage.h"
#include "Router.h"

    Router::Router(Storage& storage) : storage_(storage) {}

    void Router::add(std::string name, Handler handler) {
        m[name] = handler;
    }

    void Router::dispatch(std::string_view name, const std::vector<std::string_view>& args) {
        auto it = m.find(name);
        if (it != m.end()) {
            it->second(args, storage_);
        }
    }

// map; cmd -> handler ptr; handler calls storage class
// args are array of string views; buffer must outlive then

 // 1. Case-insensitive lookup. Redis commands are case-insensitive (SET, set, SeT all work). Normalize the command-name view to uppercase/lowercase before the map lookup — don't store every case variant in the map.
  //  2. Lock scope. Since multiple client threads share one store behind a mutex, take the lock inside the handler only around the actual map access, not around argument parsing or reply formatting — otherwise one slow client serializes everyone else more than necessary.

//      One thing to flag for later, not now: erase()-from-front on a std::string-as-buffer is O(n) per command (shifts all remaining bytes). Fine for Phase 1 correctness; when you get to performance tuning you'll likely switch to an offset-tracking or ring buffer so you're not shifting memory on every command.