/**
 * @file hall_switch.c
 * @brief 线性霍尔开关接口层实现
 */

#include "hal/HallSwitch.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <time.h>


static const float ADS122U04_VREF_VOLTS = 3.3f;

// 霍尔开关上下文
typedef struct
{
    int uart_fd;
    bool is_initialized;
    bool crc_enabled;
    bool auto_enabled;
    bool dcnt_enabled;
    uint8_t last_dcnt;
    int last_rdata_len;
    uint8_t last_rdata_dcnt;
    pthread_mutex_t lock;
    pthread_t   uart_thread;
} hall_switch_context_t;

// 全局上下文
static hall_switch_context_t g_hall_ctx = {
    .uart_fd = -1,
    .is_initialized = false,
    .crc_enabled = false,
    .auto_enabled = false,
    .dcnt_enabled = false,
    .last_dcnt = 0,
    .last_rdata_len = 0,
    .last_rdata_dcnt = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

// static int __attribute__((unused)) uart_set_custom_baudrate(int fd, int baud)
// {
//     struct termios2 tio;
//     if (ioctl(fd, TCGETS2, &tio) != 0)
//         return -1;

//     tio.c_cflag &= ~CBAUD;
//     tio.c_cflag |= BOTHER;
//     tio.c_ispeed = (speed_t)baud;
//     tio.c_ospeed = (speed_t)baud;

//     if (ioctl(fd, TCSETS2, &tio) != 0)

//         return -1;
//     return 0;
// }

 static uint8_t rev_byte(uint8_t v)
 {
     uint8_t r = 0;
     for (int i = 0; i < 8; i++)
     {
         r = (uint8_t)((r << 1) | (v & 0x01));
         v >>= 1;
     }
     return r;
 }

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t b = rev_byte(data[i]);
        crc ^= (uint16_t)b << 8;
        for (int b = 0; b < 8; b++)
        {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int ads_send_cmd(uint8_t cmd)
{
    uint8_t tx_buf[2] = {ADS_UART_SYNC_BYTE, cmd};
    // uint8_t tx_buf[2] = {cmd};
    pthread_mutex_lock(&g_hall_ctx.lock);
    ssize_t sent = write(g_hall_ctx.uart_fd, tx_buf, sizeof(tx_buf));
    pthread_mutex_unlock(&g_hall_ctx.lock);

    if (sent != (ssize_t)sizeof(tx_buf))
        return -1;
    return 0;
}

// static int hall_wait_drdy(uint64_t deadline)
// {
//     uint8_t last_c2 = 0;
//     while (monotonic_ms() < deadline)
//     {
//         uint8_t c2 = 0;
//         if (hall_reg_read(ADS122U04_REG_CONFIG2, &c2) == 0)
//         {
//             last_c2 = c2;
//             if ((c2 & ADS122U04_CONFIG2_DRDY_MASK) != 0)
//                 return 0;
//         }
//         usleep(1000);
//     }
//     log_info("Check DRDY timeout, last CONFIG2=0x%02X\n", last_c2);
//     return -1;
// }

// static int hall_rdata_read_raw(uint8_t *out_b0, uint8_t *out_b1, uint8_t *out_b2)
// {
//     if (!out_b0 || !out_b1 || !out_b2)
//         return -1;

//     uint8_t rx_buf[8] = {0};
//     size_t payload_len = (g_hall_ctx.dcnt_enabled ? 1 : 0) + 3;
//     size_t expect = payload_len + (g_hall_ctx.crc_enabled ? 2 : 0);

//     // UART模式下RDATA始终返回3字节ADC数据，不受DCNT/CRC配置影响

//     uint64_t deadline = monotonic_ms() + 800ULL;
//     int ret = 0;
//     while (monotonic_ms() < deadline)
//     {
//         // 兼容单次转换模式：每次读取前都触发一次START，再通过DRDY轮询等待新数据
//         (void)ads_send_cmd(ADS_CMD_START);

//         // // 如果DRDY在UART模式下不翻转，这里不能卡死；超时则降级为直接尝试RDATA读取
//         // (void)hall_wait_drdy(deadline);
//         // tcflush(g_hall_ctx.uart_fd, TCIFLUSH);

//         uint8_t tx_buf[2] = {ADS_UART_SYNC_BYTE, ADS_CMD_RDATA};
//         ret = hall_uart_sendrecv(tx_buf, sizeof(tx_buf), rx_buf, expect, 400);
//         g_hall_ctx.last_rdata_len = ret;
        
//         if (ret < (int)expect)
//         {
//             // 兼容：软件期望带DCNT(4字节)但设备实际只回3字节（或相反）
//             if (!g_hall_ctx.crc_enabled)
//             {
//                 if (g_hall_ctx.dcnt_enabled && ret == 3)
//                 {
//                     // 当作无DCNT帧解析
//                     g_hall_ctx.last_rdata_dcnt = 0xFF;
//                     *out_b0 = rx_buf[0];
//                     *out_b1 = rx_buf[1];
//                     *out_b2 = rx_buf[2];
//                     return 0;
//                 }
//                 if (!g_hall_ctx.dcnt_enabled && ret == 4)
//                 {
//                     // 当作带DCNT帧解析
//                     uint8_t dcnt = rx_buf[0];
//                     g_hall_ctx.last_dcnt = dcnt;
//                     g_hall_ctx.last_rdata_dcnt = dcnt;
//                     *out_b0 = rx_buf[1];
//                     *out_b1 = rx_buf[2];
//                     *out_b2 = rx_buf[3];
//                     return 0;
//                 }
//             }

//             if (ret == 0)
//             {
//                 (void)ads_send_cmd(ADS_CMD_START);
//                 tcflush(g_hall_ctx.uart_fd, TCIFLUSH);
//                 usleep(5000);
//             }
//             usleep(1000);
//             continue;
//         }

//         if (g_hall_ctx.crc_enabled)
//         {
//             uint16_t got = (uint16_t)((uint16_t)rx_buf[payload_len] << 8) | rx_buf[payload_len + 1];
//             uint16_t cal = crc16_ccitt(rx_buf, payload_len);
//             if (got != cal)
//             {
//                 usleep(1000);
//                 continue;
//             }
//         }

//         if (g_hall_ctx.dcnt_enabled)
//         {
//             uint8_t dcnt = rx_buf[0];
//             if (dcnt == g_hall_ctx.last_dcnt)
//             {
//                 usleep(1000);
//                 continue;
//             }
//             g_hall_ctx.last_dcnt = dcnt;
//             g_hall_ctx.last_rdata_dcnt = dcnt;
//         }
//         else
//         {
//             g_hall_ctx.last_rdata_dcnt = 0xFF;
//         }

//         size_t data_off = g_hall_ctx.dcnt_enabled ? 1 : 0;
//         *out_b0 = rx_buf[data_off + 0];
//         *out_b1 = rx_buf[data_off + 1];
//         *out_b2 = rx_buf[data_off + 2];
//         return 0;
//     }

//     return -1;
// }

// static int test_crc16(void)
// {
//     // CRC16-CCITT测试用例（多项式0x1021，初始值0xFFFF，按UART位序对每字节bit反转）
//     const uint8_t test_data1[] = {0x12, 0x34, 0x56};
//     const uint8_t test_data2[] = {0x00, 0x00, 0x00};
//     const uint8_t test_data3[] = {0xFF, 0xFF, 0xFF};
    
//     // 已知的CRC期望值（CRC16-CCITT, 初始值0x0000）
//     const uint16_t expected_crc1 = 0xF6F7;  // [12 34 56]
//     const uint16_t expected_crc2 = 0xCC9C;  // [00 00 00]
//     const uint16_t expected_crc3 = 0x1EF0;  // [FF FF FF]
    
//     uint16_t crc1 = crc16_ccitt(test_data1, sizeof(test_data1));
//     uint16_t crc2 = crc16_ccitt(test_data2, sizeof(test_data2));
//     uint16_t crc3 = crc16_ccitt(test_data3, sizeof(test_data3));
    
//     log_info("CRC16 self-test results:\n");
//     log_info("  Test1 [12 34 56]: CRC=0x%04X (expected 0x%04X)\n", crc1, expected_crc1);
//     log_info("  Test2 [00 00 00]: CRC=0x%04X (expected 0x%04X)\n", crc2, expected_crc2);
//     log_info("  Test3 [FF FF FF]: CRC=0x%04X (expected 0x%04X)\n", crc3, expected_crc3);
    
//     // 验证所有测试用例
//     int failed = 0;
    
//     if (crc1 != expected_crc1)
//     {
//         log_err("CRC16 Test1 FAILED: expected 0x%04X, got 0x%04X\n", expected_crc1, crc1);
//         failed++;
//     }
    
//     if (crc2 != expected_crc2)
//     {
//         log_err("CRC16 Test2 FAILED: expected 0x%04X, got 0x%04X\n", expected_crc2, crc2);
//         failed++;
//     }
    
//     if (crc3 != expected_crc3)
//     {
//         log_err("CRC16 Test3 FAILED: expected 0x%04X, got 0x%04X\n", expected_crc3, crc3);
//         failed++;
//     }
    
//     if (failed > 0)
//     {
//         log_err("CRC16 self-test FAILED: %d/%d tests failed\n", failed, 3);
//         return -1;
//     }
    
//     log_info("CRC16 self-test PASSED (all 3 tests)\n");
//     return 0;
// }


int hall_uart_init(const char *uart_device)
{
    if (!uart_device)
    {
        log_err("UART device path is NULL\n");
        return -1;
    }

    // 打开串口设备
    int fd = open(uart_device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0)
    {
        log_err("Failed to open UART device %s: %s\n", uart_device, strerror(errno));
        return -1;
    }

    // 配置串口参数
    struct termios options;
    if (tcgetattr(fd, &options) < 0)
    {
        log_err("Failed to get UART attributes: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    // 使用标准波特率配置（优先保证通信稳定性）
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    // 8N1模式：8位数据位，无奇偶校验，1位停止位
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    // 禁用硬件流控
    options.c_cflag &= ~CRTSCTS;

    // 启用接收器，设置本地模式
    options.c_cflag |= (CLOCAL | CREAD);

    // 原始模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    // 设置超时和最小字符数
    options.c_cc[VTIME] = 10; // 1秒超时
    options.c_cc[VMIN] = 0;

    // 应用配置
    if (tcsetattr(fd, TCSANOW, &options) < 0)
    {
        log_err("Failed to set UART attributes: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    // // 如需非标准波特率，可启用termios2自定义设置；9600使用标准termios即可
    // if (ADS_UART_BAUDRATE != 9600)
    // {
    //     int baud_ret = uart_set_custom_baudrate(fd, ADS_UART_BAUDRATE);
    //     if (baud_ret == 0)
    //     {
    //         log_info("UART baudrate set to %d (custom)\n", ADS_UART_BAUDRATE);
    //     }
    //     else
    //     {
    //         log_warn("UART custom baudrate %d not supported, keep termios setting\n", ADS_UART_BAUDRATE);
    //     }
    // }

    // 清空缓冲区
    tcflush(fd, TCIOFLUSH);

    g_hall_ctx.uart_fd = fd;
    log_info("UART initialized: %s (fd=%d)\n", uart_device, fd);

    return 0;
}

int hall_uart_sendrecv(const uint8_t *tx_buf, size_t tx_len,
                       uint8_t *rx_buf, size_t rx_len, int timeout_ms)
{
    if ((!tx_buf && tx_len != 0) || !rx_buf || g_hall_ctx.uart_fd < 0)
    {
        log_err("Invalid parameters for UART sendrecv\n");
        return -1;
    }

    pthread_mutex_lock(&g_hall_ctx.lock);

    if (tx_len > 0)
    {
        // 发送数据
        ssize_t written = write(g_hall_ctx.uart_fd, tx_buf, tx_len);
        if (written != (ssize_t)tx_len)
        {
            log_err("UART write failed: written=%zd, expected=%zu\n", written, tx_len);
            pthread_mutex_unlock(&g_hall_ctx.lock);
            return -1;
        }
    }

    // 循环接收直到满足期望长度或超时
    size_t total = 0;
    uint64_t deadline = monotonic_ms() + (uint64_t)timeout_ms;
    while (total < rx_len)
    {
        uint64_t now = monotonic_ms();
        if (now >= deadline)
            break;

        int remain_ms = (int)(deadline - now);

        fd_set readfds;
        struct timeval timeout;
        timeout.tv_sec = remain_ms / 1000;
        timeout.tv_usec = (remain_ms % 1000) * 1000;

        FD_ZERO(&readfds);
        FD_SET(g_hall_ctx.uart_fd, &readfds);

        int ret = select(g_hall_ctx.uart_fd + 1, &readfds, NULL, NULL, &timeout);
        if (ret < 0)
        {
            log_err("UART select failed: %s\n", strerror(errno));
            pthread_mutex_unlock(&g_hall_ctx.lock);
            return -1;
        }
        if (ret == 0)
            continue;

        ssize_t received = read(g_hall_ctx.uart_fd, rx_buf + total, rx_len - total);
        if (received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            log_err("UART read failed: %s\n", strerror(errno));
            pthread_mutex_unlock(&g_hall_ctx.lock);
            return -1;
        }
        if (received == 0)
            continue;

        // // 详细日志：打印每次接收到的字节数和内容
        // log_info("UART read: offset=%zu received=%zd remain_ms=%d bytes=", total, received, remain_ms);
        // for (ssize_t i = 0; i < received; i++)
        // {
        //     log_info("0x%02X ", rx_buf[total + i]);
        // }
        // log_info("\n");

        total += (size_t)received;
    }

    pthread_mutex_unlock(&g_hall_ctx.lock);

    if (total == 0)
        return 0;

    // // 打印最终收到的完整数据
    // log_info("UART recv complete: total=%zu expect=%zu data=", total, rx_len);
    // for (size_t i = 0; i < total; i++)
    // {
    //     log_info("0x%02X ", rx_buf[i]);
    // }
    // log_info("\n");

    return (int)total;
}

int hall_reg_write(uint8_t reg_addr, uint8_t value)
{
    // ADS122U04寄存器写命令格式（根据数据手册图61）
    // 主机TX(芯片RX): [0x55] [0100 rrrx] [dddd dddd]
    // 主机RX(芯片TX): 无响应
    uint8_t tx_buf[3];
    tx_buf[0] = ADS_UART_SYNC_BYTE;                    // 0x55 同步字节
    tx_buf[1] = ADS_CMD_WREG | ((reg_addr << 1) & 0x0E);      // 0x40 | reg_addr
    tx_buf[2] = value;
    log_info("REG Write addr %d \n", tx_buf[1]);

    // AUTO模式下不建议在设备推送数据时发送命令；这里先停止转换避免数据流冲突
    bool need_resume = false;
    if (g_hall_ctx.auto_enabled)
    {
        need_resume = true;
        (void)ads_send_cmd(ADS_CMD_STOP);
        tcflush(g_hall_ctx.uart_fd, TCIFLUSH);
    }

    tcflush(g_hall_ctx.uart_fd, TCIFLUSH);

    // WREG命令无响应，只发送不接收
    pthread_mutex_lock(&g_hall_ctx.lock);
    ssize_t sent = write(g_hall_ctx.uart_fd, tx_buf, sizeof(tx_buf));
    pthread_mutex_unlock(&g_hall_ctx.lock);
    
    if (sent != sizeof(tx_buf))
    {
        log_err("Failed to write register 0x%02X: sent=%zd\n", reg_addr, sent);
        return -1;
    }

    // 确保串口发送完成，避免后续flush/读操作影响尚未发出的字节
    (void)tcdrain(g_hall_ctx.uart_fd);

    log_debug("Register write: addr=0x%02X, value=0x%02X\n", reg_addr, value);

    if (need_resume)

    {
        (void)ads_send_cmd(ADS_CMD_START);
        tcflush(g_hall_ctx.uart_fd, TCIFLUSH);
    }
    return 0;
}

int hall_reg_read(uint8_t reg_addr, uint8_t *value)
{
    if (!value)
    {
        log_err("Invalid parameter for register read\n");
        return -1;
    }

    // ADS122U04寄存器读命令格式（根据数据手册图60）
    // 主机TX(芯片RX): [0x55] [0010 rrrx]
    // 主机RX(芯片TX): [REG DATA] (如果CONFIG2启用CRC，则为 [DATA][CRC_H][CRC_L])
    uint8_t tx_buf[2];
    tx_buf[0] = ADS_UART_SYNC_BYTE;                             // 0x55 同步字节
    tx_buf[1] = ADS_CMD_RREG | ((reg_addr << 1)& 0x0E);         // 0x20 | reg_addr
    log_info("REG READ addr %d \n", tx_buf[1]);

    // AUTO模式下不建议在设备推送数据时发送命令；这里先停止转换避免数据流冲突
    bool need_resume = false;
    if (g_hall_ctx.auto_enabled)
    {
        need_resume = true;
        (void)ads_send_cmd(ADS_CMD_STOP);
        tcflush(g_hall_ctx.uart_fd, TCIFLUSH);
    }

    tcflush(g_hall_ctx.uart_fd, TCIFLUSH);

    // RREG命令响应不受CRC设置影响，始终只返回1字节寄存器值
    uint8_t rx_buf[3] = {0};
    size_t expect = 1;
    int ret = hall_uart_sendrecv(tx_buf, sizeof(tx_buf), rx_buf, expect, 100);
    if (ret < 0)
    {
        log_err("Failed to read register 0x%02X\n", reg_addr);
        return -1;
    }

    // 严格要求收满期望字节数，避免把rx_buf默认0当成有效读回
    if (ret != (int)expect)
    {
        log_err("Register read short/timeout: addr=0x%02X ret=%d expect=%zu\n", reg_addr, ret, expect);
        return -1;
    }

    // CRC校验（如果启用）
    if (g_hall_ctx.crc_enabled && ret >= 3)
    {
        uint16_t got = (uint16_t)((uint16_t)rx_buf[1] << 8) | rx_buf[2];
        uint16_t cal = crc16_ccitt(&rx_buf[0], 1);
        if (got != cal)
        {
            log_err("Register CRC mismatch: addr=0x%02X got=0x%04X cal=0x%04X\n", reg_addr, got, cal);
            return -1;
        }
    }

    *value = rx_buf[0];
    log_debug("Register read: addr=0x%02X, value=0x%02X\n", reg_addr, *value);

    if (need_resume)
    {
        (void)ads_send_cmd(ADS_CMD_START);
        tcflush(g_hall_ctx.uart_fd, TCIFLUSH);
    }

    return 0;
}

float hall_voltage_get(void)
{
    // 手动读取模式：发送RDATA获取最新转换结果
    // 启用DCNT时，数据前会多1字节计数器；启用CRC16时，数据后会多2字节CRC
    uint8_t rx_buf[8] = {0};
    size_t payload_len = (g_hall_ctx.dcnt_enabled ? 1 : 0) + 3;
    size_t expect = payload_len + (g_hall_ctx.crc_enabled ? 2 : 0);
    int ret = 0;

    // 超时500ms
    uint64_t deadline = monotonic_ms() + 500ULL;
    // log_info("hall_voltage_get: expect=%zu, payload_len=%zu, crc_enabled=%d\n", expect, payload_len, g_hall_ctx.crc_enabled);
    while (monotonic_ms() < deadline)
    {
        // 清空RX避免历史残留字节导致错位
        tcflush(g_hall_ctx.uart_fd, TCIFLUSH);

        (void)ads_send_cmd(ADS_CMD_START);
        // // 判断寄存器2的DRDY准备好后再下发数据 （100ms）
        // uint64_t drdy_deadline = monotonic_ms() + 100ULL;
        // if (drdy_deadline > deadline)
        //     drdy_deadline = deadline;
        // while (monotonic_ms() < drdy_deadline)
        // {
        //     uint8_t c2 = 0;
        //     if (hall_reg_read(ADS122U04_REG_CONFIG2, &c2) == 0)
        //     {
        //         if ((c2 & ADS122U04_CONFIG2_DRDY_MASK) != 0)
        //         {
        //             log_info("DRDY OK \n");
        //             break;
        //         }
                    
        //     }
        //     usleep(1000);
        // }

        uint8_t tx_buf[2] = {ADS_UART_SYNC_BYTE, ADS_CMD_RDATA};
        ret = hall_uart_sendrecv(tx_buf, sizeof(tx_buf), rx_buf, expect, 400);
        if (ret < (int)expect)
        {
            // 如果完全收不到数据，尝试重发START唤醒连续转换
            if (ret == 0)
            {
                log_info("hall_voltage_get: no data, retry start\n");
                (void)ads_send_cmd(ADS_CMD_START);
                tcflush(g_hall_ctx.uart_fd, TCIFLUSH);
                usleep(5000);
            }
            usleep(1000);
            continue;
        }

        if (g_hall_ctx.crc_enabled)
        {
            uint16_t got = (uint16_t)((uint16_t)rx_buf[payload_len] << 8) | rx_buf[payload_len + 1];
            uint16_t cal = crc16_ccitt(rx_buf, payload_len);
            if (got != cal)
            {
                log_err("ADC CRC mismatch: got=0x%04X cal=0x%04X\n", got, cal);
                usleep(1000);
                continue;
            }
        }

        if (g_hall_ctx.dcnt_enabled)
        {
            uint8_t dcnt = rx_buf[0];
            if (dcnt == g_hall_ctx.last_dcnt)
            {
                // 计数未变化，可能读到了旧样本，继续尝试
                usleep(1000);
                continue;
            }
            g_hall_ctx.last_dcnt = dcnt;
        }

        break;
    }

    if (ret < (int)expect)
    {
        log_err("Failed to read ADC data, ret=%d, expect=%zu\n", ret, expect);
        return -1.0f;
    }

    size_t data_off = g_hall_ctx.dcnt_enabled ? 1 : 0;

    // 将24位ADC数据转换为电压值（UART输出按LSB, MID, MSB）
    int32_t adc_raw = ((int32_t)rx_buf[data_off + 2] << 16) | ((int32_t)rx_buf[data_off + 1] << 8) | rx_buf[data_off + 0];
    
    // 处理符号位（24位有符号数）
    if (adc_raw & 0x800000)
    {
        adc_raw |= 0xFF000000;
    }

    // 转换为电压（假设参考电压为3.3f，增益为1）
    // 电压 = (24位ADC值 × VREF) / (增益 × 2^23) 数据手册35页
    float voltage = (float)adc_raw * ADS122U04_VREF_VOLTS / 8388608.0f; // 2^23 = 8388608

    int dcnt = (g_hall_ctx.last_rdata_dcnt == 0xFF) ? -1 : (int)g_hall_ctx.last_rdata_dcnt;
    log_debug("Hall voltage: %.4fV (rdata_len=%d dcnt=%d, rbuf0=0x%02X, rbuf1=0x%02X, rbuf2=0x%02X, raw=%d)\n",
             voltage, g_hall_ctx.last_rdata_len, dcnt, rx_buf[0], rx_buf[1], rx_buf[2], adc_raw);

    return voltage;
}

int hall_switch_init(const char *uart_device)
{
    int ret;

    if (g_hall_ctx.is_initialized)
    {
        log_warn("Hall switch already initialized\n");
        return 0;
    }

    // // CRC自测试
    // ret = test_crc16();
    // if (ret < 0)
    // {
    //     log_err("CRC16 self-test failed\n");
    //     close(g_hall_ctx.uart_fd);
    //     g_hall_ctx.uart_fd = -1;
    //     return -1;
    // }
    // log_info("CRC16 self-test passed\n");

    // 初始化UART
    ret = hall_uart_init(uart_device);
    if (ret < 0)
    {
        log_err("Failed to initialize UART\n");
        return -1;
    }

    (void)ads_send_cmd(ADS_CMD_RESET);
    usleep(100000);

    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data msgset;

    // 复位芯片
    uint8_t reset_cmd = HALL_RESET_REG;
    msg.addr = HALL_DEV_ADDR;
    msg.flags = 0;
    msg.len = 1;
    msg.buf = &reset_cmd;

    msgset.msgs = &msg;
    msgset.nmsgs = 1;

    int retry;
    for (retry = 0; retry < 3; retry++) {
        if (ioctl(fd, I2C_RDWR, &msgset) >= 0)
            break;
        if (retry < 2) {
            log_warn("I2C_RDWR write 0x%02x failed (retry %d): %s\n",
                     reset_cmd, retry + 1, strerror(errno));
            usleep(100000);
        }
    }
    if (retry >= 3) {
        log_err("I2C_RDWR write 0x%02x failed after 3 retries: %s\n",
                reset_cmd, strerror(errno));
        close(fd);
        g_hall_ctx.fd = -1;
        return -1;
    }
    log_info("reset hall chip success.\n");
    usleep(200000);

    // 空闲模式
    uint8_t exit_cmd = HALL_EX_MODE_CMD_REG;

    // CONFIG1: VREF[1:0]=11（按需求） + CM=1（连续转换，配合AUTO输出）
    uint8_t config1 = (uint8_t)(ADS122U04_CONFIG1_DR_45SPS | ADS122U04_CONFIG1_VREF_AVDD_AVSS | ADS122U04_CONFIG1_CM_MASK);
    // uint8_t config1 = (uint8_t)(ADS122U04_CONFIG1_VREF_AVDD_AVSS);
    ret = hall_reg_write(ADS122U04_REG_CONFIG1, config1);
    if (ret < 0)
    {
        log_err("Failed to configure ADS122U04 CONFIG1\n");
        close(g_hall_ctx.uart_fd);
        g_hall_ctx.uart_fd = -1;
        return -1;
    }
    
    // 读回验证CONFIG1
    uint8_t read_config1 = 0;
    ret = hall_reg_read(ADS122U04_REG_CONFIG1, &read_config1);
    if (ret < 0 || read_config1 != config1)
    {
        log_err("CONFIG1 verify failed: wrote=0x%02X, read=0x%02X\n", config1, read_config1);
        close(g_hall_ctx.uart_fd);
        g_hall_ctx.uart_fd = -1;
        return -1;
    }
    log_info("CONFIG1 verified: 0x%02X\n", read_config1);

    // CONFIG2: 实测证明在UART模式下DCNT和CRC都不输出，使用纯3字节模式
    uint8_t config2 = ADS122U04_CONFIG2_CRC_DISABLED;
    ret = hall_reg_write(ADS122U04_REG_CONFIG2, config2);
    if (ret < 0)
    {
        log_err("Failed to configure ADS122U04 CONFIG2\n");
        close(g_hall_ctx.uart_fd);
        g_hall_ctx.uart_fd = -1;
        return -1;
    }
    tcflush(g_hall_ctx.uart_fd, TCIFLUSH);

    usleep(2000);

    for (retry = 0; retry < 3; retry++) {
        if (ioctl(fd, I2C_RDWR, &msgset) >= 0)
            break;
        if (retry < 2) {
            log_warn("I2C_RDWR write 0x%02x failed (retry %d): %s\n",
                     exit_cmd, retry + 1, strerror(errno));
            usleep(100000);
        }
    }
    if (retry >= 3) {
        log_err("I2C_RDWR write 0x%02x failed after 3 retries: %s\n",
                exit_cmd, strerror(errno));
        close(fd);
        g_hall_ctx.fd = -1;
        return -1;
    }
    log_info("set hall chip enter idle mode success.\n");
    usleep(500000);

    // CONFIG3: AUTO=0（先关AUTO，配置完毕后再开）
    // uint8_t config3 = ADS122U04_CONFIG3_AUTO_MASK;
    uint8_t config3 = 0;
    ret = hall_reg_write(ADS122U04_REG_CONFIG3, config3);
    if (ret < 0)
    {
        log_err("Failed to configure ADS122U04 CONFIG3\n");
        close(g_hall_ctx.uart_fd);
        g_hall_ctx.uart_fd = -1;
        return -1;
    }
    
    // 读回验证CONFIG3
    uint8_t read_config3 = 0;
    ret = hall_reg_read(ADS122U04_REG_CONFIG3, &read_config3);
    if (ret < 0 || read_config3 != config3)
    {
        log_err("CONFIG3 verify failed: wrote=0x%02X, read=0x%02X\n", config3, read_config3);
        close(g_hall_ctx.uart_fd);
        g_hall_ctx.uart_fd = -1;
        return -1;
    }
    log_info("CONFIG3 verified: 0x%02X\n", read_config3);

    for (retry = 0; retry < 3; retry++) {
        if (ioctl(fd, I2C_RDWR, &msgset) >= 0)
            break;
        if (retry < 2) {
            log_warn("I2C_RDWR write 0x%02x failed (retry %d): %s\n",
                     HALL_MODE_REG_ADDR, retry + 1, strerror(errno));
            usleep(100000);
        }
    }
    
    // 读回验证CONFIG4
    uint8_t read_config4 = 0;
    ret = hall_reg_read(ADS122U04_REG_CONFIG4, &read_config4);
    if (ret < 0 || ((read_config4 & 0x7F) != (config4 & 0x7F)))
    {
        log_err("CONFIG4 verify failed: wrote=0x%02X, read=0x%02X\n", config4, read_config4);
        close(g_hall_ctx.uart_fd);
        g_hall_ctx.uart_fd = -1;
        return -1;
    }
    log_info("set hall chip enter continue mode success.\n");
    usleep(200000);

    g_hall_ctx.is_initialized = true;
    

    log_info("Hall switch initialized successfully\n");

    return 0;
}

void hall_switch_deinit(void)
{
    if (!g_hall_ctx.is_initialized)
    {
        return;
    }

    if (g_hall_ctx.uart_fd >= 0)
    {
        close(g_hall_ctx.uart_fd);
        g_hall_ctx.uart_fd = -1;
    }

    g_hall_ctx.is_initialized = false;
    log_info("Hall switch deinitialized\n");
}

bool hall_switch_is_initialized(void)
{
    return g_hall_ctx.is_initialized;
}
