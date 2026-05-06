#include "hal/MotorHal.h"
#include "log.h"
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>

static motor_hal_context_t g_motor_hal_ctx = {
    .fd = -1,
    .is_initialized = false,
    .device_path = {0}
};

#define CYCLES_CONVERT_PHASE 1024

static int angles_to_phases(struct chx_enable *chx, float angle)
{
    double cycles = 0;
    float integer_part;
    double fractional_part;
    double phase_cnt;

    if (!(chx->direct == FORWARD || chx->direct == BACK))
    {
        log_err("not set motor direct, please set direct before START_CHAN\n");
        return -1;
    }

    cycles = angle * 512.0 / 360.0;
    fractional_part = modff(cycles, &integer_part);

    chx->cycles = (int)integer_part;
    phase_cnt = fractional_part * 1024.0f;
    chx->phase = (int)phase_cnt;

    int actual_subdivide;
    switch (chx->subdivide) {
    case SUBDIVIDE16:  actual_subdivide = 16;  break;
    case SUBDIVIDE32:  actual_subdivide = 32;  break;
    case SUBDIVIDE64:  actual_subdivide = 64;  break;
    case SUBDIVIDE128: actual_subdivide = 128; break;
    default:           actual_subdivide = 128; break;
    }

    int phase_align_base = 256 / actual_subdivide;
    int aligned_phase = ALIGN_UP(chx->phase, phase_align_base);

    if (aligned_phase >= CYCLES_CONVERT_PHASE)
    {
        chx->cycles++;
        aligned_phase -= CYCLES_CONVERT_PHASE;
    }

    chx->phase = aligned_phase;
    return 0;
}

static double phases_to_angles(u64 phases)
{
    return (double)(phases * 360.0 / 524288.0);
}

int motor_hal_init(const char *device_path)
{
    if (!device_path) { log_err("Invalid device_path parameter\n"); return -1; }
    if (g_motor_hal_ctx.is_initialized) { log_warn("Motor HAL already initialized\n"); return 0; }

    g_motor_hal_ctx.fd = open(device_path, O_RDWR);
    if (g_motor_hal_ctx.fd < 0) {
        log_err("Failed to open device %s: errno=%d (%s)\n", device_path, errno, strerror(errno));
        return -1;
    }

    strncpy(g_motor_hal_ctx.device_path, device_path, sizeof(g_motor_hal_ctx.device_path) - 1);
    g_motor_hal_ctx.is_initialized = true;
    log_info("Motor HAL initialized: device=%s, fd=%d\n", device_path, g_motor_hal_ctx.fd);
    return 0;
}

void motor_hal_deinit(void)
{
    if (!g_motor_hal_ctx.is_initialized) return;
    if (g_motor_hal_ctx.fd >= 0) {
        close(g_motor_hal_ctx.fd);
        g_motor_hal_ctx.fd = -1;
    }
    g_motor_hal_ctx.is_initialized = false;
    memset(g_motor_hal_ctx.device_path, 0, sizeof(g_motor_hal_ctx.device_path));
    log_info("Motor HAL deinitialized\n");
}

bool motor_hal_is_initialized(void) { return g_motor_hal_ctx.is_initialized; }

int motor_hal_get_position(struct get_angle *angle, float *out_angle)
{
    if (!g_motor_hal_ctx.is_initialized || !angle || !out_angle) return -1;
    int ret = ioctl(g_motor_hal_ctx.fd, GET_CYCLE_CNT, angle);
    if (ret < 0) { log_err("GET_CYCLE_CNT failed: errno=%d\n", errno); return -1; }
    *out_angle = (float)phases_to_angles(angle->phase_done_toltal);
    return 0;
}

int motor_hal_start(enum channel_select ch, float angle, enum motor_speed_gear speed, enum direct_select direct)
{
    if (!g_motor_hal_ctx.is_initialized) return -1;

    struct chx_enable params = {0};
    params.chx = ch;
    params.speed = speed;
    params.direct = direct;
    params.subdivide = SUBDIVIDE128;

    int ret = angles_to_phases(&params, fabsf(angle));
    if (ret < 0) return -1;

    log_info("motor_hal_start: ch=%d, angle=%.2f, cycles=%d, phase=%d\n", ch, angle, params.cycles, params.phase);
    ret = ioctl(g_motor_hal_ctx.fd, CHAN_START, &params);
    if (ret < 0) { log_err("CHAN_START failed: errno=%d, channel=%d\n", errno, ch); return -1; }
    return 0;
}

int motor_hal_stop(struct chx_enable *params)
{
    if (!g_motor_hal_ctx.is_initialized || !params) return -1;
    int ret = ioctl(g_motor_hal_ctx.fd, CHAN_STOP, params);
    if (ret < 0) { log_err("CHAN_STOP failed: errno=%d\n", errno); return -1; }
    return 0;
}

int motor_hal_params_update(enum channel_select ch, float angle, enum motor_speed_gear speed, enum direct_select direct)
{
    if (!g_motor_hal_ctx.is_initialized) return -1;

    struct chx_speed speed_params = {0};
    speed_params.chx = ch;
    speed_params.speed = speed;
    int ret = ioctl(g_motor_hal_ctx.fd, SET_SPEED, &speed_params);
    if (ret < 0) return -1;

    struct chx_enable params = {0};
    params.chx = ch;
    params.speed = speed;
    params.direct = direct;
    params.subdivide = SUBDIVIDE128;

    ret = angles_to_phases(&params, fabsf(angle));
    if (ret < 0) return -1;

    ret = ioctl(g_motor_hal_ctx.fd, CHANGE_WORKING_PARAM, &params);
    if (ret < 0) { log_err("CHANGE_WORKING_PARAM failed: errno=%d\n", errno); return -1; }
    return 0;
}

int motor_hal_set_center(enum channel_select channel)
{
    if (!g_motor_hal_ctx.is_initialized) return -1;
    int ret = ioctl(g_motor_hal_ctx.fd, SET_CENTER, &channel);
    if (ret < 0) { log_err("SET_CENTER failed: errno=%d\n", errno); return -1; }
    return 0;
}

int motor_hal_clear_cnt(enum channel_select channel)
{
    if (!g_motor_hal_ctx.is_initialized) return -1;
    int ret = ioctl(g_motor_hal_ctx.fd, CLR_CYCLE_CNT, &channel);
    if (ret < 0) { log_err("CLR_CYCLE_CNT failed: errno=%d\n", errno); return -1; }
    return 0;
}
