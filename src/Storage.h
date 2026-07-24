#pragma once
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <string>
#include <string_view>
#include <variant>
#include <mutex>
#include <optional>
#include <chrono>
#include <atomic>
#include "Socket.h"
#include "StringHash.h"

struct Stats {
    std::atomic<int64_t> clients_connected{0};
    std::atomic<uint64_t> total_commands{0};
    std::atomic<uint64_t> bytes_read{0};
    std::atomic<uint64_t> bytes_written{0};
};

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

struct ReplicaBuffer {
    std::vector<std::string> commands;
    std::mutex lock;
};

struct Replica {
    Socket replica_socket;
    ReplicaBuffer buffer;
};

struct Shard {
    std::unordered_map<std::string, std::variant<std::string, std::deque<std::string>>, StringHash, std::equal_to<>> m_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point, StringHash, std::equal_to<>> expiry;
    std::unordered_map<uint32_t, ReplicaBuffer> buffers;
    std::mutex lock;
};

class Storage {
    using RedisValue = std::variant<
        // std::unordered_map<std::string, std::string>, // could implement later if i have time lol
        // std::unordered_set<std::string>,
        std::deque<std::string>,
        std::string
    >;
    std::atomic<uint32_t> next_replica_id{0};
    std::vector<Socket> replica_sockets;
    std::unordered_map<uint32_t, Replica> replicas;
    std::mutex replicas_lock; // separate lock to write+erase+read from rep buffers

    static constexpr int NUM_SHARDS { 16 };
    std::array<Shard, NUM_SHARDS> shards;
    bool _check_and_delete_if_expr(std::string_view, Shard&);
    Shard& _shard_for(std::string_view);

public:
    Stats stats;
    void set(std::string, std::string);
    StorageResult<std::string> get(std::string_view);
    bool exists(std::string_view);
    size_t del(std::string_view);
    StorageResult<size_t> deque_push(std::string_view, std::string, bool);
    StorageResult<std::string> deque_pop(std::string_view, bool);
    StorageResult<size_t> deque_len(std::string_view);
    StorageResult<std::vector<std::string>> deque_range(std::string_view, ssize_t, ssize_t);
    size_t expire(std::string_view, int);
    size_t persist(std::string_view);
    int ttl(std::string_view);
    std::string info();
    void serialize_shard(int, uint32_t, std::vector<std::pair<std::string, std::string>>&, std::vector<std::pair<std::string, std::deque<std::string>>>&);
    uint32_t get_new_replica_id() { return next_replica_id.fetch_add(1, std::memory_order_relaxed); }
    int get_num_shards() { return NUM_SHARDS; }
    std::unordered_map<uint32_t, Replica>& get_replicas() { return replicas; }
    std::mutex& get_replicas_lock() { return replicas_lock; }
    void add_replica(uint32_t, Socket);
    std::vector<std::string>* get_shard_buffer(int, uint32_t);
    void delete_shard_buffer(int, uint32_t);
    void send_to_replicas(std::string_view);
    void print_kv();
};