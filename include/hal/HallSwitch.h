#ifndef HALL_SWITCH_H
#define HALL_SWITCH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HALL_UART_DEVICE "/dev/ttyS9"

#define ADS_UART_SYNC_BYTE   0x55
#define ADS_CMD_START        0x08
#define ADS_CMD_STOP         0x0A
#define ADS_CMD_RDATA        0x10
#define ADS_CMD_RREG         0x20
#define ADS_CMD_WREG         0x40
#define ADS_CMD_RESET        0x06

#define ADS_UART_BAUDRATE    9600

#define ADS122U04_REG_CONFIG0   0x00
#define ADS122U04_REG_CONFIG1   0x01
#define ADS122U04_REG_CONFIG2   0x02
#define ADS122U04_REG_CONFIG3   0x03
#define ADS122U04_REG_CONFIG4   0x04

#define ADS122U04_CONFIG0_MUX_SHIFT          4
#define ADS122U04_CONFIG0_MUX_MASK           (0x0F << ADS122U04_CONFIG0_MUX_SHIFT)
#define ADS122U04_CONFIG0_GAIN_SHIFT         1
#define ADS122U04_CONFIG0_GAIN_MASK          (0x07 << ADS122U04_CONFIG0_GAIN_SHIFT)

#define ADS122U04_CONFIG0_MUX_AIN3_AVSS       (0x0B << ADS122U04_CONFIG0_MUX_SHIFT)
#define ADS122U04_CONFIG0_GAIN_1              (0x00 << ADS122U04_CONFIG0_GAIN_SHIFT)
#define ADS122U04_CONFIG0_PGA_BYPASS_ENABLED  (0x00)
#define ADS122U04_CONFIG0_PGA_BYPASS_DISABLED (0x01)

#define ADS122U04_CONFIG1_DR_SHIFT           5
#define ADS122U04_CONFIG1_DR_MASK            (0x07 << ADS122U04_CONFIG1_DR_SHIFT)
#define ADS122U04_CONFIG1_DR_175SPS          (0x03 << ADS122U04_CONFIG1_DR_SHIFT)
#define ADS122U04_CONFIG1_DR_45SPS           (0x01 << ADS122U04_CONFIG1_DR_SHIFT)
#define ADS122U04_CONFIG1_MODE_MASK          (0x01 << 4)
#define ADS122U04_CONFIG1_CM_MASK            (0x01 << 3)
#define ADS122U04_CONFIG1_VREF_SHIFT         1
#define ADS122U04_CONFIG1_VREF_MASK          (0x03 << ADS122U04_CONFIG1_VREF_SHIFT)
#define ADS122U04_CONFIG1_VREF_INTERNAL      (0x00 << ADS122U04_CONFIG1_VREF_SHIFT)
#define ADS122U04_CONFIG1_VREF_EXTERNAL      (0x01 << ADS122U04_CONFIG1_VREF_SHIFT)
#define ADS122U04_CONFIG1_VREF_AVDD_AVSS     (0x02 << ADS122U04_CONFIG1_VREF_SHIFT)
#define ADS122U04_CONFIG1_VREF_SUPPLY        (0x03 << ADS122U04_CONFIG1_VREF_SHIFT)
#define ADS122U04_CONFIG1_TS_MASK            (0x01)

#define ADS122U04_CONFIG2_DRDY_MASK          (0x01 << 7)
#define ADS122U04_CONFIG2_DCNT_MASK          (0x01 << 6)
#define ADS122U04_CONFIG2_CRC_SHIFT          4
#define ADS122U04_CONFIG2_CRC_MASK           (0x03 << ADS122U04_CONFIG2_CRC_SHIFT)
#define ADS122U04_CONFIG2_CRC_DISABLED       (0x00 << ADS122U04_CONFIG2_CRC_SHIFT)
#define ADS122U04_CONFIG2_CRC_INVERTED       (0x01 << ADS122U04_CONFIG2_CRC_SHIFT)
#define ADS122U04_CONFIG2_CRC_CRC16          (0x02 << ADS122U04_CONFIG2_CRC_SHIFT)
#define ADS122U04_CONFIG2_BCS_MASK           (0x01 << 3)
#define ADS122U04_CONFIG2_IDAC_SHIFT         0
#define ADS122U04_CONFIG2_IDAC_MASK          (0x07 << ADS122U04_CONFIG2_IDAC_SHIFT)

#define ADS122U04_CONFIG3_I1MUX_SHIFT        5
#define ADS122U04_CONFIG3_I1MUX_MASK         (0x07 << ADS122U04_CONFIG3_I1MUX_SHIFT)
#define ADS122U04_CONFIG3_I2MUX_SHIFT        2
#define ADS122U04_CONFIG3_I2MUX_MASK         (0x07 << ADS122U04_CONFIG3_I2MUX_SHIFT)
#define ADS122U04_CONFIG3_AUTO_MASK          (0x01)

#define ADS122U04_CONFIG4_GPIO2DIR_MASK      (0x01 << 6)
#define ADS122U04_CONFIG4_GPIO1DIR_MASK      (0x01 << 5)
#define ADS122U04_CONFIG4_GPIO0DIR_MASK      (0x01 << 4)
#define ADS122U04_CONFIG4_GPIO2SEL_MASK      (0x01 << 3)
#define ADS122U04_CONFIG4_GPIO2DAT_MASK      (0x01 << 2)
#define ADS122U04_CONFIG4_GPIO1DAT_MASK      (0x01 << 1)
#define ADS122U04_CONFIG4_GPIO0DAT_MASK      (0x01 << 0)

int hall_switch_init(const char *uart_device);
void hall_switch_deinit(void);
int hall_uart_init(const char *uart_device);
int hall_uart_sendrecv(const uint8_t *tx_buf, size_t tx_len, uint8_t *rx_buf, size_t rx_len, int timeout_ms);
int hall_reg_write(uint8_t reg_addr, uint8_t value);
int hall_reg_read(uint8_t reg_addr, uint8_t *value);
float hall_voltage_get(void);
bool hall_switch_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif
