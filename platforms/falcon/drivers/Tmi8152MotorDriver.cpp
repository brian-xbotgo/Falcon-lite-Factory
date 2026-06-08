#include "drivers/Tmi8152MotorDriver.h"
#include "tmi8152/tmi8152.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

namespace ft {

static constexpr const char* MOTOR_DEV_PATH = "/dev/tmi8152";

static int toChannel(MotorDirection dir) {
    return (dir == MOTOR_HORIZONTAL) ? CH12 : CH34;
}

static int toMotorSpeedGear(MotorSpeed speed) {
    switch (speed) {
        case SPEED_LOW:  return MOTOR_SPEED_10_36_8;
        case SPEED_MID:  return MOTOR_SPEED_4_71_8;
        case SPEED_HIGH: return MOTOR_SPEED_0_111_0;
    }
    return MOTOR_SPEED_4_71_8;
}

static int toDirectSelect(MotorDirect direct) {
    return (direct == DIRECT_FORWARD) ? FORWARD : BACK;
}

static int toSubdivideSelect(int subdivide) {
    switch (subdivide) {
        case 16:  return SUBDIVIDE16;
        case 32:  return SUBDIVIDE32;
        case 64:  return SUBDIVIDE64;
        case 128: return SUBDIVIDE128;
    }
    return SUBDIVIDE128;
}

static int anglesToPhases(struct chx_enable* chx, float angleDeg)
{
    double cycles = std::fabs(angleDeg) * 512.0 / 360.0;
    float intPart;
    double fracPart = std::modf(cycles, &intPart);
    chx->cycles = static_cast<int>(intPart);
    int phase = static_cast<int>(fracPart * 1024.0);

    int actualSubdivide;
    switch (chx->subdivide) {
        case SUBDIVIDE16:  actualSubdivide = 16;  break;
        case SUBDIVIDE32:  actualSubdivide = 32;  break;
        case SUBDIVIDE64:  actualSubdivide = 64;  break;
        case SUBDIVIDE128: actualSubdivide = 128; break;
        default:           actualSubdivide = 128; break;
    }

    int phaseAlignBase = 256 / actualSubdivide;
    int alignedPhase = ((phase + phaseAlignBase - 1) / phaseAlignBase) * phaseAlignBase;
    if (alignedPhase >= CYCLES_CONVERT_PHASE) {
        chx->cycles++;
        alignedPhase -= CYCLES_CONVERT_PHASE;
    }
    chx->phase = alignedPhase;
    return 0;
}

static double phasesToAngles(unsigned long long phases)
{
    return static_cast<double>(phases) * 360.0 / 524288.0;
}

bool Tmi8152MotorDriver::init()
{
    if (initialized_) return true;
    fd_ = ::open(MOTOR_DEV_PATH, O_RDWR);
    if (fd_ < 0) {
        std::fprintf(stderr, "[Tmi8152] open %s failed: %s\n",
                     MOTOR_DEV_PATH, std::strerror(errno));
        return false;
    }
    initialized_ = true;
    return true;
}

void Tmi8152MotorDriver::deinit()
{
    if (!initialized_) return;
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    initialized_ = false;
}

bool Tmi8152MotorDriver::move(MotorDirection dir, float angleDeg,
                              MotorSpeed speed, MotorDirect direct)
{
    if (!initialized_ || fd_ < 0) return false;

    struct chx_enable params = {};
    params.chx       = static_cast<channel_select>(toChannel(dir));
    params.speed     = static_cast<motor_speed_gear>(toMotorSpeedGear(speed));
    params.direct    = static_cast<direct_select>(toDirectSelect(direct));
    params.subdivide = SUBDIVIDE128;

    if (anglesToPhases(&params, angleDeg) < 0) return false;

    if (::ioctl(fd_, CHAN_START, &params) < 0) {
        std::fprintf(stderr, "[Tmi8152] CHAN_START failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

bool Tmi8152MotorDriver::startBoardTest(MotorDirection dir, unsigned int cycles, int subdivide,
                                        MotorSpeed speed, MotorDirect direct)
{
    if (!initialized_) return false;

    (void)subdivide;

    struct chx_mode mode = {};
    mode.chx  = static_cast<channel_select>(toChannel(dir));
    mode.mode = AOTU_CTRL;
    ::ioctl(fd_, SET_MODE, &mode);

    struct chx_enable config = {};
    config.chx       = static_cast<channel_select>(toChannel(dir));
    config.subdivide = SUBDIVIDE128;
    config.direct    = static_cast<direct_select>(toDirectSelect(direct));
    config.phase     = 0;
    config.cycles    = static_cast<int>(cycles);
    config.speed     = static_cast<motor_speed_gear>(toMotorSpeedGear(speed));

    if (::ioctl(fd_, CHAN_START, &config) < 0) {
        std::fprintf(stderr, "[Tmi8152] CHAN_START failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

void Tmi8152MotorDriver::stop(MotorDirection dir)
{
    if (!initialized_ || fd_ < 0) return;
    auto channel = static_cast<channel_select>(toChannel(dir));
    if (::ioctl(fd_, CHAN_STOP, &channel) < 0) {
        std::fprintf(stderr, "[Tmi8152] CHAN_STOP failed: %s\n", std::strerror(errno));
    }
}

float Tmi8152MotorDriver::getPosition(MotorDirection dir)
{
    if (!initialized_ || fd_ < 0) return 0.0f;
    struct get_angle angle = {};
    angle.chx = static_cast<channel_select>(toChannel(dir));
    if (::ioctl(fd_, GET_CYCLE_CNT, &angle) < 0) {
        std::fprintf(stderr, "[Tmi8152] GET_CYCLE_CNT failed: %s\n", std::strerror(errno));
        return 0.0f;
    }
    return static_cast<float>(phasesToAngles(angle.phase_done_toltal));
}

} // namespace ft
