#pragma once
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include "Storage.h"

struct StringHash {                                                                                                                                                                                   
    using is_transparent = void;                                                                                                                                                                      
    size_t operator()(std::string_view sv) const {                                                                                                                                                    
        return std::hash<std::string_view>{}(sv);                                                                                                                                                     
    }                                                                                                                                                                                                 
    size_t operator()(const std::string& s) const {                                                                                                                                                   
        return std::hash<std::string_view>{}(s);                                                                                                                                                      
    }
};

class Router {
private:
    using Handler = std::string(*)(const std::vector<std::string_view>&, Storage&);
    std::unordered_map<std::string, Handler, StringHash, std::equal_to<>> m;
    Storage& storage_;

public:

    Router(Storage& storage);

    void add(std::string name, Handler handler);

    void dispatch(std::string_view name, const std::vector<std::string_view>& args);
};

// map; cmd -> handler ptr; handler calls storage class
// args are array of string views; buffer must outlive then

 // 1. Case-insensitive lookup. Redis commands are case-insensitive (SET, set, SeT all work). Normalize the command-name view to uppercase/lowercase before the map lookup — don't store every case variant in the map.
  //  2. Lock scope. Since multiple client threads share one store behind a mutex, take the lock inside the handler only around the actual map access, not around argument parsing or reply formatting — otherwise one slow client serializes everyone else more than necessary.

//      One thing to flag for later, not now: erase()-from-front on a std::string-as-buffer is O(n) per command (shifts all remaining bytes). Fine for Phase 1 correctness; when you get to performance tuning you'll likely switch to an offset-tracking or ring buffer so you're not shifting memory on every command.