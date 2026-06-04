#pragma once
#include "FactoryStore.h"
#include <typeindex>
#include <memory>

namespace ft {

class DriverRegistry {
public:
    template<class Interface>
    void bind(std::function<std::unique_ptr<Interface>()> factory) {
        store_.put(typeid(Interface), [f = std::move(factory)]() -> void* {
            return f().release();
        });
    }

    template<class Interface>
    std::unique_ptr<Interface> create() const {
        void* raw = store_.get(typeid(Interface));
        return std::unique_ptr<Interface>(
            raw ? static_cast<Interface*>(raw) : nullptr);
    }

    template<class Interface>
    bool has() const {
        return store_.has(typeid(Interface));
    }

private:
    FactoryStore<std::type_index> store_;
};

} // namespace ft
