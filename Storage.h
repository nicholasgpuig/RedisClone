#pragma once
#include <unordered_map>
#include <string>
#include <mutex>

class Storage {
public:
    std::unordered_map<std::string, std::string> m_;
    std::mutex lock;
};