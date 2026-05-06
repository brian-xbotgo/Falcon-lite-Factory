// TF card test 14R: dd write speed test (decoupled from misc daemon)
// error_code: bit0=write speed<10MB/s, bit1=no partition, bit2=mount failed,
//             bit3=statvfs failed, bit4=insufficient space
#include "tests/TfCardTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <unistd.h>

namespace ft {
namespace {

// ---- TF card device constants ----
constexpr const char* TF_PARTITION   = "/dev/mmcblk1p1";
constexpr const char* TF_MOUNT_POINT = "/mnt/sdcard";
constexpr const char* TF_TEST_FILE   = "/mnt/sdcard/test_file";
constexpr int MIN_AVAILABLE_MB       = 300;
constexpr float MIN_WRITE_SPEED_MB   = 10.0f;

// ---- Helpers ----

std::string exec_cmd(const std::string& cmd) {
    char buf[1024];
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buf, sizeof(buf), pipe))
        out += buf;
    pclose(pipe);
    return out;
}

// Parse dd output: "... 31.6MB/s" (BusyBox) or "... 42.7 MB/s" (GNU)
float parse_dd_speed(const std::string& output) {
    // Try both formats: "MB/s" with or without leading space
    for (const char* pat : {"MB/s", "kB/s"}) {
        size_t pos = output.rfind(pat);
        if (pos == std::string::npos) continue;

        // Walk back to find start of number (skip optional space)
        size_t start = pos;
        while (start > 0 && output[start-1] == ' ') start--;
        while (start > 0 && (isdigit(output[start-1]) || output[start-1] == '.'))
            start--;

        float val = strtof(output.c_str() + start, nullptr);
        if (val > 0) {
            return (pat[0] == 'k') ? val / 1000.0f : val;
        }
    }
    return -1.0f;
}

// ---- TF card check / mount ----

bool partition_exists() {
    struct stat st;
    return stat(TF_PARTITION, &st) == 0;
}

bool is_mounted() {
    struct stat st;
    return stat(TF_MOUNT_POINT, &st) == 0;
}

bool mount_tf() {
    if (is_mounted()) return true;
    if (!partition_exists()) return false;
    int ret = mount(TF_PARTITION, TF_MOUNT_POINT, "exfat", 0, nullptr);
    if (ret != 0) {
        ret = mount(TF_PARTITION, TF_MOUNT_POINT, "vfat", 0, nullptr);
    }
    return ret == 0;
}

// ---- dd-based write speed benchmark ----

struct DdResult {
    bool  ok    = false;
    float speed_mbs = 0.0f;   // write speed in MB/s
};

DdResult run_dd_test(const char* test_file, int test_mb) {
    DdResult r{};

    // dd with fsync to flush after each block (BusyBox compatible)
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "dd if=/dev/zero of=%s bs=1M count=%d conv=fsync 2>&1",
             test_file, test_mb);

    std::string output = exec_cmd(cmd);
    std::fprintf(stderr, "[14R] dd output:\n%s\n", output.c_str());

    float speed = parse_dd_speed(output);
    if (speed > 0) {
        r.ok = true;
        r.speed_mbs = speed;
        std::fprintf(stderr, "[14R] write speed = %.1f MB/s\n", speed);
    } else {
        std::fprintf(stderr, "[14R] failed to parse dd speed\n");
    }

    remove(test_file);
    return r;
}

} // anonymous namespace

void register_tf_tests(TestEngine& engine) {

    // 14R: TF card write speed test — uses dd (always present on Linux)
    engine.registerRaw("14R", [&engine](const uint8_t* data, size_t len) {
        ProtoHeader hdr;
        if (!hdr.parse(data, len)) return;
        save_sn(hdr.sn);

        uint32_t err = 0;

        // 1. Check partition exists
        if (!partition_exists()) {
            std::fprintf(stderr, "[14R] TF partition %s not found\n", TF_PARTITION);
            err = 0x0002;
            goto respond;
        }

        // 2. Mount
        if (!mount_tf()) {
            std::fprintf(stderr, "[14R] mount %s → %s failed\n", TF_PARTITION, TF_MOUNT_POINT);
            err = 0x0004;
            goto respond;
        }

        // 3. Check available space
        {
            struct statvfs vfs;
            if (statvfs(TF_MOUNT_POINT, &vfs) != 0) {
                std::fprintf(stderr, "[14R] statvfs %s failed\n", TF_MOUNT_POINT);
                err = 0x0008;
                goto respond;
            }

            int64_t avail_mb = (int64_t(vfs.f_bavail) * int64_t(vfs.f_frsize)) >> 20;
            std::fprintf(stderr, "[14R] available space = %lld MB\n", (long long)avail_mb);

            if (avail_mb < MIN_AVAILABLE_MB) {
                std::fprintf(stderr, "[14R] insufficient space: %lld MB < %d MB\n",
                             (long long)avail_mb, MIN_AVAILABLE_MB);
                err = 0x0010;
                goto respond;
            }

            // 4. Run dd write test (256 MiB — fast but reliable)
            int test_mb = 256;
            if (avail_mb < 512) test_mb = avail_mb / 2;   // use half of available on small cards

            std::fprintf(stderr, "[14R] running dd write test, %d MiB\n", test_mb);

            DdResult result = run_dd_test(TF_TEST_FILE, test_mb);

            if (!result.ok || result.speed_mbs < MIN_WRITE_SPEED_MB) {
                std::fprintf(stderr, "[14R] write speed %.1f MB/s < %.0f MB/s threshold\n",
                             result.speed_mbs, MIN_WRITE_SPEED_MB);
                err = 0x0001;
            }
        }

    respond:
        std::string resp = hdr.build_response(err);
        engine.publish("14A", resp, 2);
        std::fprintf(stderr, "[14R] done, err=0x%08X\n", err);

    }, /*async=*/true);
}

} // namespace ft
