#pragma once
#include "platforms/common/interface/ICameraDriver.h"
#include <string>

namespace ft {

class Gc4663CameraDriver : public ICameraDriver {
public:
    CameraProbeResult probe(int cam_index) override;
    bool checkOtp(int cam_index) override;
    uint32_t readChipId(int bus, int addr) override;
    bool hasMipiError() override;
    bool videoNodesExist() override;

private:
    static bool checkDmesg(const std::string& name);
    static int  checkOtpFile(const std::string& path);
};

} // namespace ft
