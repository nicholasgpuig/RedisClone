#pragma once
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <string>
#include <string_view>
#include <variant>
#include <mutex>
#include <optional>
#include "StringHash.h"

enum class StorageError {
    NotFound,
    WrongType,
    Ok
};

template <typename T>
struct StorageResult {
    T value;
    StorageError error;
    bool ok() const { return error == StorageError::Ok; }
};

class Storage {
    using RedisValue = std::variant<
        std::unordered_map<std::string, std::string>,
        std::unordered_set<std::string>,
        std::deque<std::string>,
        std::string
    >;
    std::unordered_map<std::string, RedisValue, StringHash, std::equal_to<>> m_;
    std::mutex lock;

public:

    void set(std::string, std::string);
    StorageResult<std::string> get(std::string_view);
    bool exists(std::string_view);
    size_t del(std::string_view);
    StorageResult<size_t> deque_push(std::string_view, std::string, bool);
    StorageResult<std::string> deque_pop(std::string_view, bool);
    StorageResult<size_t> deque_len(std::string_view);
    StorageResult<std::vector<std::string>> deque_range(std::string_view, ssize_t, ssize_t);
};