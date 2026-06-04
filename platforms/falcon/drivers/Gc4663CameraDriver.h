#pragma once
#include "platforms/common/interface/ICameraDriver.h"

namespace ft {

class Gc4663CameraDriver : public ICameraDriver {
public:
    CameraProbeResult probe(int) override {
        return CameraProbeResult{true, true, true, true, "GC4663"};
    }
    bool checkOtp(int) override { return true; }
    uint32_t readChipId(int, int) override { return 0x4663; }
    bool hasMipiError() override { return false; }
    bool videoNodesExist() override { return true; }
};

} // namespace ft
