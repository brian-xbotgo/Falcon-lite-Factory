/**
 * @file hall_switch.c
 * @brief KTH3601 3D霍尔传感器 I2C 接口层
 */

#include "hal/HallSwitch.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <pthread.h>

typedef struct
{
    int fd;
    bool is_initialized;
    pthread_mutex_t lock;
} hall_switch_context_t;

static hall_switch_context_t g_hall_ctx = {
    .fd = -1,
    .is_initialized = false,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static unsigned char KTH3601CRCCalculation(unsigned char DataReadFrame[])
{
    static const unsigned char CRC_TABLE[256] = {
        0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
        0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
        0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
        0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
        0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
        0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
        0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
        0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
        0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
        0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
        0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
        0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
        0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
        0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
        0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
        0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
        0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
        0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
        0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
        0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
        0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
        0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
        0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
        0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
        0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
        0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
        0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
        0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
        0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
        0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
        0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
        0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
    };
    unsigned char CRC_RMData[8] = {0};
    CRC_RMData[6] = DataReadFrame[1];
    CRC_RMData[7] = DataReadFrame[2];
    unsigned char crcData = 0;
    for (int i = 0; i < 8; i++) {
        crcData = CRC_TABLE[crcData ^ CRC_RMData[i]];
    }
    return crcData ^ 0x55;
}

float hall_value_get(void)
{
    pthread_mutex_lock(&g_hall_ctx.lock);

    if (g_hall_ctx.fd < 0) {
        pthread_mutex_unlock(&g_hall_ctx.lock);
        log_err("Hall device fd is invalid\n");
        return 9999.0f;
    }

    uint8_t reg = HALL_READ_REG_ADDR;
    uint8_t data[READ_DATA_LEN];
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data msgset;

    msgs[0].addr  = HALL_DEV_ADDR;
    msgs[0].flags = 0;
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = HALL_DEV_ADDR;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = READ_DATA_LEN;
    msgs[1].buf   = data;

    msgset.msgs  = msgs;
    msgset.nmsgs = 2;

    int retry;
    for (retry = 0; retry < 3; retry++) {
        if (ioctl(g_hall_ctx.fd, I2C_RDWR, &msgset) >= 0)
            break;
        if (retry < 2)
            usleep(10000);
    }
    if (retry >= 3) {
        log_err("I2C_RDWR read 0x%02x failed after 3 retries: %s\n",
                HALL_READ_REG_ADDR, strerror(errno));
        pthread_mutex_unlock(&g_hall_ctx.lock);
        return 9999.0f;
    }

    pthread_mutex_unlock(&g_hall_ctx.lock);

    unsigned char crc = KTH3601CRCCalculation(data);
    if (crc == data[3]) {
        int16_t raw = (int16_t)((data[1] << 8) | data[2]);
        float val = (float)raw / 90.0f;
        log_info("hall raw=0x%02x 0x%02x 0x%02x 0x%02x, CRC ok, value=%.2f mT\n",
                 data[0], data[1], data[2], data[3], val);
        return val;
    } else {
        log_warn("hall raw=0x%02x 0x%02x 0x%02x 0x%02x, CRC failed\n",
                 data[0], data[1], data[2], data[3]);
        return 9999.0f;
    }
}

int hall_switch_init(void)
{
    if (g_hall_ctx.is_initialized) {
        log_warn("Hall switch already initialized\n");
        return 0;
    }

    int fd = open(HALL_I2C_BUS, O_RDWR);
    if (fd < 0) {
        log_err("open hall device %s failed\n", HALL_I2C_BUS);
        return -1;
    }

    g_hall_ctx.fd = fd;

    // 退出当前模式
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data msgset;
    uint8_t exit_cmd = HALL_EX_MODE_CMD_REG;

    msg.addr  = HALL_DEV_ADDR;
    msg.flags = 0;
    msg.len   = 1;
    msg.buf   = &exit_cmd;

    msgset.msgs  = &msg;
    msgset.nmsgs = 1;

    if (ioctl(fd, I2C_RDWR, &msgset) < 0) {
        log_err("I2C_RDWR write 0x%02x failed: %s\n", exit_cmd, strerror(errno));
        close(fd);
        g_hall_ctx.fd = -1;
        return -1;
    }

    usleep(500000);

    // 连续感应模式
    uint8_t mode_reg = HALL_MODE_REG_ADDR;
    msg.buf = &mode_reg;

    int retry;
    for (retry = 0; retry < 3; retry++) {
        if (ioctl(fd, I2C_RDWR, &msgset) >= 0)
            break;
        if (retry < 2) {
            log_warn("I2C_RDWR write 0x%02x failed (retry %d): %s\n",
                     HALL_MODE_REG_ADDR, retry + 1, strerror(errno));
            usleep(100000);
        }
    }
    if (retry >= 3) {
        log_err("I2C_RDWR write 0x%02x failed after 3 retries: %s\n",
                HALL_MODE_REG_ADDR, strerror(errno));
        close(fd);
        g_hall_ctx.fd = -1;
        return -1;
    }

    usleep(200000);

    g_hall_ctx.is_initialized = true;
    log_info("Hall switch initialized successfully\n");
    return 0;
}

void hall_switch_deinit(void)
{
    if (!g_hall_ctx.is_initialized)
        return;

    if (g_hall_ctx.fd >= 0) {
        close(g_hall_ctx.fd);
        g_hall_ctx.fd = -1;
    }

    g_hall_ctx.is_initialized = false;
    log_info("Hall switch deinitialized\n");
}

bool hall_switch_is_initialized(void)
{
    return g_hall_ctx.is_initialized;
}
