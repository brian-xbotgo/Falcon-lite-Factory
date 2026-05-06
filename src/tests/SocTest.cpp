// Soc test: 12R
// GPU / DDR / CPU / NPU / eMMC comprehensive test
// error_code: bit0=GPU, bit1=DDR, bit2=CPU, bit3=NPU, bit4=eMMC

#include "tests/SocTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include <cstdio>
#include <cstdlib>

namespace ft {

void register_soc_tests(TestEngine& engine) {
    engine.registerTest("12R", [](const ProtoHeader& hdr) -> uint32_t {
        uint32_t err = 0;

        std::system("mkdir -p /userdata/logs");

        // ---- GPU ----
        std::fprintf(stderr, "[12R] GPU test start\n");
        std::system("echo userspace > /sys/class/devfreq/27800000.gpu/governor");
        std::system("echo 900000000 > /sys/class/devfreq/27800000.gpu/userspace/set_freq");

        if (!general_test("gpu_test",
                          "/oem/usr/bin/gpu_test 5120 1000 > /userdata/logs/gpu_test.log 2>&1 &",
                          "/sys/class/devfreq/27800000.gpu/load",
                          "100@",
                          15)) {
            err |= 0x0001;
            std::fprintf(stderr, "[12R] GPU FAIL\n");
        } else {
            std::fprintf(stderr, "[12R] GPU PASS\n");
        }

        // ---- DDR ----
        std::fprintf(stderr, "[12R] DDR test start\n");
        std::system("echo userspace > /sys/class/devfreq/dmc/governor");
        std::system("echo 2112000000 > /sys/class/devfreq/dmc/userspace/set_freq");

        if (!general_test("stressapptest",
                          "stressapptest -s 5 -M 2048 > /userdata/logs/ddr_test.log 2>&1 &",
                          "/userdata/logs/ddr_test.log",
                          "Status: PASS",
                          30)) {
            err |= 0x0002;
            std::fprintf(stderr, "[12R] DDR FAIL\n");
        } else {
            std::fprintf(stderr, "[12R] DDR PASS\n");
        }

        // ---- CPU ----
        std::fprintf(stderr, "[12R] CPU test start\n");
        std::system("echo userspace > /sys/class/devfreq/dmc/governor");
        std::system("echo performance > /sys/devices/system/cpu/cpu5/cpufreq/scaling_governor");

        if (!general_test("stress",
                          "taskset -c 0-7 /oem/usr/bin/stress --cpu 8 --timeout 5 > /userdata/logs/cpu_test.log 2>&1 &",
                          "/userdata/logs/cpu_test.log",
                          "successful",
                          15)) {
            err |= 0x0004;
            std::fprintf(stderr, "[12R] CPU FAIL\n");
        } else {
            std::fprintf(stderr, "[12R] CPU PASS\n");
        }

        // ---- NPU ----
        std::fprintf(stderr, "[12R] NPU test start\n");
        if (!general_test("rknn_matmul_api_demo",
                          "taskset -c 1 /oem/usr/bin/rknn_matmul_api_demo 2 4096,4096,4096 0 0 10000000 > /dev/null 2>&1 & "
                          "taskset -c 2 /oem/usr/bin/rknn_matmul_api_demo 2 4096,4096,4096 0 0 10000000 > /dev/null 2>&1 & "
                          "taskset -c 3 /oem/usr/bin/rknn_matmul_api_demo 2 4096,4096,4096 0 0 10000000 > /dev/null 2>&1 & "
                          "taskset -c 4 /oem/usr/bin/rknn_matmul_api_demo 2 4096,4096,4096 0 0 10000000 > /dev/null 2>&1 &",
                          "/sys/kernel/debug/rknpu/load",
                          "Core0: 99%, Core1: 99%",
                          20)) {
            err |= 0x0008;
            std::fprintf(stderr, "[12R] NPU FAIL\n");
        } else {
            std::fprintf(stderr, "[12R] NPU PASS\n");
        }

        // ---- eMMC ----
        std::fprintf(stderr, "[12R] eMMC test start\n");
        if (!general_test("rwcheck",
                          "/oem/usr/bin/rwcheck -j 2 -t 1 -d /userdata -p 1 > /userdata/logs/emmc_test.log 2>&1 &",
                          "/userdata/logs/emmc_test.log",
                          "rwcheck pass",
                          30)) {
            err |= 0x0010;
            std::fprintf(stderr, "[12R] eMMC FAIL\n");
        } else {
            std::fprintf(stderr, "[12R] eMMC PASS\n");
        }

        std::fprintf(stderr, "[12R] Soc test complete, sn=%s, err=0x%08X\n", hdr.sn, err);
        return err;
    }, /*async=*/true);
}

} // namespace ft
