#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace ft {

struct TestResult {
    enum class Status { Pass, Fail, Skipped };
    Status status;
    std::string detail;
    nlohmann::json data;

    static TestResult pass() {
        return {Status::Pass, "", nlohmann::json::object()};
    }
    static TestResult fail(std::string msg) {
        return {Status::Fail, std::move(msg), nlohmann::json::object()};
    }
    static TestResult skipped(std::string msg) {
        return {Status::Skipped, std::move(msg), nlohmann::json::object()};
    }

    bool isPass()     const { return status == Status::Pass; }
    bool isFail()     const { return status == Status::Fail; }
    bool isSkipped()  const { return status == Status::Skipped; }
};

} // namespace ft
