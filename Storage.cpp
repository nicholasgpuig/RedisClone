#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <optional>
#include "Storage.h"

void Storage::set(std::string key, std::string value) {
    std::lock_guard lg(lock);
    m_[std::move(key)] = std::move(value);
}

StorageResult<std::string> Storage::get(std::string_view key) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it != m_.end()) {
        if (const auto* s = std::get_if<std::string>(&it->second)) {
            return {*s, StorageError::Ok};
        }
        return {{}, StorageError::WrongType};
    }
    return {{}, StorageError::NotFound};
}

bool Storage::exists(std::string_view key) {
    std::lock_guard lg(lock);
    return m_.find(key) != m_.end();
}

size_t Storage::del(std::string_view key) {
    std::lock_guard lg(lock);
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
    if (it == m_.end()) {
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
    if (it == m_.end()) return {{}, StorageError::NotFound};
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
    if (l->size() == 0) m_.erase(it);
    return {val, StorageError::Ok};
}

StorageResult<size_t> Storage::deque_len(std::string_view key) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end()) return {0, StorageError::Ok};
    const auto* l = std::get_if<std::deque<std::string>>(&it->second);
    if (l == nullptr) return {{}, StorageError::WrongType};

    return {l->size(), StorageError::Ok};
}

StorageResult<std::vector<std::string>> Storage::deque_range(std::string_view key, ssize_t lIndex, ssize_t rIndex) {
    std::lock_guard lg(lock);
    auto it = m_.find(key);
    if (it == m_.end()) return {{}, StorageError::Ok};
    const auto* l = std::get_if<std::deque<std::string>>(&it->second);
    if (l == nullptr) return {{}, StorageError::WrongType};

    size_t n = l->size();
    if (lIndex < 0) lIndex = std::max(lIndex + n, (size_t)0);
    if (rIndex < 0) rIndex = std::max(rIndex + n, (size_t)0);
    StorageResult<std::vector<std::string>> res{{}, StorageError::Ok};
    if (lIndex < n && lIndex <= rIndex) {
        const size_t rBound {std::min(n, static_cast<size_t>(rIndex + 1))};
        for (int i = lIndex; i < rBound; ++i) {
            res.value.push_back((*l)[i]);
        }
    }
    return res;
}