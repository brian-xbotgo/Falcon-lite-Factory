#pragma once
#include "core/TestResult.h"
#include "core/TestContext.h"
#include "core/ModuleRegistry.h"

namespace ft {

class ITestModule {
public:
    virtual ~ITestModule() = default;
    virtual TestResult run(TestContext& ctx) = 0;
};

} // namespace ft
