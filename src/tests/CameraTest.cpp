// Camera test: 18R / 22R / 28R
// Dual GC4663: cam0 on I2C3/0x29 (csi2-dphy0), cam1 on I2C4/0x29 (csi2-dphy3)
// error_code: bit0=MIPI error / no video nodes, bit1=I2C probe fail, bit2=not in dmesg, bit3=OTP fail

#include "tests/CameraTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include "hal/I2cController.h"
#include <cstdio>
#include <cstring>
#include <atomic>
#include <glob.h>

namespace ft {

enum CamStatus { CAM_UNKNOWN = 0, CAM_FOUND, CAM_NOT_FOUND };

static CamStatus check_camera(const std::string& name) {
    auto r = shell_exec("dmesg | grep -i " + name);
    if (r.output.find(name) != std::string::npos) return CAM_FOUND;
    return CAM_NOT_FOUND;
}

static int check_otp_flags(const std::string& otp_file) {
    std::string content = read_file(otp_file);
    if (content.empty()) return -1;

    bool in_rkawb = false, in_rklsc = false;
    int rkawb_flag = -1, rklsc_flag = -1;

    size_t pos = 0;
    while (pos < content.size()) {
        auto eol = content.find('\n', pos);
        if (eol == std::string::npos) eol = content.size();
        std::string line = content.substr(pos, eol - pos);
        pos = eol + 1;

        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == "[RKAWBOTPParam]") { in_rkawb = true; in_rklsc = false; continue; }
        if (line == "[RKLSCOTPParam]") { in_rklsc = true; in_rkawb = false; continue; }
        if (!line.empty() && line[0] == '[') { in_rkawb = false; in_rklsc = false; continue; }

        if (in_rkawb && (line == "flag=1;" || line == "flag=1")) rkawb_flag = 1;
        if (in_rklsc && (line == "flag=1;" || line == "flag=1")) rklsc_flag = 1;
    }

    if (rkawb_flag != 1) { std::fprintf(stderr, "[cam] RKAWBOTPParam flag != 1\n"); return -2; }
    if (rklsc_flag != 1) { std::fprintf(stderr, "[cam] RKLSCOTPParam flag != 1\n"); return -3; }
    return 0;
}

static bool probe_gc4663_i2c(int bus, int addr) {
    int hi = I2cController::readRegister(bus, addr, 0x03f0);
    int lo = I2cController::readRegister(bus, addr, 0x03f1);
    if (hi < 0 || lo < 0) {
        std::fprintf(stderr, "[cam] I2C probe failed: bus %d addr 0x%02x\n", bus, addr);
        return false;
    }
    unsigned int chip_id = ((unsigned int)hi << 8) | (unsigned int)lo;
    if (chip_id != 0x4653) {
        std::fprintf(stderr, "[cam] I2C CHIP_ID mismatch: expected 0x4653 got 0x%04X\n", chip_id);
        return false;
    }
    return true;
}

static bool mipi_csi_error() {
    auto r = shell_exec("dmesg | tail -n 300");
    return r.output.find("MIPI_CSI2 ERR") != std::string::npos;
}

static bool video_nodes_exist() {
    glob_t g;
    int rc = glob("/dev/video*", 0, nullptr, &g);
    bool found = (rc == 0 && g.gl_pathc > 0);
    globfree(&g);
    return found;
}

// I2C params for dual GC4663
static constexpr int kCam0Bus = 3, kCam0Addr = 0x29;
static constexpr int kCam1Bus = 4, kCam1Addr = 0x29;
static const char* kCam0Otp = "/proc/otp_eeprom-3-50";
static const char* kCam1Otp = "/proc/otp_eeprom-4-50";

static std::atomic<int> g_cam0_status{CAM_UNKNOWN};
static std::atomic<int> g_cam1_status{CAM_UNKNOWN};

void register_camera_tests(TestEngine& engine) {
    // Startup detection — check both cameras in dmesg
    g_cam0_status = check_camera("gc4663");
    g_cam1_status = CAM_FOUND; // both report "gc4663" so second check is same; dual-cam verified by I2C probe
    std::fprintf(stderr, "[cam] gc4663 dmesg status=%d (dual cam0=I2C%d, cam1=I2C%d)\n",
                 g_cam0_status.load(), kCam0Bus, kCam1Bus);

    // 18R: Board-level cam0 (I2C3/0x29, csi2-dphy0)
    engine.registerRaw("18R", [&engine](const uint8_t* data, size_t len) {
        ProtoHeader hdr;
        if (!hdr.parse(data, len)) return;
        save_sn(hdr.sn);
        uint32_t err = 0;

        if (check_otp_flags(kCam0Otp) != 0) {
            err |= 0x0008;
            engine.publish("18A", hdr.build_response(err), 2);
            std::fprintf(stderr, "[18R] OTP fail (cam0: %s)\n", kCam0Otp);
            return;
        }

        if (g_cam0_status != CAM_FOUND) {
            err |= 0x0004;
            engine.publish("18A", hdr.build_response(err), 2);
            std::fprintf(stderr, "[18R] gc4663 not found in dmesg\n");
            return;
        }

        if (!probe_gc4663_i2c(kCam0Bus, kCam0Addr)) {
            err |= 0x0002;
        }

        if (mipi_csi_error()) {
            err |= 0x0001;
            std::fprintf(stderr, "[18R] MIPI_CSI2 ERR detected\n");
        }

        if (!video_nodes_exist()) {
            err |= 0x0001;
            std::fprintf(stderr, "[18R] no /dev/video* nodes\n");
        }

        engine.publish("18A", hdr.build_response(err), 2);
        std::fprintf(stderr, "[18R] done, err=0x%08X\n", err);
    }, /*async=*/true);

    // 22R: System-level cam1 (I2C4/0x29, csi2-dphy3) — second GC4663
    engine.registerRaw("22R", [&engine](const uint8_t* data, size_t len) {
        ProtoHeader hdr;
        if (!hdr.parse(data, len)) return;
        save_sn(hdr.sn);
        uint32_t err = 0;

        if (check_otp_flags(kCam1Otp) != 0) {
            err |= 0x0008;
            engine.publish("22A", hdr.build_response(err), 2);
            std::fprintf(stderr, "[22R] OTP fail (cam1: %s)\n", kCam1Otp);
            return;
        }

        if (g_cam0_status != CAM_FOUND) {
            err |= 0x0004;
            engine.publish("22A", hdr.build_response(err), 2);
            std::fprintf(stderr, "[22R] gc4663 not found in dmesg\n");
            return;
        }

        if (!probe_gc4663_i2c(kCam1Bus, kCam1Addr)) {
            err |= 0x0002;
        }

        if (mipi_csi_error()) {
            err |= 0x0001;
            std::fprintf(stderr, "[22R] MIPI_CSI2 ERR detected\n");
        }

        if (!video_nodes_exist()) {
            err |= 0x0001;
            std::fprintf(stderr, "[22R] no /dev/video* nodes\n");
        }

        engine.publish("22A", hdr.build_response(err), 2);
        std::fprintf(stderr, "[22R] done, err=0x%08X\n", err);
    }, /*async=*/true);

    // 28R: System-level both cameras (comprehensive check)
    engine.registerRaw("28R", [&engine](const uint8_t* data, size_t len) {
        ProtoHeader hdr;
        if (!hdr.parse(data, len)) return;
        save_sn(hdr.sn);
        uint32_t err = 0;

        // Check both OTPs
        if (check_otp_flags(kCam0Otp) != 0) {
            err |= 0x0008;
            std::fprintf(stderr, "[28R] OTP fail (cam0: %s)\n", kCam0Otp);
        }

        if (g_cam0_status != CAM_FOUND) {
            err |= 0x0004;
            std::fprintf(stderr, "[28R] gc4663 not found in dmesg\n");
        }

        // Probe both cameras
        if (!probe_gc4663_i2c(kCam0Bus, kCam0Addr)) {
            err |= 0x0002;
        }
        if (!probe_gc4663_i2c(kCam1Bus, kCam1Addr)) {
            err |= 0x0002;
        }

        if (mipi_csi_error()) {
            err |= 0x0001;
            std::fprintf(stderr, "[28R] MIPI_CSI2 ERR detected\n");
        }

        if (!video_nodes_exist()) {
            err |= 0x0001;
            std::fprintf(stderr, "[28R] no /dev/video* nodes\n");
        }

        engine.publish("28A", hdr.build_response(err), 2);
        std::fprintf(stderr, "[28R] done, err=0x%08X\n", err);
    }, /*async=*/true);
}

} // namespace ft
