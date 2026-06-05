// motor_sdk/tmi8152.h
// 从 XbotGo-Falcon-Air-Embedded/sdk_patch/kernel_patch/motor_tmi8152/tmi8152.h 提取
// TMI8152 电机驱动芯片的硬件级类型、枚举、ioctl 定义
// 产测固件独立副本，不依赖正式固件头文件
#ifndef MOTOR_SDK_TMI8152_H
#define MOTOR_SDK_TMI8152_H

#include <unistd.h>
#include <math.h>
#include <stdbool.h>

#ifndef u8
#define u8 unsigned char
#endif

#ifndef u32
#define u32 unsigned int
#endif

#ifndef u64
#define u64 unsigned long
#endif

enum channel_select{
	CH12 = 0,
	CH34,
};

enum subdivide_select{
	SUBDIVIDE_NONE  = 0,
	SUBDIVIDE16  = 4,
	SUBDIVIDE32  = 5,
	SUBDIVIDE64  = 6,
	SUBDIVIDE128 = 7,
};

enum motor_speed_gear {
	MOTOR_SPEED_0_111_0 = 0,
	MOTOR_SPEED_1_94_3,
	MOTOR_SPEED_2_81_2,
	MOTOR_SPEED_3_76_1,
	MOTOR_SPEED_4_71_8,
	MOTOR_SPEED_5_58_1,
	MOTOR_SPEED_6_55_4,
	MOTOR_SPEED_7_48_8,
	MOTOR_SPEED_8_41_9,
	MOTOR_SPEED_9_39_1,
	MOTOR_SPEED_10_36_8,
	MOTOR_SPEED_11_33_7,
	MOTOR_SPEED_12_29_7,
	MOTOR_SPEED_13_28_2,
	MOTOR_SPEED_14_24_7,
	MOTOR_SPEED_15_21_3,
	MOTOR_SPEED_16_19_9,
	MOTOR_SPEED_17_18_7,
	MOTOR_SPEED_18_17_1,
	MOTOR_SPEED_19_14_9,
	MOTOR_SPEED_20_14_2,
	MOTOR_SPEED_21_12_5,
	MOTOR_SPEED_22_10_7,
	MOTOR_SPEED_23_10_0,
	MOTOR_SPEED_24_08_6,
	MOTOR_SPEED_25_07_5,
	MOTOR_SPEED_26_07_1,
	MOTOR_SPEED_27_06_2,
	MOTOR_SPEED_28_05_3,
	MOTOR_SPEED_29_05_0,
	MOTOR_SPEED_30_04_3,
	MOTOR_SPEED_31_03_7,
	MOTOR_SPEED_00,
	MOTOR_SPEED_MAX
};

enum direct_select{
	OUTPUT_OPEN_CHANNEL = 0,
	FORWARD = 5,
	BACK = 10,
	STOP = 0xf,
};

enum work_mode{
	OUTPUT = 0,
	RESERVER = 1,
	MODE1_1 = 2,
	MODE2_2 = 3,
	SPI_CTRL = 4,
	RESERVE = 5,
	AOTU_CTRL = 6,
	MANUAL_CTRL = 7,
};

struct chx_enable {
	enum channel_select chx;
	enum subdivide_select subdivide;
	enum direct_select direct;
	int phase;
	int cycles;
	enum motor_speed_gear speed;
	u8 prediv;
	u8 div;
	u8 pwmset;
};

struct chx_mode {
	enum channel_select chx;
	enum work_mode mode;
};

struct motor_status {
	u8 temp;
	u8 work_status;
};

struct backlash_config {
	enum channel_select chx;
	u32 compensation_phases;
	u32 compensation_angle;
	bool enabled;
};

#define SPIMIOC_BASE        'S'
#define CHAN_START          _IOW(SPIMIOC_BASE, 0, struct chx_enable*)
#define CHAN_STOP           _IOW(SPIMIOC_BASE, 1, enum channel_select*)
#define CHANGE_WORKING_PARAM _IOW(SPIMIOC_BASE, 2, struct chx_enable*)
#define SET_MODE            _IOW(SPIMIOC_BASE, 3, struct chx_mode*)
#define ENTER_RESET         _IO(SPIMIOC_BASE, 4)
#define EXIT_RESET          _IO(SPIMIOC_BASE, 5)
#define ENTER_STANBY        _IO(SPIMIOC_BASE, 6)
#define EXIT_STANBY         _IO(SPIMIOC_BASE, 7)
#define POWER_PINC          _IO(SPIMIOC_BASE, 8)
#define NPOWER_PINC         _IO(SPIMIOC_BASE, 9)
#define GET_STATUS          _IOR(SPIMIOC_BASE, 10, struct motor_status*)
#define DISABLE_CH12        _IO(SPIMIOC_BASE, 11)
#define DISABLE_CH34        _IO(SPIMIOC_BASE, 12)
#define CLOSE_CHIP          _IO(SPIMIOC_BASE, 13)
#define SET_SPEED           _IOW(SPIMIOC_BASE, 14, struct chx_speed*)
#define GET_CYCLE_CNT       _IOW(SPIMIOC_BASE, 15, enum channel_select*)
#define CLR_CYCLE_CNT       _IOW(SPIMIOC_BASE, 16, enum channel_select*)
#define GET_TARG_CNT        _IOW(SPIMIOC_BASE, 17, enum channel_select*)
#define SET_CENTER          _IOW(SPIMIOC_BASE, 18, enum channel_select*)
#define SET_BACKLASH_COMPENSATION  _IOW(SPIMIOC_BASE, 19, struct backlash_config*)
#define GET_BACKLASH_COMPENSATION  _IOWR(SPIMIOC_BASE, 20, struct backlash_config*)
#define READ_REGISTERS      _IOWR(SPIMIOC_BASE, 21, struct reg_read_request)
#define WRITE_REGISTERS     _IOW(SPIMIOC_BASE, 22, struct reg_write_request)
#define GET_CYCLE_PHASE     _IOWR(SPIMIOC_BASE, 23, struct get_cycle_phase)
#define CHAN_START_TEST     _IOW(SPIMIOC_BASE, 24, struct chx_enable*)

#define MOTOR_CYCLES_MAX    8192

#define ANGLE_CONVERT_PHASE  1456356ULL
#define CYCLES_CONVERT_PHASE 1024
#define DEFAULT_ANGLE_VAL    2880000
#define DEFAULT_PHASE_VAL    4194304
#define DEFAULT_CNT_VAL      4096

#define MOTOR_FULL_ROTATION_CYCLES 512
#define MOTOR_TOTAL_PHASES (MOTOR_FULL_ROTATION_CYCLES * CYCLES_CONVERT_PHASE)

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

typedef enum {
	COMPENSATE_NONE,
	COMPENSATE_FORWARD,
	COMPENSATE_FORWARD_CHANGED,
	COMPENSATE_BACK,
	COMPENSATE_BACK_CHANGED
} COMPENSATE_STATUS;

struct chx_speed {
	enum channel_select chx;
	enum motor_speed_gear speed;
};

struct get_angle {
	enum channel_select chx;
	u64 phase_done_toltal;
	u8 dir;
	COMPENSATE_STATUS compensate_status;
	u32 compensation_phases;
};

struct get_targ {
	enum channel_select chx;
	u8 dir;
	u32 cycles_targ;
	u32 phase_targ;
	u64 phase_targ_toltal;
	COMPENSATE_STATUS compensate_status;
	u32 compensation_phases;
};

struct reg_read_request {
	u8 addr;
	u8 len;
	u8 *data;
};

struct reg_write_request {
	u8 addr;
	u8 len;
	u8 data[64];
};

struct get_cycle_phase {
	enum channel_select chx;
	u32 cycles_cnt;
	u32 phase_cnt;
};

#endif // MOTOR_SDK_TMI8152_H
