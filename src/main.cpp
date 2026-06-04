#include "config/PlatformConfig.h"
#include "core/DriverRegistry.h"
#include "core/TestEngine.h"
#include "platforms/common/interface/IPlatform.h"
#include "platforms/common/interface/IDisplayDriver.h"
#include <cstdio>

int main() {
    auto& cfg = PlatformConfig::instance();
    if (!cfg.loadFromFile("/oem/usr/conf/platform.json")) {
        fprintf(stderr, "[main] failed to load platform.json\n");
        return -1;
    }

    auto platform = ft::createPlatform(cfg.platformName());
    if (!platform || !platform->init(cfg.raw())) {
        fprintf(stderr, "[main] platform init failed\n");
        return -1;
    }

    ft::DriverRegistry reg;
    platform->registerDrivers(reg);

    auto display = reg.create<IDisplayDriver>();
    if (display) display->init();

    ft::TestEngine engine(reg, cfg);
    if (!engine.loadTestConfig("/oem/usr/conf/tests.json")) {
        fprintf(stderr, "[main] failed to load tests.json\n");
        return -1;
    }

    engine.run();
    return 0;
}
