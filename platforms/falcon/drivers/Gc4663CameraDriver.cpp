#include "drivers/Gc4663CameraDriver.h"
#include "control/I2cController.h"
#include "common/ShellUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <glob.h>
#include <sstream>

namespace ft {

namespace {

struct FalconCameraInfo {
    const char* sensor;
    int bus;
    int addr;
    const char* otp;
    uint32_t chipId;
    int chipIdReg;
    bool idLowByteFirst;
};

constexpr FalconCameraInfo kCameras[] = {
    {"gc4663", 4, 0x29, "/proc/otp_eeprom-4-50", 0x4653, 0x03f0, false},
    {"imx678", 5, 0x1a, "/proc/otp_eeprom-5-50", 0x0884, 0x3046, true},
};

const FalconCameraInfo& cameraInfo(int camIndex)
{
    constexpr int cameraCount = static_cast<int>(sizeof(kCameras) / sizeof(kCameras[0]));
    if (camIndex < 0 || camIndex >= cameraCount) {
        return kCameras[0];
    }
    return kCameras[camIndex];
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string hexString(uint32_t value)
{
    std::ostringstream os;
    os << "0x" << std::hex << value;
    return os.str();
}

uint32_t read16BitSensorId(int bus, int addr, int reg, bool lowByteFirst)
{
    int first = I2cController::readRegister(bus, addr, reg);
    int second = I2cController::readRegister(bus, addr, reg + 1);
    if (first < 0 || second < 0) {
        return 0xFFFFFFFF;
    }
    first &= 0xff;
    second &= 0xff;
    if (lowByteFirst) {
        return (static_cast<uint32_t>(second) << 8) | static_cast<uint32_t>(first);
    }
    return (static_cast<uint32_t>(first) << 8) | static_cast<uint32_t>(second);
}

} // namespace

bool Gc4663CameraDriver::checkDmesg(const std::string& name)
{
    std::string cmd = "dmesg | grep -i " + name;
    std::string r = shell_exec(cmd.c_str());
    return toLower(r).find(toLower(name)) != std::string::npos;
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
    const auto& cam = cameraInfo(cam_index);
    CameraProbeResult r = {};
    r.found = checkDmesg(cam.sensor);

    uint32_t chip = read16BitSensorId(cam.bus, cam.addr, cam.chipIdReg, cam.idLowByteFirst);
    r.i2cOk = (chip == cam.chipId);
    r.chipId = hexString(chip);

    r.otpValid = (checkOtpFile(cam.otp) == 0);
    r.mipiOk = !hasMipiError() && videoNodesExist();
    return r;
}

bool Gc4663CameraDriver::checkOtp(int cam_index)
{
    return checkOtpFile(cameraInfo(cam_index).otp) == 0;
}

uint32_t Gc4663CameraDriver::readChipId(int bus, int addr)
{
    return read16BitSensorId(bus, addr, 0x03f0, false);
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
