#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <optional>
#include "Storage.h"

// assuming lock is held and key exists
bool Storage::_check_and_delete_if_expr(std::string_view key) {
    auto eit = expiry.find(key);
    if (eit != expiry.end() && eit->second <= std::chrono::steady_clock::now()) {
        expiry.erase(eit);
        m_.erase(m_.find(key));
        return true;
    }
    return false;
}

void Storage::set(std::string key, std::string value) {
    std::lock_guard lg(lock);
    auto it = expiry.find(std::string_view(key));
    if (it != expiry.end()) expiry.erase(it);
    m_[std::move(key)] = std::move(value);
}

StorageResult<std::string> Storage::get(std::string_view key) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end() || _check_and_delete_if_expr(key)) return {{}, StorageError::NotFound};
    if (const auto* s = std::get_if<std::string>(&it->second)) {
        return {*s, StorageError::Ok};
    }
    return {{}, StorageError::WrongType};
    
}

bool Storage::exists(std::string_view key) {
    std::lock_guard lg(lock);
    return (m_.find(key) != m_.end() && !_check_and_delete_if_expr(key));
}

size_t Storage::del(std::string_view key) {
    std::lock_guard lg(lock);
    auto eit = expiry.find(key);
    if (eit != expiry.end()) expiry.erase(eit);
    auto it = m_.find(key);
    if (it != m_.end()) {
        m_.erase(it);
        return 1;
    }
    return 0;
}

StorageResult<size_t> Storage::deque_push(std::string_view key, std::string value, bool isLeftPush) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end() || _check_and_delete_if_expr(key)) {
        m_[std::string(key)] = std::deque<std::string>();
        it = m_.find(key);
    }
    auto* l = std::get_if<std::deque<std::string>>(&it->second);
    if (l == nullptr) return {0, StorageError::WrongType};
    
    if (isLeftPush) {
        l->push_front(std::move(value));
    } else {
        l->push_back(std::move(value));
    }

    return {l->size(), StorageError::Ok};
}

StorageResult<std::string> Storage::deque_pop(std::string_view key, bool isLeftPop) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end() || _check_and_delete_if_expr(key)) return {{}, StorageError::NotFound};
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
        m_.erase(it);
        auto eit = expiry.find(key);
        if (eit != expiry.end()) expiry.erase(eit);
    }
    return {val, StorageError::Ok};
}

StorageResult<size_t> Storage::deque_len(std::string_view key) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end() || _check_and_delete_if_expr(key)) return {0, StorageError::Ok};
    const auto* l = std::get_if<std::deque<std::string>>(&it->second);
    if (l == nullptr) return {{}, StorageError::WrongType};

    return {l->size(), StorageError::Ok};
}

StorageResult<std::vector<std::string>> Storage::deque_range(std::string_view key, ssize_t lIndex, ssize_t rIndex) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end() || _check_and_delete_if_expr(key)) return {{}, StorageError::Ok};
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
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end() || _check_and_delete_if_expr(key)) return 0;

    expiry[std::string(key)] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    return 1;
}

size_t Storage::persist(std::string_view key) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    auto eit = expiry.find(key);
    if (it == m_.end() || eit == expiry.end() || _check_and_delete_if_expr(key)) return 0;
    expiry.erase(eit);
    return 1;
}

int Storage::ttl(std::string_view key) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end() || _check_and_delete_if_expr(key)) return -2;

    auto eit = expiry.find(key);
    if (eit == expiry.end()) return -1;

    auto duration = eit->second - std::chrono::steady_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

    return seconds;
}