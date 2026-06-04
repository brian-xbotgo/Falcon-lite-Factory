#include "platforms/common/interface/IPlatform.h"
#include "platforms/common/interface/IEncoder.h"
#include "platforms/common/interface/ICameraDriver.h"
#include "platforms/common/interface/IBatteryDriver.h"
#include "platforms/common/interface/IMotorDriver.h"
#include "platforms/common/interface/IHallDriver.h"
#include "platforms/common/interface/IDisplayDriver.h"
#include "platforms/common/interface/IGpioDriver.h"
#include "platforms/common/interface/IRecorder.h"
#include "platforms/common/interface/IWifiManager.h"

namespace ft {

class NullPlatform : public IPlatform {
public:
    bool init(const nlohmann::json&) override { return true; }
    const char* name() const override { return "null"; }
    void registerDrivers(DriverRegistry&) override {}
};

std::unique_ptr<IPlatform> createNullPlatform() {
    return std::make_unique<NullPlatform>();
}

static bool _reg = registerPlatformFactory("null", createNullPlatform);

} // namespace ft
