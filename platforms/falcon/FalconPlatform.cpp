#include "platforms/common/interface/IPlatform.h"
#include "core/DriverRegistry.h"
#include "platforms/common/SysfsGpioDriver.h"
#include "drivers/RkMppEncoder.h"
#include "drivers/Gc4663CameraDriver.h"
#include "drivers/Cw221xBatteryDriver.h"
#include "drivers/Tmi8152MotorDriver.h"
#include "drivers/HallSwitchDriver.h"
#include "drivers/LvglDisplayDriver.h"
#include "drivers/Rk3576Recorder.h"
#include "drivers/Rk3576WifiManager.h"

namespace ft {

class FalconPlatform : public IPlatform {
public:
    bool init(const nlohmann::json& config) override {
        config_ = config;
        return true;
    }
    const char* name() const override { return "falcon"; }
    void registerDrivers(DriverRegistry& reg) override;

    const char* gpuTestPath() const    { return "/oem/usr/bin/gpu_test"; }
    const char* npuTestPath() const    { return "/oem/usr/bin/npu_test"; }
    const char* stressTestPath() const { return "/oem/usr/bin/stress_test"; }
    const char* emmcTestPath() const   { return "/oem/usr/bin/emmc_test"; }

private:
    nlohmann::json config_;
};

void FalconPlatform::registerDrivers(DriverRegistry& reg) {
    reg.bind<IEncoder>      ([] { return std::make_unique<RkMppEncoder>(); });
    reg.bind<ICameraDriver> ([] { return std::make_unique<Gc4663CameraDriver>(); });
    reg.bind<IBatteryDriver>([] { return std::make_unique<Cw221xBatteryDriver>(); });
    reg.bind<IMotorDriver>  ([] { return std::make_unique<Tmi8152MotorDriver>(); });
    reg.bind<IHallDriver>   ([] { return std::make_unique<HallSwitchDriver>(); });
    reg.bind<IDisplayDriver>([] { return std::make_unique<LvglDisplayDriver>(); });
    reg.bind<IGpioDriver>   ([] { return std::make_unique<SysfsGpioDriver>(); });
    reg.bind<IRecorder>     ([] { return std::make_unique<Rk3576Recorder>(); });
    reg.bind<IWifiManager>  ([] { return std::make_unique<Rk3576WifiManager>(); });
}

std::unique_ptr<IPlatform> createFalconPlatform() {
    return std::make_unique<FalconPlatform>();
}

static bool _reg_falcon = registerPlatformFactory("falcon", createFalconPlatform);

} // namespace ft
