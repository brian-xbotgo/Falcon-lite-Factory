#include "drivers/Gc4663CameraDriver.h"
#include "hal/I2cController.h"
#include "common/ShellUtils.h"
#include <cstdio>
#include <cstdlib>
#include <glob.h>

namespace ft {

// TODO(RK3576): 核对 I2C 总线号、MIPI CSI 接口号、OTP 路径是否与 RK3576 设备树一致
static constexpr int kCam0Bus = 3, kCam0Addr = 0x29;
static constexpr int kCam1Bus = 4, kCam1Addr = 0x29;
static const char* kCam0Otp = "/proc/otp_eeprom-3-50";
static const char* kCam1Otp = "/proc/otp_eeprom-4-50";

static int camBus(int cam_index) { return (cam_index == 0) ? kCam0Bus : kCam1Bus; }
static int camAddr(int cam_index) { return (cam_index == 0) ? kCam0Addr : kCam1Addr; }
static const char* camOtp(int cam_index) { return (cam_index == 0) ? kCam0Otp : kCam1Otp; }

bool Gc4663CameraDriver::checkDmesg(const std::string& name)
{
    std::string cmd = "dmesg | grep -i " + name;
    std::string r = shell_exec(cmd.c_str());
    return r.find(name) != std::string::npos;
}

int Gc4663CameraDriver::checkOtpFile(const std::string& path)
{
    std::string content = read_file(path);
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
    if (rkawb_flag != 1) return -2;
    if (rklsc_flag != 1) return -3;
    return 0;
}

CameraProbeResult Gc4663CameraDriver::probe(int cam_index)
{
    CameraProbeResult r = {};
    r.found = checkDmesg("gc4663");

    int bus = camBus(cam_index);
    int addr = camAddr(cam_index);
    uint32_t chip = readChipId(bus, addr);
    r.i2cOk = (chip == 0x4653);
    r.chipId = (r.i2cOk) ? "0x4653" : "0x" + std::to_string(chip);

    r.otpValid = (checkOtpFile(camOtp(cam_index)) == 0);
    r.mipiOk = !hasMipiError() && videoNodesExist();
    return r;
}

bool Gc4663CameraDriver::checkOtp(int cam_index)
{
    return checkOtpFile(camOtp(cam_index)) == 0;
}

uint32_t Gc4663CameraDriver::readChipId(int bus, int addr)
{
    int hi = I2cController::readRegister(bus, addr, 0x03f0);
    int lo = I2cController::readRegister(bus, addr, 0x03f1);
    if (hi < 0 || lo < 0) return 0xFFFFFFFF;
    return (static_cast<uint32_t>(hi) << 8) | static_cast<uint32_t>(lo);
}

bool Gc4663CameraDriver::hasMipiError()
{
    std::string r = shell_exec("dmesg | tail -n 300");
    return r.find("MIPI_CSI2 ERR") != std::string::npos;
}

bool Gc4663CameraDriver::videoNodesExist()
{
    glob_t g;
    int rc = glob("/dev/video*", 0, nullptr, &g);
    bool found = (rc == 0 && g.gl_pathc > 0);
    globfree(&g);
    return found;
}

} // namespace ft
