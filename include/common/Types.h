#pragma once
#include <string>
#include <atomic>

namespace ft {

// TODO: migrate actual protocol constants from old include/common/Types.h

struct GlobalState {
    std::atomic<bool> running{true};
};

GlobalState& globals();

} // namespace ft
