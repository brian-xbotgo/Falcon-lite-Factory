#ifndef HALL_SWITCH_H
#define HALL_SWITCH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* KTH3601 I2C 配置 */
#define HALL_I2C_BUS         "/dev/i2c-0"
#define HALL_DEV_ADDR        0x6A
#define HALL_EX_MODE_CMD_REG 0x80
#define HALL_MODE_REG_ADDR   0x18
#define HALL_READ_REG_ADDR   0x48
#define READ_DATA_LEN        4

int   hall_switch_init(void);
void  hall_switch_deinit(void);
float hall_value_get(void);
bool  hall_switch_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif
