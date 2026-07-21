#pragma once
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include "Storage.h"
#include "StringHash.h"
#include "handle.h"

class Router {
private:
    using Handler = std::pair<std::string, HandlerAction>(*)(const std::vector<std::string>&, Storage&);
    std::unordered_map<std::string, Handler, StringHash, std::equal_to<>> m;
    Storage& storage_;

public:

    Router(Storage& storage);

    void add(std::string name, Handler handler);

    [[nodiscard]] std::pair<std::string, HandlerAction> dispatch(std::string_view name, const std::vector<std::string>& args);

    void print() { storage_.print_kv(); }

    void add_all() {
        this->add("PING", handle_ping);
        this->add("GET", handle_get);
        this->add("SET", handle_set);
        this->add("EXISTS", handle_exists);
        this->add("DEL", handle_del);
        this->add("LPUSH", handle_lpush);
        this->add("RPUSH", handle_rpush);
        this->add("LPOP", handle_lpop);
        this->add("RPOP", handle_rpop);
        this->add("LLEN", handle_llen);
        this->add("LRANGE", handle_lrange);
        this->add("EXPIRE", handle_expire);
        this->add("PERSIST", handle_persist);
        this->add("TTL", handle_ttl);
        this->add("INFO", handle_info);
        this->add("PSYNC", handle_psync);
    }
};