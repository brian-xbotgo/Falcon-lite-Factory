#pragma once
#include "FactoryStore.h"
#include <string>
#include <memory>

namespace ft {

class ITestModule;

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    void add(const std::string& name,
             std::function<std::unique_ptr<ITestModule>()> factory);
    std::unique_ptr<ITestModule> create(const std::string& name) const;
    bool has(const std::string& name) const;

private:
    FactoryStore<std::string> store_;
};

#define REGISTER_TEST_MODULE(name, T) \
    static bool _reg_##T = []{ \
        ModuleRegistry::instance().add(name, []{ return std::make_unique<T>(); }); \
        return true; \
    }();

} // namespace ft
