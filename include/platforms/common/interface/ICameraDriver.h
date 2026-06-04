#pragma once
#include <string>
#include <cstdint>

namespace ft {

struct CameraProbeResult {
    bool found = false;
    bool otpValid = false;
    bool i2cOk = false;
    bool mipiOk = false;
    std::string chipId;
};

class ICameraDriver {
public:
    virtual ~ICameraDriver() = default;
    virtual CameraProbeResult probe(int cam_index) = 0;
    virtual bool checkOtp(int cam_index) = 0;
    virtual uint32_t readChipId(int bus, int addr) = 0;
    virtual bool hasMipiError() = 0;
    virtual bool videoNodesExist() = 0;
};

} // namespace ft
