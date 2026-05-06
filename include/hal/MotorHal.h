#ifndef MOTOR_HAL_H
#define MOTOR_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "tmi8152.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BACKLASH_CALIBRATE_ANGLE 5

typedef struct {
    int fd;
    bool is_initialized;
    char device_path[256];
} motor_hal_context_t;

int motor_hal_init(const char *device_path);
void motor_hal_deinit(void);
bool motor_hal_is_initialized(void);

int motor_hal_get_position(struct get_angle *angle, float *out_angle);

int motor_hal_start(enum channel_select ch, float angle, enum motor_speed_gear speed, enum direct_select direct);
int motor_hal_params_update(enum channel_select ch, float angle, enum motor_speed_gear speed, enum direct_select direct);
int motor_hal_stop(struct chx_enable *params);

int motor_hal_set_center(enum channel_select channel);
int motor_hal_clear_cnt(enum channel_select channel);

#ifdef __cplusplus
}
#endif

#endif
