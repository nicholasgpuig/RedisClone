#pragma once
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include "Storage.h"
#include "StringHash.h"

class Router {
private:
    using Handler = std::string(*)(const std::vector<std::string>&, Storage&);
    std::unordered_map<std::string, Handler, StringHash, std::equal_to<>> m;
    Storage& storage_;

public:

    Router(Storage& storage);

    void add(std::string name, Handler handler);

    [[nodiscard]] std::string dispatch(std::string_view name, const std::vector<std::string>& args);
};