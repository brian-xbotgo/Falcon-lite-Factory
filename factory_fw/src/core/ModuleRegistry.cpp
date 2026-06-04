#include "core/ModuleRegistry.h"

namespace ft {

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry inst;
    return inst;
}

void ModuleRegistry::add(const std::string& name,
    std::function<std::unique_ptr<ITestModule>()> factory) {
    store_.put(name, std::move(factory));
}

std::unique_ptr<ITestModule> ModuleRegistry::create(const std::string& name) const {
    void* raw = store_.get(name);
    return std::unique_ptr<ITestModule>(
        raw ? static_cast<ITestModule*>(raw) : nullptr);
}

bool ModuleRegistry::has(const std::string& name) const {
    return store_.has(name);
}

} // namespace ft
