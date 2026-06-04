#pragma once
#include <unordered_map>
#include <functional>

namespace ft {

template<class Key>
class FactoryStore {
public:
    using FactoryFunc = std::function<void*()>;

    void put(Key k, FactoryFunc f) {
        store_[std::move(k)] = std::move(f);
    }

    void* get(const Key& k) const {
        auto it = store_.find(k);
        return (it != store_.end()) ? it->second() : nullptr;
    }

    bool has(const Key& k) const {
        return store_.count(k);
    }

private:
    std::unordered_map<Key, FactoryFunc> store_;
};

} // namespace ft
