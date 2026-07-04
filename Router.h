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
    using Handler = std::string(*)(const std::vector<std::string>&, Storage&);
    std::unordered_map<std::string, Handler, StringHash, std::equal_to<>> m;
    Storage& storage_;

public:

    Router(Storage& storage);

    void add(std::string name, Handler handler);

    [[nodiscard]] std::string dispatch(std::string_view name, const std::vector<std::string>& args);
};