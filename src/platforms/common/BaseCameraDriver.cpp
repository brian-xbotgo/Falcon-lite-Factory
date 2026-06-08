#include "platforms/common/interface/ICameraDriver.h"

namespace ft {

class BaseCameraDriver : public ICameraDriver {
public:
    CameraProbeResult probe(int) override { return {}; }
    bool checkOtp(int) override { return false; }
    uint32_t readChipId(int, int) override { return 0; }
    bool hasMipiError() override { return false; }
    bool videoNodesExist() override { return false; }
};

} // namespace ft
