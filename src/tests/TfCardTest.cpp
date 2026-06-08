#include "tests/ITestModule.h"
#include "config/PlatformConfig.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>

namespace ft {

namespace {

constexpr uint32_t kTfCardFail = 1u << 0;

std::string storageString(TestContext& ctx, const char* key,
                          const std::string& fallback = {})
{
    try {
        return ctx.config().raw().at("storage").value(key, fallback);
    } catch (...) {
        return fallback;
    }
}

int storageInt(TestContext& ctx, const char* key, int fallback)
{
    try {
        return ctx.config().raw().at("storage").value(key, fallback);
    } catch (...) {
        return fallback;
    }
}

bool pathExists(const std::string& path)
{
    return !path.empty() && ::access(path.c_str(), F_OK) == 0;
}

std::string readTextFile(const std::string& path)
{
    std::ifstream file(path);
    std::string value;
    std::getline(file, value);
    return value;
}

bool isAllDigits(const std::string& value)
{
    if (value.empty()) {
        return false;
    }
    for (char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

int blockRemovable(const std::string& blockName)
{
    const auto value = readTextFile("/sys/block/" + blockName + "/removable");
    return value == "1" ? 1 : 0;
}

std::vector<std::string> blockPartitions(const std::string& blockName)
{
    std::vector<std::string> partitions;
    const auto base = "/sys/block/" + blockName;
    DIR* dir = ::opendir(base.c_str());
    if (!dir) {
        return partitions;
    }

    while (auto* entry = ::readdir(dir)) {
        std::string name = entry->d_name;
        if (name.rfind(blockName + "p", 0) != 0) {
            continue;
        }
        const auto suffix = name.substr(blockName.size() + 1);
        if (isAllDigits(suffix)) {
            partitions.push_back("/dev/" + name);
        }
    }
    ::closedir(dir);
    std::sort(partitions.begin(), partitions.end());
    return partitions;
}

std::vector<std::string> discoverTfPartitions()
{
    struct Candidate {
        std::string block;
        int removable;
        std::vector<std::string> partitions;
    };

    std::vector<Candidate> candidates;
    DIR* dir = ::opendir("/sys/block");
    if (!dir) {
        return {};
    }

    while (auto* entry = ::readdir(dir)) {
        std::string name = entry->d_name;
        if (name.rfind("mmcblk", 0) != 0) {
            continue;
        }
        if (!isAllDigits(name.substr(6))) {
            continue;
        }
        candidates.push_back({name, blockRemovable(name), blockPartitions(name)});
    }
    ::closedir(dir);

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs,
                                                       const Candidate& rhs) {
        if (lhs.removable != rhs.removable) {
            return lhs.removable > rhs.removable;
        }
        return lhs.block > rhs.block;
    });

    std::vector<std::string> discovered;
    std::fprintf(stderr, "[TfCardTest] block candidates:");
    for (const auto& candidate : candidates) {
        std::fprintf(stderr, " %s(removable=%d parts=%zu)",
                     candidate.block.c_str(), candidate.removable,
                     candidate.partitions.size());
        const bool canBeTfCard = candidate.removable == 1 || candidate.block != "mmcblk0";
        if (!canBeTfCard) {
            continue;
        }
        if (candidate.partitions.empty()) {
            const auto wholeDevice = "/dev/" + candidate.block;
            if (pathExists(wholeDevice)) {
                discovered.push_back(wholeDevice);
            }
        } else {
            for (const auto& partition : candidate.partitions) {
                if (pathExists(partition)) {
                    discovered.push_back(partition);
                }
            }
        }
    }
    std::fprintf(stderr, "\n");
    return discovered;
}

std::string selectTfPartition(const std::string& configured)
{
    if (pathExists(configured)) {
        return configured;
    }

    const auto discovered = discoverTfPartitions();
    if (!discovered.empty()) {
        return discovered.front();
    }
    return configured;
}

bool isMountedAt(const std::string& mountPoint)
{
    if (mountPoint.empty()) {
        return false;
    }

    std::ifstream mounts("/proc/mounts");
    std::string dev;
    std::string target;
    while (mounts >> dev >> target) {
        if (target == mountPoint) {
            return true;
        }
        std::string rest;
        std::getline(mounts, rest);
    }
    return false;
}

bool mountPartition(const std::string& partition, const std::string& mountPoint)
{
    if (partition.empty() || mountPoint.empty()) {
        return false;
    }

    if (::mkdir(mountPoint.c_str(), 0755) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "[TfCardTest] mkdir mount point failed path=%s errno=%d\n",
                     mountPoint.c_str(), errno);
        return false;
    }

    constexpr const char* kFilesystems[] = {"exfat", "vfat", "ext4"};
    for (const char* fs : kFilesystems) {
        if (::mount(partition.c_str(), mountPoint.c_str(), fs, 0, nullptr) == 0) {
            std::fprintf(stderr, "[TfCardTest] mounted partition=%s mount_point=%s fs=%s\n",
                         partition.c_str(), mountPoint.c_str(), fs);
            return true;
        }
        std::fprintf(stderr,
                     "[TfCardTest] mount failed partition=%s mount_point=%s fs=%s errno=%d\n",
                     partition.c_str(), mountPoint.c_str(), fs, errno);
    }
    return false;
}

uint64_t availableBytes(const std::string& mountPoint)
{
    struct statvfs st {};
    if (mountPoint.empty() || ::statvfs(mountPoint.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
}

void fillPattern(std::vector<char>& buffer, int blockIndex)
{
    const auto seed = static_cast<unsigned char>(0xa5 ^ (blockIndex & 0xff));
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<char>(seed ^ (i & 0xff));
    }
}

double writeSpeedMbPerSec(const std::string& path, int testMb, bool& ok)
{
    ok = false;
    if (testMb <= 0) {
        return 0.0;
    }

    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        std::fprintf(stderr, "[TfCardTest] open speed file failed path=%s\n", path.c_str());
        return 0.0;
    }

    std::vector<char> buffer(1024 * 1024);
    const auto start = std::chrono::steady_clock::now();
    int writtenMb = 0;
    for (; writtenMb < testMb; ++writtenMb) {
        fillPattern(buffer, writtenMb);
        const char* ptr = buffer.data();
        size_t remain = buffer.size();
        while (remain > 0) {
            const ssize_t n = ::write(fd, ptr, remain);
            if (n <= 0) {
                std::fprintf(stderr,
                             "[TfCardTest] write failed path=%s written_mb=%d\n",
                             path.c_str(), writtenMb);
                ::close(fd);
                return 0.0;
            }
            ptr += n;
            remain -= static_cast<size_t>(n);
        }
    }
    ::fsync(fd);
    const auto end = std::chrono::steady_clock::now();
    ::close(fd);

    const double elapsedSec = std::chrono::duration<double>(end - start).count();
    if (elapsedSec <= 0.0) {
        return 0.0;
    }

    ok = true;
    return static_cast<double>(writtenMb) / elapsedSec;
}

double readSpeedMbPerSec(const std::string& path, int testMb, bool& ok)
{
    ok = false;
    if (testMb <= 0) {
        return 0.0;
    }

    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[TfCardTest] open read file failed path=%s\n", path.c_str());
        return 0.0;
    }

    std::vector<char> buffer(1024 * 1024);
    std::vector<char> expected(1024 * 1024);
    const auto start = std::chrono::steady_clock::now();
    int readMb = 0;
    for (; readMb < testMb; ++readMb) {
        char* ptr = buffer.data();
        size_t remain = buffer.size();
        while (remain > 0) {
            const ssize_t n = ::read(fd, ptr, remain);
            if (n <= 0) {
                std::fprintf(stderr,
                             "[TfCardTest] read failed path=%s read_mb=%d\n",
                             path.c_str(), readMb);
                ::close(fd);
                return 0.0;
            }
            ptr += n;
            remain -= static_cast<size_t>(n);
        }

        fillPattern(expected, readMb);
        if (std::memcmp(buffer.data(), expected.data(), buffer.size()) != 0) {
            std::fprintf(stderr,
                         "[TfCardTest] read verify failed path=%s read_mb=%d\n",
                         path.c_str(), readMb);
            ::close(fd);
            return 0.0;
        }
    }
    const auto end = std::chrono::steady_clock::now();
    ::close(fd);

    const double elapsedSec = std::chrono::duration<double>(end - start).count();
    if (elapsedSec <= 0.0) {
        return 0.0;
    }

    ok = true;
    return static_cast<double>(readMb) / elapsedSec;
}

TestResult resultWithCode(bool pass, uint32_t errorCode, const std::string& detail)
{
    auto result = pass ? TestResult::pass() : TestResult::fail(detail);
    result.data["error_code"] = errorCode;
    return result;
}

} // namespace

class TfCardTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        const auto configuredPartition = storageString(ctx, "tf_partition", "/dev/mmcblk1p1");
        const auto partition = selectTfPartition(configuredPartition);
        const auto mountPoint = storageString(ctx, "tf_mount_point", "/mnt/sdcard");
        const auto speedFile = storageString(
            ctx, "tf_speed_test_file", mountPoint + "/factory_tf_speed_test.bin");
        const int configuredTestMb = storageInt(ctx, "tf_speed_test_mb", 64);
        const int minWriteSpeed = storageInt(ctx, "tf_min_write_speed_mb_s", 10);
        const int minReadSpeed = storageInt(ctx, "tf_min_read_speed_mb_s", 10);

        const bool partitionExists = pathExists(partition);
        bool mounted = isMountedAt(mountPoint);
        bool autoMounted = false;
        if (partitionExists && !mounted) {
            autoMounted = mountPartition(partition, mountPoint);
            mounted = isMountedAt(mountPoint);
        }
        const uint64_t availBytes = availableBytes(mountPoint);
        const int availableMb = static_cast<int>(availBytes / (1024 * 1024));
        const int testMb = availableMb > 0
            ? std::min(configuredTestMb, std::max(1, availableMb / 2))
            : configuredTestMb;

        std::fprintf(stderr,
                     "[TfCardTest] configured_partition=%s partition=%s exists=%d mount_point=%s mounted=%d auto_mounted=%d available_mb=%d test_mb=%d min_write=%dMB/s min_read=%dMB/s\n",
                     configuredPartition.c_str(), partition.c_str(), partitionExists ? 1 : 0,
                     mountPoint.c_str(), mounted ? 1 : 0,
                     autoMounted ? 1 : 0, availableMb, testMb,
                     minWriteSpeed, minReadSpeed);

        if (!partitionExists) {
            return resultWithCode(false, kTfCardFail, "tf card partition missing");
        }
        if (!mounted) {
            return resultWithCode(false, kTfCardFail, "tf card not mounted");
        }
        if (availableMb <= 0) {
            return resultWithCode(false, kTfCardFail, "tf card no writable space");
        }

        bool writeOk = false;
        const double writeSpeed = writeSpeedMbPerSec(speedFile, testMb, writeOk);
        std::fprintf(stderr,
                     "[TfCardTest] write_test file=%s size_mb=%d speed=%.2fMB/s threshold=%dMB/s result=%s\n",
                     speedFile.c_str(), testMb, writeSpeed, minWriteSpeed,
                     writeOk && writeSpeed >= minWriteSpeed ? "PASS" : "FAIL");

        bool readOk = false;
        const double readSpeed = writeOk
            ? readSpeedMbPerSec(speedFile, testMb, readOk)
            : 0.0;
        std::fprintf(stderr,
                     "[TfCardTest] read_test file=%s size_mb=%d speed=%.2fMB/s threshold=%dMB/s verify=%s result=%s\n",
                     speedFile.c_str(), testMb, readSpeed, minReadSpeed,
                     readOk ? "PASS" : "FAIL",
                     readOk && readSpeed >= minReadSpeed ? "PASS" : "FAIL");
        ::unlink(speedFile.c_str());

        const bool pass = writeOk && writeSpeed >= minWriteSpeed &&
                          readOk && readSpeed >= minReadSpeed;
        auto result = resultWithCode(pass, pass ? 0 : kTfCardFail,
                                     pass ? "" : "tf card read/write speed low");
        result.data["avg_write_speed_mb_s"] = writeSpeed;
        result.data["avg_read_speed_mb_s"] = readSpeed;
        result.data["test_size_mb"] = testMb;
        return result;
    }
};

REGISTER_TEST_MODULE("tfcard", TfCardTest);

} // namespace ft
