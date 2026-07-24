#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <optional>
#include "Storage.h"

Shard& Storage::_shard_for(std::string_view key) {
    return shards[std::hash<std::string_view>{}(key) % NUM_SHARDS];
}

// assuming lock is held and key exists
bool Storage::_check_and_delete_if_expr(std::string_view key, Shard& shard) {
    auto eit = shard.expiry.find(key);
    if (eit != shard.expiry.end() && eit->second <= std::chrono::steady_clock::now()) {
        shard.expiry.erase(eit);
        shard.m_.erase(shard.m_.find(key));
        return true;
    }
    return false;
}

void Storage::set(std::string key, std::string value) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.expiry.find(std::string_view(key));
    if (it != shard.expiry.end()) shard.expiry.erase(it);
    for (auto& [id, buffer] : shard.buffers) {
        buffer.commands.emplace_back("*3\r\n$3\r\nSET\r\n$" + std::to_string(key.size()) + "\r\n" + std::string(key) + "\r\n$" + std::to_string(value.size()) + "\r\n" + value);
    }
    shard.m_[std::move(key)] = std::move(value);
}

StorageResult<std::string> Storage::get(std::string_view key) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.m_.find(key);
    if (it == shard.m_.end() || _check_and_delete_if_expr(key, shard)) return {{}, StorageError::NotFound};
    if (const auto* s = std::get_if<std::string>(&it->second)) {
        return {*s, StorageError::Ok};
    }
    return {{}, StorageError::WrongType};
    
}

bool Storage::exists(std::string_view key) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    return (shard.m_.find(key) != shard.m_.end() && !_check_and_delete_if_expr(key, shard));
}

size_t Storage::del(std::string_view key) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto eit = shard.expiry.find(key);
    if (eit != shard.expiry.end()) shard.expiry.erase(eit);
    auto it = shard.m_.find(key);
    if (it != shard.m_.end()) {
        shard.m_.erase(it);
        return 1;
    }
    return 0;
}

StorageResult<size_t> Storage::deque_push(std::string_view key, std::string value, bool isLeftPush) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.m_.find(key);
    if (it == shard.m_.end() || _check_and_delete_if_expr(key, shard)) {
        shard.m_[std::string(key)] = std::deque<std::string>();
        it = shard.m_.find(key);
    }
    auto* l = std::get_if<std::deque<std::string>>(&it->second);
    if (l == nullptr) return {0, StorageError::WrongType};
    
    for (auto& [id, buffer] : shard.buffers) {
        buffer.commands.emplace_back("*3\r\n$5\r\nRPUSH\r\n$" + std::to_string(key.size()) + "\r\n" + std::string(key) + "\r\n$" + std::to_string(value.size()) + "\r\n" + value);
    }

    if (isLeftPush) {
        l->push_front(std::move(value));
    } else {
        l->push_back(std::move(value));
    }

    return {l->size(), StorageError::Ok};
}

StorageResult<std::string> Storage::deque_pop(std::string_view key, bool isLeftPop) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.m_.find(key);
    if (it == shard.m_.end() || _check_and_delete_if_expr(key, shard)) return {{}, StorageError::NotFound};
    auto* l = std::get_if<std::deque<std::string>>(&it->second);
    if (l == nullptr) return {{}, StorageError::WrongType};


    std::string val;
    if (isLeftPop) {
        val = std::move(l->front());
        l->pop_front();
    } else {
        val = std::move(l->back());
        l->pop_back();
    }
    if (l->size() == 0) {
        shard.m_.erase(it);
        auto eit = shard.expiry.find(key);
        if (eit != shard.expiry.end()) shard.expiry.erase(eit);
    }
    return {val, StorageError::Ok};
}

StorageResult<size_t> Storage::deque_len(std::string_view key) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.m_.find(key);
    if (it == shard.m_.end() || _check_and_delete_if_expr(key, shard)) return {0, StorageError::Ok};
    const auto* l = std::get_if<std::deque<std::string>>(&it->second);
    if (l == nullptr) return {{}, StorageError::WrongType};

    return {l->size(), StorageError::Ok};
}

StorageResult<std::vector<std::string>> Storage::deque_range(std::string_view key, ssize_t lIndex, ssize_t rIndex) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.m_.find(key);
    if (it == shard.m_.end() || _check_and_delete_if_expr(key, shard)) return {{}, StorageError::Ok};
    const auto* l = std::get_if<std::deque<std::string>>(&it->second);
    if (l == nullptr) return {{}, StorageError::WrongType};

    size_t n = l->size();
    if (lIndex < 0) lIndex = std::max(lIndex + (ssize_t)n, (ssize_t)0);
    if (rIndex < 0) rIndex = std::max(rIndex + (ssize_t)n, (ssize_t)0);
    StorageResult<std::vector<std::string>> res{{}, StorageError::Ok};
    if (lIndex < n && lIndex <= rIndex) {
        const size_t rBound {std::min(n, static_cast<size_t>(rIndex + 1))};
        for (int i = lIndex; i < rBound; ++i) {
            res.value.push_back((*l)[i]);
        }
    }
    return res;
}

size_t Storage::expire(std::string_view key, int seconds) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.m_.find(key);
    if (it == shard.m_.end() || _check_and_delete_if_expr(key, shard)) return 0;

    shard.expiry[std::string(key)] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    return 1;
}

size_t Storage::persist(std::string_view key) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.m_.find(key);
    auto eit = shard.expiry.find(key);
    if (it == shard.m_.end() || eit == shard.expiry.end() || _check_and_delete_if_expr(key, shard)) return 0;
    shard.expiry.erase(eit);
    return 1;
}

int Storage::ttl(std::string_view key) {
    Shard& shard = _shard_for(key);
    std::lock_guard lg(shard.lock);
    auto it = shard.m_.find(key);
    if (it == shard.m_.end() || _check_and_delete_if_expr(key, shard)) return -2;

    auto eit = shard.expiry.find(key);
    if (eit == shard.expiry.end()) return -1;

    auto duration = eit->second - std::chrono::steady_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

    return seconds;
}

std::string Storage::info() {
    return std::format("Clients: {}\nRead: {}\nWrite: {}\nCommand count: {}\n", 
    stats.clients_connected.load(std::memory_order_relaxed),
    stats.bytes_read.load(std::memory_order_relaxed),
    stats.bytes_written.load(std::memory_order_relaxed),
    stats.total_commands.load(std::memory_order_relaxed));
}

void Storage::serialize_shard(int shard_id, uint32_t replica_id, std::vector<std::pair<std::string, std::string>>& pairs, std::vector<std::pair<std::string, std::deque<std::string>>>& lists) {
    if (shard_id >= NUM_SHARDS) return;

    auto& shard = shards[shard_id];
    {
        std::lock_guard lg(shard.lock);
        for (auto& [key, value] : shard.m_) {
            if (const auto* val_ptr = std::get_if<std::string>(&value)) {
                pairs.emplace_back(key, *val_ptr);
            } else {
                const auto& deque_val = std::get<std::deque<std::string>>(value);
                lists.emplace_back(key, deque_val);
            }
        }
        shard.buffers.try_emplace(replica_id);
    }
}

void Storage::add_replica(uint32_t replica_id, Socket replica_socket) {
    std::lock_guard lg(replicas_lock);
    replicas.try_emplace(replica_id, std::move(replica_socket));
}

std::vector<std::string>* Storage::get_shard_buffer(int shard_id, uint32_t replica_id) {
    Shard& shard = shards[shard_id];
    auto it = shard.buffers.find(replica_id);
    if (it == shard.buffers.end()) return nullptr;

    return &it->second.commands;
}
void Storage::delete_shard_buffer(int shard_id, uint32_t replica_id) {
    Shard& shard = shards[shard_id];
    auto it = shard.buffers.find(replica_id);
    if (it == shard.buffers.end()) return;

    shard.buffers.erase(it);
}

void Storage::send_to_replicas(std::string_view sv) {
    std::lock_guard lg(replicas_lock);
    for (auto& [id, replica] : replicas) {
        std::lock_guard rlg(replica.buffer.lock);
        replica.buffer.commands.emplace_back(sv);
    }
}

#include <iostream>
void Storage::print_kv() {
    for (int i = 0; i < 16; ++i) {
        Shard& sh = shards[i];
        std::lock_guard lg(sh.lock);
        for (auto& [key, value] : sh.m_) {
            if (auto* l = std::get_if<std::string>(&value)) {
                std::cout << std::format("{}: {}", key, *l) << '\n';
            } else {
                auto d = std::get<std::deque<std::string>>(value);
                std::cout << "List: " << key << '\n';
                for (auto& item : d) {
                    std::cout << item << ' ';
                }
                std::cout << "\n\n";
            }
        }
    }
}