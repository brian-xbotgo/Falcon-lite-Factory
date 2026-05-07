#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/sizes.h>

/* xbotgo add */
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>


/***************************** user setting ********************************************/
#define OMFG_ENABLE_LOG 1 /* CHANGE Customer need to change this for enable/disable log */
#define OMFG_ENABLE_ERR_LOG 1
#define OM_PROPERTIES "om70X0X-bat"
#define OM_DRV_VER		"V2026031301"

#define OM70X0X_DRIVER_VERSION_TYPE			2		//1 voltage only OM7010X; 2 voltage-current OM7020X
#define OM70X0X_DRIVER_VERSION_HARDWARE		1		//om70X01
#define OM70X0X_DRIVER_VERSION_HARDWARE_SON	2		//1 OM70101DB; 2 OM70101DC
#define OM70X0X_DRIVER_VERSION_SOFTWARE		1.43

#if OM70X0X_DRIVER_VERSION_TYPE == 1
	#if OM70X0X_DRIVER_VERSION_HARDWARE_SON == 1
	#define OM70X0X_PRODUCT_ID		0xA3	//OM70101DB:0xA3(not active)/0xA0(active); OM70101DC/OM70201:0xB1
	#else 
	#define OM70X0X_PRODUCT_ID		0xB1
	#endif
#else
#define OM70X0X_PRODUCT_ID		0xB1	//om70101:0xA3(not active)/0xA0(active); om70201:0xB1
#endif

#define USER_USE_TEMP_SOURCE_TYPE_INTERNAL	0
#define USER_USE_TEMP_SOURCE_TYPE_EXTERNAL	1
#define USER_USE_TEMP_SOURCE_TYPE_HOST_CPU	2

#define USER_SENSING			2		//mohm
#define USER_VOLTAGE_MULTIPLE	1		
#define USER_USE_TEMP_SOURCE_TYPE USER_USE_TEMP_SOURCE_TYPE_EXTERNAL //0 internal; 1 external NTC; 2 host MCU
/***************************** user setting ********************************************/
/* Profile write verify control
 * 1: enable write-after-read verify with up to 3 retries
 * 0: disable
 */
#ifndef OMG_ENABLE_PROFILE_WRITE_VERIFY
#define OMG_ENABLE_PROFILE_WRITE_VERIFY 1
#if OMG_ENABLE_PROFILE_WRITE_VERIFY
#define OMG_PROFILE_VERIFY_RETRY_MAX  3
#define OMG_PROFILE_VERIFY_DELAY_MS   1
#endif
#endif

/***************************** system setting ********************************************/
#define OM70X0X_USE_SOH_HOST	1	//0 use default soh calc; 1 use soh host + cycle cnt
/***************************** system setting ********************************************/

/***************************** vbus           ********************************************/
#define USB_CONN_IRQF	\
	(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT)
/***************************** vbus           ********************************************/

/***************************** battery profile ********************************************/
#define MAX_BATTERY_PROFILE_PAIR_DATA_NUM 75

// 20251121会议决定, 因操作i2c总处理灯显问题，导致异常，故放弃次方案。
// 现解决方案：当进入灯显，则复位电量计并重装驱动。
#define CHARGER_LED_MODE_PROTECT					1
#define GAUGE_SW_RESET_TEST_MODE					0
#define CHARGER_AUTO_STOP_FOR_TEMP_TEST_MODE		0
#define CONFIG_VBUS_CONN_GPIO						1

#if CHARGER_LED_MODE_PROTECT
#define BATTERY_SN_KEY_LABEL		"[batterySn]"
#define BATTERY_SN_VAL_LABEL		"sn = "
#define SS_BATTERY_LABEL			"SS"  //思商电池配置
#define RD_BATTERY_LABEL			"RD"  //荣事通达电池配置

typedef enum _BATTERY_TYPE_E
{
	UNKNOWN_BATTERY_TYPE = -1,
	RD_BATTERY_TYPE = 3,  //思商电池
	SS_BATTERY_TYPE = 6,  //荣事通达电池
} BATTERY_TYPE_E;

// 定义需与misc_app定义一致
typedef enum _FASYNC_SI_CODE_E {
	UNKNOWN_SI_CODE = -1,
	USB_CHARGER_REMOVED = 0,
	USB_CHARGER_INSERTED = 1,
} FASYNC_SI_CODE_E;

// async message to misc app
static struct fasync_struct *g_async_queue;
static int disable_charger_for_new_profile = 0;
#endif

#if GAUGE_SW_RESET_TEST_MODE
#define GAUGE_SW_RESET_TEST_NUM_MAX		(10000)
static int gauge_test_num = 0;
int gauge_test_flag = 0;
#endif

static char model_name[32] = {0};
static bool model_name_inited = false;

typedef struct 
{
	unsigned char addr;
	unsigned char value;
} PairDataType;

typedef struct
{
	unsigned char ver;
	PairDataType pairData[MAX_BATTERY_PROFILE_PAIR_DATA_NUM];
	unsigned char pairDataNum;
} BatteryProfileDataType;

static const BatteryProfileDataType g_battery_profile_data_SS =
{
    //Config_2m_ohm_100kNTC_verB 2025-08-01 15:24:05 SS 10000mAH
	.ver = 0x04,	//can not be 0, need to change when pairData data change
    .pairData = {
        {0x20, 0x00},
        {0x21, 0x06},
        {0x22, 0x0D},
        {0x23, 0x14},
        {0x24, 0x1B},
        {0x25, 0x21},
        {0x26, 0x28},
        {0x27, 0x2E},
        {0x28, 0x36},
        {0x29, 0x3D},
        {0x2A, 0x45},
        {0x2B, 0x4E},
        {0x2C, 0x57},
        {0x2D, 0x61},
        {0x2E, 0x69},
        {0x2F, 0x6F},
        {0x30, 0x76},
        {0x31, 0x7D},
        {0x32, 0x84},
        {0x33, 0x8A},
        {0x34, 0x8F},
        {0x35, 0x95},
        {0x36, 0x9A},
        {0x37, 0xA0},
        {0x38, 0xA8},
        {0x39, 0xB9},
        {0x3A, 0xE2},
        {0x3B, 0xED},
        {0x3C, 0xF3},
        {0x3D, 0xF8},
        {0x3E, 0xFB},
        {0x3F, 0xFD},
        {0x40, 0xFF},
        {0x41, 0x13},
        {0x42, 0x10},
        {0x43, 0x09},
        {0x44, 0x07},
        {0x45, 0x05},
        {0x46, 0x05},
        {0x47, 0x01},
        {0x48, 0x01},
        {0x49, 0x09},
        {0x4E, 0x52},
        {0x4F, 0xB6},
        {0x50, 0x0A},
        {0x51, 0x2E},
        {0x52, 0x0A},
        {0x53, 0x6E},
        {0x54, 0x07},
        {0x55, 0x05},
        {0x5A, 0x00},
        {0x5B, 0x05},
        {0x5C, 0xEB},
        {0x5D, 0x0D},
        {0x5E, 0x0C},
        {0x5F, 0x80},
        {0x60, 0x6E},
        {0x61, 0x00},
        {0x62, 0xFB},
        {0x63, 0x50},
        {0x64, 0x1E},
        {0x65, 0x6C},
        {0x66, 0xDC},
        {0x67, 0x01},
        {0x68, 0x32},
        {0x69, 0xF0},
        {0x6A, 0x80},
        {0x80, 0x61},
#if OM70X0X_USE_SOH_HOST
        {0x01, 0xE3 | 0x10},
#else
		{0x01, 0xE3},
#endif
        {0x0A, 0x80},
        {0x96, 0x03},
        {0x87, 0x4D},
        {0x10, 0x93},
    },
    .pairDataNum = 73,
};

const BatteryProfileDataType g_battery_profile_data_RSTD =
{
    //Config_2m_ohm_100kNTC_verB 2025-08-11 10:57:57 RSTD 10000mAH
	.ver = 0x04,
    .pairData = {
        {0x20, 0x00},
        {0x21, 0x06},
        {0x22, 0x0D},
        {0x23, 0x14},
        {0x24, 0x1B},
        {0x25, 0x22},
        {0x26, 0x28},
        {0x27, 0x2F},
        {0x28, 0x36},
        {0x29, 0x3D},
        {0x2A, 0x45},
        {0x2B, 0x4E},
        {0x2C, 0x57},
        {0x2D, 0x61},
        {0x2E, 0x69},
        {0x2F, 0x70},
        {0x30, 0x77},
        {0x31, 0x7E},
        {0x32, 0x84},
        {0x33, 0x8A},
        {0x34, 0x90},
        {0x35, 0x95},
        {0x36, 0x9B},
        {0x37, 0xA1},
        {0x38, 0xA8},
        {0x39, 0xBA},
        {0x3A, 0xE4},
        {0x3B, 0xEF},
        {0x3C, 0xF4},
        {0x3D, 0xF8},
        {0x3E, 0xFB},
        {0x3F, 0xFD},
        {0x40, 0xFF},
        {0x41, 0x16},
        {0x42, 0x11},
        {0x43, 0x0A},
        {0x44, 0x05},
        {0x45, 0x08},
        {0x46, 0x07},
        {0x47, 0x01},
        {0x48, 0x02},
        {0x49, 0x10},
        {0x4E, 0x52},
        {0x4F, 0xB6},
        {0x50, 0x0A},
        {0x51, 0x2E},
        {0x52, 0x0A},
        {0x53, 0x6E},
        {0x54, 0x06},
        {0x55, 0x04},
        {0x5A, 0x00},
        {0x5B, 0x05},
        {0x5C, 0xEB},
        {0x5D, 0x0C},
        {0x5E, 0x18},
        {0x5F, 0x80},
        {0x60, 0x6E},
        {0x61, 0x05},
        {0x62, 0xF9},
        {0x63, 0x50},
        {0x64, 0x1E},
        {0x65, 0x6C},
        {0x66, 0xDC},
        {0x67, 0x02},
        {0x68, 0x32},
        {0x69, 0xEB},
        {0x6A, 0x80},
        {0x80, 0x61},
#if OM70X0X_USE_SOH_HOST
        {0x01, 0xE3 | 0x10},
#else
		{0x01, 0xE3},
#endif
        {0x0A, 0x80},
        {0x96, 0x03},
        {0x87, 0x4D},
        {0x10, 0x93},
    },
    .pairDataNum = 73,
};

const BatteryProfileDataType g_battery_profile_data_RSTD2 =
{
    //Config_2m_ohm_100kNTC_verB 2025-08-29 10:38:33 RSTD2 10500mAH
    .ver = 0x11, 
    .pairData = {
        {0x20, 0x00},
        {0x21, 0x07},
        {0x22, 0x0E},
        {0x23, 0x16},
        {0x24, 0x1D},
        {0x25, 0x25},
        {0x26, 0x2C},
        {0x27, 0x32},
        {0x28, 0x39},
        {0x29, 0x41},
        {0x2A, 0x49},
        {0x2B, 0x51},
        {0x2C, 0x59},
        {0x2D, 0x61},
        {0x2E, 0x68},
        {0x2F, 0x6E},
        {0x30, 0x75},
        {0x31, 0x7D},
        {0x32, 0x85},
        {0x33, 0x8C},
        {0x34, 0x92},
        {0x35, 0x97},
        {0x36, 0x9D},
        {0x37, 0xA2},
        {0x38, 0xA9},
        {0x39, 0xB2},
        {0x3A, 0xD6},
        {0x3B, 0xEC},
        {0x3C, 0xF3},
        {0x3D, 0xF8},
        {0x3E, 0xFB},
        {0x3F, 0xFD},
        {0x40, 0xFF},
        {0x41, 0x13},
        {0x42, 0x0E},
        {0x43, 0x0A},
        {0x44, 0x06},
        {0x45, 0x06},
        {0x46, 0x07},
        {0x47, 0x01},
        {0x48, 0x01},
        {0x49, 0x0D},
        {0x4E, 0x52},
        {0x4F, 0x87},
        {0x50, 0x09},
        {0x51, 0xEE},
        {0x52, 0x0A},
        {0x53, 0x6E},
        {0x54, 0x02},
        {0x55, 0x01},
        {0x5A, 0x00},
        {0x5B, 0x08},
        {0x5C, 0x07},
        {0x5D, 0x0C},
        {0x5E, 0x18},
        {0x5F, 0x80},
        {0x60, 0x6E},
        {0x61, 0xFF},
        {0x62, 0xF8},
        {0x63, 0x50},
        {0x64, 0x1E},
        {0x65, 0x6C},
        {0x66, 0xDC},
        {0x67, 0x02},
        {0x68, 0x32},
        {0x69, 0xFE},
        {0x6A, 0x10},
        {0x80, 0x61},
#if OM70X0X_USE_SOH_HOST
        {0x01, 0xE3 | 0x10},
#else
		{0x01, 0xE3},
#endif
        {0x0A, 0x80},
        {0x96, 0x03},
        {0x87, 0x4D},
        {0x10, 0x93},
    },
    .pairDataNum = 73,
};

const BatteryProfileDataType g_battery_profile_data_SS2 =
{
    //Config_2m_ohm_100kNTC_verB 2025-09-21 15:43:43 SS2 9000mAH
    .ver = 0x11,
    .pairData = {
        {0x20, 0x00},
        {0x21, 0x07},
        {0x22, 0x0E},
        {0x23, 0x16},
        {0x24, 0x1D},
        {0x25, 0x24},
        {0x26, 0x2A},
        {0x27, 0x31},
        {0x28, 0x38},
        {0x29, 0x3F},
        {0x2A, 0x47},
        {0x2B, 0x4F},
        {0x2C, 0x57},
        {0x2D, 0x60},
        {0x2E, 0x67},
        {0x2F, 0x6E},
        {0x30, 0x75},
        {0x31, 0x7D},
        {0x32, 0x84},
        {0x33, 0x8B},
        {0x34, 0x91},
        {0x35, 0x97},
        {0x36, 0x9C},
        {0x37, 0xA2},
        {0x38, 0xA9},
        {0x39, 0xB2},
        {0x3A, 0xD9},
        {0x3B, 0xEB},
        {0x3C, 0xF3},
        {0x3D, 0xF7},
        {0x3E, 0xFB},
        {0x3F, 0xFD},
        {0x40, 0xFF},
        {0x41, 0x1F},
        {0x42, 0x17},
        {0x43, 0x11},
        {0x44, 0x06},
        {0x45, 0x06},
        {0x46, 0x09},
        {0x47, 0x01},
        {0x48, 0x02},
        {0x49, 0x14},
        {0x4E, 0x52},
        {0x4F, 0xB7},
        {0x50, 0x09},
        {0x51, 0x85},
        {0x52, 0x0A},
        {0x53, 0x6E},
        {0x54, 0x07},
        {0x55, 0x06},
        {0x5A, 0x00},
        {0x5B, 0x07},
        {0x5C, 0x67},
        {0x5D, 0x0C},
        {0x5E, 0x0C},
        {0x5F, 0x80},
        {0x60, 0x6E},
        {0x61, 0xFF},
        {0x62, 0xFB},
        {0x63, 0x50},
        {0x64, 0x1E},
        {0x65, 0x6C},
        {0x66, 0xDC},
        {0x67, 0x01},
        {0x68, 0x32},
        {0x69, 0xFE},
        {0x6A, 0x10},
        {0x80, 0x61},
#if OM70X0X_USE_SOH_HOST
        {0x01, 0xEB | 0x10},
#else
		{0x01, 0xEB},
#endif
        {0x0A, 0x80},
        {0x96, 0x03},
        {0x87, 0x4D},
        {0x10, 0x93},
    },
    .pairDataNum = 73,
};

/***************************** battery profile ********************************************/


#define REG_PRODUCT_ID          0x00
#define REG_EN_CONF				0x01
#define REG_VCELL_H             0x02
#define REG_VCELL_L             0x03
#define REG_SOC_H             	0x04
#define REG_SOC_L		        0x05
#define REG_TEMP_EXTERNAL       0x06
#define REG_TEMP_INTERNAL       0x07
#if USER_USE_TEMP_SOURCE_TYPE == USER_USE_TEMP_SOURCE_TYPE_EXTERNAL
#define REG_TEMP				REG_TEMP_EXTERNAL
#else
#define REG_TEMP				REG_TEMP_INTERNAL
#endif
#define REG_CONFIG         		0x08
#define REG_CMD					0x09
#define REG_INT_CONFIG          0x0A
#define REG_SOC_ALERT           0x0B
#define REG_TEMP_MAX            0x0C
#define REG_TEMP_MIN            0x0D
#define REG_CURRENT_H           0x0E
#define REG_CURRENT_L           0x0F
#define REG_T_HOST            	0xA0
#define REG_USER_CONF           0xA1
#define REG_CYCLE_H             0xA4
#define REG_CYCLE_L             0xA5
#define REG_SOH                 0xA6
#define REG_SAVE_DSOC				REG_TEMP_MAX
#define REG_SAVE_DSOC_DEFAULT_VALUE	0xAA

#define CYCLE_CNT_INIT_MASK		0x10
#define SOH_INIT_MASK			0x08
#define SOC_INIT_MASK			0x04
#define ACTIVE_MODE_MASK		0x02

#define REG_EN_CONF_DEFAULT_VALUE	0x41
#define REG_EN_CONF_INT_TEMP_MASK	0x01
#define REG_EN_CONF_EXT_TEMP_MASK	0x02
#define REG_EN_CONF_T_HOST_EN_MASK	0x08

#define MAX_T_HOST				85
#define MIN_T_HOST				-40

#define MAX_CYCLE_CNT			1000
#define MAX_SOH					100
#define MIN_SOH					60

#define USER_CONFIG_VALUE_FINISH_INIT	0x5A	//check if reboot
#define USER_CONFIG_VALUE_RESET			0x00

#define TEMP_EXPANSION		10
#define SOC_EXPANSION		100
#define CUR_EXPANSION		10

//temperature compensation parameter(100K NTC; key 0.1 degree C, gain&&offset float*TEMP_FIXED_EXPANSION_TIMES(1000))
#define TEMP_FIXED_EXPANSION_TIMES 1000
#define TEMP_FIXED_KEY0	-50
#define TEMP_FIXED_KEY1 150
#define TEMP_FIXED_KEY2 300
#define TEMP_FIXED_KEY3 450
#define TEMP_FIXED_GAIN0    600
#define TEMP_FIXED_GAIN1    0
#define TEMP_FIXED_GAIN2    (-100)
#define TEMP_FIXED_GAIN3    0
#define TEMP_FIXED_GAIN4    287
#define TEMP_FIXED_OFFSET0   (3000 * TEMP_EXPANSION)
#define TEMP_FIXED_OFFSET1   (0 * TEMP_EXPANSION)
#define TEMP_FIXED_OFFSET2   (1500 * TEMP_EXPANSION)
#define TEMP_FIXED_OFFSET3   (-1500 * TEMP_EXPANSION)
#define TEMP_FIXED_OFFSET4   (-14400 * TEMP_EXPANSION)

#define OM70X0X_ERROR_NONE			0
#define OM70X0X_ERROR_IIC			-1
#define OM70X0X_ERROR_CHIP_ID		-2
#define OM70X0X_ERROR_NO_PROFILE	-3
#define OM70X0X_ERROR_OTHER			-4

#define OM_SLEEP_100MS          100
#define OM_SLEEP_200MS          200
#define OM_RETRY_COUNT          7

#define OMFG_NAME               "om70X0X"

#define queue_delayed_work_time  3000
#define queue_start_work_time    3000

#define VOL_UNIT				1000
#define CAPACITY_UNIT			1000
#define CHARGE_COUNTER			2000000

#define BATTERY_DESIGNE_CAPACITY	7000

/* xbotgo add */
#define BATTERY_HIGHT_TEMP				65
#define BATTERY_SUIT_TEMP				62
#define BATTERY_SUSPEND_LOW_TEMP		0
#define BATTERY_RESUME_LOW_TEMP			2
#define BATTERY_LOW_TEMP_COUNT_THESHOLD				5
#define BATTERY_HIGH_TEMP_COUNT_THESHOLD			5
#define IP2315_SLAVE_ID					0x75
#define OM7X0X0_SLAVE_ID				0x38

#define SOC_START_DIFF_MAX	15
#define ABS(x) (((x) > 0) ? (x) : -(x))

static bool is_chip_anomaly = false;
#define MAX_SOFT_RESET_COUNT 10
#define MIN_SOFT_RESET_VOLT		(3000)
#define OM_LOCKED_SOC 6

//cur adjust parameter
#define CUR_ADJUST_THRESHOLD_NUMBER	3
#define CUR_ADJUST_REGISTER_NUMBER 14
typedef struct
{
	short threshold[CUR_ADJUST_THRESHOLD_NUMBER];
	unsigned char addr[CUR_ADJUST_REGISTER_NUMBER];
	unsigned char value[CUR_ADJUST_THRESHOLD_NUMBER + 1][CUR_ADJUST_REGISTER_NUMBER];
} CurAdjustParamType;

static const CurAdjustParamType g_CurAdjustParam_SS = {
	.threshold = {-1500, -1300, 10},
	.addr = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x5B, 0x5C, 0x61, 0x62, 0x5D},
	.value = {
		{0x13, 0x10, 0x09, 0x07, 0x05, 0x05, 0x01, 0x01, 0x09, 0x06, 0x4E, 0xFF, 0xFB, 0x0D},	
		{0x13, 0x10, 0x09, 0x07, 0x05, 0x05, 0x01, 0x01, 0x09, 0x05, 0xEB, 0x00, 0xFB, 0x0D},	
		{0x13, 0x10, 0x09, 0x07, 0x05, 0x05, 0x01, 0x01, 0x09, 0x05, 0xEB, 0x00, 0xFB, 0x0D},
		{0x13, 0x10, 0x09, 0x07, 0x05, 0x05, 0x01, 0x01, 0x09, 0x05, 0xEB, 0x00, 0xFB, 0x0D},	
	},
};

static const CurAdjustParamType g_CurAdjustParam_RSTD = {
	.threshold = {-1500, -1300, 10},
	.addr = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x5B, 0x5C, 0x61, 0x62, 0x5D},
	.value = {
		{0x16, 0x11, 0x0A, 0x05, 0x08, 0x07, 0x01, 0x02, 0x10, 0x06, 0x60, 0x05, 0xF8, 0x0C},	
		{0x16, 0x11, 0x0A, 0x05, 0x08, 0x07, 0x01, 0x02, 0x10, 0x05, 0xEB, 0x05, 0xF9, 0x0C},	
		{0x13, 0x10, 0x09, 0x07, 0x05, 0x05, 0x01, 0x01, 0x09, 0x05, 0xEB, 0x00, 0xFB, 0x0C},
		{0x13, 0x10, 0x09, 0x07, 0x05, 0x05, 0x01, 0x01, 0x09, 0x05, 0xEB, 0x00, 0xFB, 0x0C},
	},
};

static const CurAdjustParamType g_CurAdjustParam_RSTD2 = {
	.threshold = {-2500, -1300, 10},
	.addr = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x5B, 0x5C, 0x61, 0x62, 0x5D},
	.value = {
		{0x18, 0x11, 0x0C, 0x08, 0x08, 0x09, 0x02, 0x02, 0x10, 0x0A, 0x52, 0xFF, 0xF4, 0x0C},
		{0x13, 0x0E, 0x0A, 0x06, 0x06, 0x07, 0x01, 0x01, 0x0D, 0x08, 0x73, 0xFF, 0xF8, 0x0C},
		{0x13, 0x0E, 0x0A, 0x06, 0x06, 0x07, 0x01, 0x01, 0x0D, 0x06, 0xFA, 0xFF, 0xF8, 0x0C},
		{0x13, 0x0E, 0x0A, 0x06, 0x06, 0x07, 0x01, 0x01, 0x0D, 0x06, 0xA1, 0xFF, 0xF8, 0x0A},
	},
};

static const CurAdjustParamType g_CurAdjustParam_SS2 = {
	.threshold = {-2500, -1300, 10},
	.addr = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x5B, 0x5C, 0x61, 0x62, 0x5D},
	.value = {
		{0x1F, 0x17, 0x11, 0x06, 0x06, 0x09, 0x01, 0x02, 0x14, 0x09, 0x85, 0xFF, 0xF8, 0x0C},
		{0x1F, 0x17, 0x11, 0x06, 0x06, 0x09, 0x01, 0x02, 0x14, 0x07, 0xCD, 0xFF, 0xFB, 0x0C},
		{0x1F, 0x17, 0x11, 0x06, 0x06, 0x09, 0x01, 0x02, 0x14, 0x06, 0x80, 0xFF, 0xFB, 0x0C},
		{0x1F, 0x17, 0x11, 0x06, 0x06, 0x09, 0x01, 0x02, 0x14, 0x06, 0x80, 0xFF, 0xFB, 0x0A},
	},
};

//charge endpoint adjust parameter
#define CHARGE_EP_THRESHOLD_NUMBER	10
typedef struct
{
	int soc_threshold_start;				//0.01
	uint16_t charge_soc_init_vol;  			// CV voltage to init soc
	int soc[CHARGE_EP_THRESHOLD_NUMBER];	//0.01
	int coeff[CHARGE_EP_THRESHOLD_NUMBER];	//0.01
} ChargeEndPointParamType;
static const ChargeEndPointParamType ChargeEndPointParam = {
	.soc_threshold_start = 90 * SOC_EXPANSION,
	.charge_soc_init_vol = 4200,
	.soc = {100 * SOC_EXPANSION, 99 * SOC_EXPANSION, 98 * SOC_EXPANSION, 97 * SOC_EXPANSION, 96 * SOC_EXPANSION, \
		95 * SOC_EXPANSION, 94 * SOC_EXPANSION, 93 * SOC_EXPANSION, 92 * SOC_EXPANSION, 91 * SOC_EXPANSION},
	.coeff = {100, 90, 80, 70, 60, 50, 40, 30, 15, 6},
};

#define CALC_VOL(v)			((v) * USER_VOLTAGE_MULTIPLE * 5 / 16)	//mV
#define CALC_CUR(i)			((i) * CUR_EXPANSION * 16 / 10 / USER_SENSING)	//0.1 mA
#define CALC_TEMP(t)		(((t) * TEMP_EXPANSION) / 2 - 40 * TEMP_EXPANSION)	// 0.1 degree C
#define CALC_SOC(soc)		(((soc) * SOC_EXPANSION) / 256)	//0.01%
#define CALC_CYCLE_CNT(cnt)	((cnt) / 32)
#define CALC_CYCLE_CNT_INVERSE(cnt)	(cnt * 32)
#define CALC_TEMP_INVERSE(t)	((t) * 2 + 80)

#define CALC_SOH_I(cycle) (MAX_SOH - (cycle) / 25)	// 500 cycle <-> soh 80%
#define CALC_SOH(cycle)	(((cycle) > MAX_CYCLE_CNT) ? MIN_SOH : CALC_SOH_I(cycle))
/* Round-up base for converting 0.01% to 1% near full charge */
#define SOC_ROUND_UP_BASE     9901


#define om_printk(fmt, arg...)                                                 \
	{                                                                          \
		if (OMFG_ENABLE_LOG)                                                   \
			printk("FG_OM70X0X : %s-%d : " fmt, __FUNCTION__ ,__LINE__,##arg);  \
		else {}                                                                \
	}

#define om_printk_err(fmt, arg...)                                                 \
	{                                                                          \
		if (OMFG_ENABLE_ERR_LOG)                                                   \
			pr_err("FG_OM70X0X : %s-%d : " fmt, __FUNCTION__ ,__LINE__,##arg);  \
		else {}                                                                \
	}

/* xbotgo add */
struct battery_data {
	bool battery_status;
	bool charger_status;
	int capacity;
	bool present;
	int voltage_now;
	const char *technology;
	int temp;
	int charge_full;
	int charge_counter;
	bool capacity_level;
	int current_now;
	bool health;
	int cycle_count;
};

/* xbotgo add */
struct battery_dev_data {
	unsigned int major;
	const char * name;
	dev_t devnu;
	struct mutex ibe_lock;
	struct mutex i2c_lock;
	
	struct class *cdev_class;
	struct cdev cdev;
	struct om_battery *om_bat;
};

#define I2CBAT_BASE  	'I'
#define I2CBAT_GET_STATUS				_IOR (I2CBAT_BASE, 0, struct battery_data*)
#define I2CBAT_GET_VBUS_STATUS			_IOR (I2CBAT_BASE, 1, int*)
#define I2CBAT_SET_CHARGE_STATUS		_IOR (I2CBAT_BASE, 2, int*)
#define I2CBAT_GET_CHARGE_STATUS		_IOR (I2CBAT_BASE, 3, int*)
#define I2CBAT_GET_MODEL_NAME			_IOR (I2CBAT_BASE, 4, char*)

struct om_battery {
	struct i2c_client *client;

	struct workqueue_struct *omfg_workqueue;
	struct delayed_work battery_delay_work;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
	struct power_supply om_bat;
#else
	struct power_supply *om_bat;
#endif
	int  chip_id;	//om70101:0xA3(not active)/0xA0(active); om70201:0xB1
	int  voltage;	//mV
	int  soc;		//1%
	int  temp;		//1℃
	int  temp_internal;	//1℃, from REG_TEMP_INTERNAL
#if OM70X0X_DRIVER_VERSION_TYPE == 2
	int om_current;	//1mA
	int  cycle;		//1
	int  soh;		//1%
#endif

	unsigned int design_capacity;	//mAH
	struct power_supply *usb_psy;
	
    /* xbotgo add */
	struct notifier_block psy_nb;
#if CONFIG_VBUS_CONN_GPIO
	bool stop_charger_flag;
#else
	int low_temp_num;
	int high_temp_num;
	bool low_temp_stop_charger_flag;
	bool high_temp_stop_charger_flag;
#endif
	struct battery_dev_data battery_dev;
	struct battery_data battery_dat; //give app
	
	struct gpio_desc *vbus_gpiod;
	int vbus_irq;
	struct delayed_work dw_det;
	unsigned long debounce_jiffies;
};


/* xbotgo add */
#define 	BAT_SS		1 		//思商
#define 	BAT_RSTD	2		//荣事通达
#define		BAT_RSTD2	3		//荣事通达2
#define		BAT_YJN		4		//永佳能
#define		BAT_SW		5		//赛旺
#define		BAT_SS2		6		//思商2
static const BatteryProfileDataType* profile_data_use = &g_battery_profile_data_SS;
static const CurAdjustParamType *g_cur_adjust_param_use = &g_CurAdjustParam_SS;
static unsigned char cache_soh = 0;		//soh value want to write back, only om7020X support
static unsigned char cache_cycle_cnt = 0;	//cycle cnt value want to write back, only om7020X support
static int battery_profile_type = UNKNOWN_BATTERY_TYPE;    //1:思商  2:荣事通达    		3:荣事通达2  	4:永佳能   	5:赛旺	6:思商2

static int om70X0X_init(struct om_battery *om_bat);
static int enable_charger(struct om_battery *om_bat, int en);
static int bat_get_vbus_status(struct om_battery *om_bat);
static int bat_get_charge_status(struct om_battery *om_bat);

/* reinstall profile flag: set via module param when reloading module */
static int reinstall_module_flag = 0;
static int soft_reset_flag = 0;

/* OM70X0X iic read function */
static int _om_read(struct i2c_client *client, const unsigned char addr, const unsigned char bytes, unsigned char* ret_data)
{
	int ret = 0;
	struct om_battery *om_bat = i2c_get_clientdata(client);

	if(om_bat)
	{
		mutex_lock(&(om_bat->battery_dev.i2c_lock));
		ret = i2c_smbus_read_i2c_block_data(client, addr, bytes, ret_data);
		mutex_unlock(&(om_bat->battery_dev.i2c_lock));
	}

	return ret;
}

/* OM70X0X iic write function */
static int _om_write(struct i2c_client *client, const unsigned char addr, const unsigned char* data, const unsigned char bytes)
{
	int ret = 0;
	struct om_battery *om_bat = i2c_get_clientdata(client);

	if(om_bat)
	{
		mutex_lock(&(om_bat->battery_dev.i2c_lock));
		ret = i2c_smbus_write_i2c_block_data(client, addr, bytes, data);
		mutex_unlock(&(om_bat->battery_dev.i2c_lock));
	}

	return ret;
}

static int _om70X0X_read_byte(struct i2c_client *client, const unsigned char addr, unsigned char* ret_data)
{
	return _om_read(client, addr, 1, ret_data);
}

static int _om70X0X_read_word(struct i2c_client *client, const unsigned char addr, unsigned short* ret_data)
{
	unsigned char tmp_data[2];
	int ret = _om_read(client, addr, 2, tmp_data);
	if(ret < 0)
	{
		return OM70X0X_ERROR_IIC;
	}
	
	*ret_data = ((unsigned short)tmp_data[0] << 8) | tmp_data[1];
	return OM70X0X_ERROR_NONE;
}

static int _om70X0X_write_byte(struct i2c_client *client, const unsigned char addr, const unsigned char data)
{
	return _om_write(client, addr, &data, 1);
}

static int _om70X0X_write_word(struct i2c_client *client, const unsigned char addr, const unsigned short data)
{
	int ret = 0;
	unsigned char tmp_data[2];
	tmp_data[0] = (data >> 8) & 0xFF;
	tmp_data[1] = data & 0xFF;
	
	ret = _om_write(client, addr, tmp_data, 2);
	if(ret < 0)
	{
		return OM70X0X_ERROR_IIC;
	}
	
	return OM70X0X_ERROR_NONE;
}

static int _om70X0X_min(const int a, const int b)
{
	return a < b ? a : b;
}

#if OMG_ENABLE_PROFILE_WRITE_VERIFY
static inline uint8_t _om_profile_expected_read(const uint8_t addr, const uint8_t written)
{
	switch(addr)
	{
		case 0x53: return 0x00;
		case 0x60: return 0x00;
		case 0x87: return 0x0D;
		default:   return written;
	}
}
#endif

static int _om70X0X_set_battery_profile(struct i2c_client *client, const BatteryProfileDataType* profile_data)
{
	int i;
	if(!profile_data)
	{
		om_printk_err("no profile\n");
		return OM70X0X_ERROR_NO_PROFILE;
	}
	
	for(i = 0; i < profile_data->pairDataNum; i ++)
	{
		const PairDataType* one = &profile_data->pairData[i];
		int ret = _om70X0X_write_byte(client, one->addr, one->value);
		if(ret < 0)
		{
			om_printk_err("i2c error\n");
			return OM70X0X_ERROR_IIC;
		}

#if OMG_ENABLE_PROFILE_WRITE_VERIFY
		// Read-back verify with up to OMG_PROFILE_VERIFY_RETRY_MAX retries
		uint8_t read_val = 0;
		int verify_ok = 0;
		const uint8_t expect_val = _om_profile_expected_read(one->addr, one->value);
		for(int t = 0; t <= OMG_PROFILE_VERIFY_RETRY_MAX; t++)
		{
			int r = _om70X0X_read_byte(client, one->addr, &read_val);
			if(r >= 0 && read_val == expect_val)
			{
				verify_ok = 1;
				break;
			}
			else
			{
				om_printk_err("verify error: addr=0x%02x wr_val=0x%02x rd_val=0x%02x expect_val=0x%02x\n", 
							one->addr, one->value, read_val, expect_val);
			}

			if(t < OMG_PROFILE_VERIFY_RETRY_MAX)
			{
				_om70X0X_write_byte(client, one->addr, one->value);
			}
		}
		if(!verify_ok)
		{
			om_printk_err("verify error\n");
			return OM70X0X_ERROR_IIC;
		}
#endif
	}
	
	return OM70X0X_ERROR_NONE;
}

#if USER_USE_TEMP_SOURCE_TYPE == USER_USE_TEMP_SOURCE_TYPE_HOST_CPU
static int om70X0X_set_temp(const char temp)
{
	if(temp < MIN_T_HOST || temp > MAX_T_HOST)
	{
		return OM70X0X_ERROR_OTHER;
	}
	
	const int tmp = (const int)temp;
	unsigned char value = (unsigned char)CALC_TEMP_INVERSE(tmp);
	return _om70X0X_write_byte(REG_T_HOST, value);
}
#endif

static int om70X0X_get_id(struct om_battery *om_bat)
{
	unsigned char id = 0xFF;
	int ret = _om70X0X_read_byte(om_bat->client, REG_PRODUCT_ID, &id);
	if(ret < 0)
	{
		om_printk_err("i2c error\n");
		return OM70X0X_ERROR_IIC;
	}

	om_printk("chip_id = 0x%x\n", id);	
	om_bat->chip_id = (int)id;

	if(id != OM70X0X_PRODUCT_ID)
	{
		om_printk_err("chipi ID error\n");
		return OM70X0X_ERROR_CHIP_ID;
	}

	return OM70X0X_ERROR_NONE;
}

static int om70X0X_get_vol(struct om_battery *om_bat)
{
	unsigned short vol;
	int ret = _om70X0X_read_word(om_bat->client, REG_VCELL_H, &vol);
	if(ret < 0)
	{
		om_printk_err("i2c error %d addr=0x%02X\n", ret, om_bat->client->addr);
		return OM70X0X_ERROR_IIC;
	}
	
	om_bat->voltage = (unsigned int)CALC_VOL((unsigned int)vol);
	return OM70X0X_ERROR_NONE;
}

static int _om70X0X_temp_fixed(const int temp)	//0.1 degree C
{
    int diff, t;
    if (temp <= TEMP_FIXED_KEY0) {
        diff = TEMP_FIXED_GAIN0 * temp + TEMP_FIXED_OFFSET0; 
    } else if (temp <= TEMP_FIXED_KEY1) {
        diff = TEMP_FIXED_GAIN1 * temp + TEMP_FIXED_OFFSET1;
    } else if (temp <= TEMP_FIXED_KEY2) {
        diff = TEMP_FIXED_GAIN2 * temp + TEMP_FIXED_OFFSET2;
    } else if (temp <= TEMP_FIXED_KEY3) {
        diff = TEMP_FIXED_GAIN3 * temp + TEMP_FIXED_OFFSET3;
    } else {
        diff = TEMP_FIXED_GAIN4 * temp + TEMP_FIXED_OFFSET4; 
    }

	//temp += diff / TEMP_FIXED_EXPANSION_TIMES;
	t = temp + diff / TEMP_FIXED_EXPANSION_TIMES;
	//om_printk("%s: diff = %d, temp = %d\n", __FUNCTION__, diff, temp);
    //return temp;
	return t;
}

static void om70X0X_temp_adjust(struct om_battery *om_bat, const int temp)	//temp unit: 1 degree C
{
	static unsigned char s_last_val = 0xFF;
	unsigned char new_val;

	if (om_bat->om_current > 5) {
		new_val = 0x0C;
	} else {
		if (temp < 5 || temp > 65)
			new_val = 0x14;
		else if (temp >= 45 && temp <= 55)
			new_val = 0x0F;
		else
			new_val = 0x0C;
	}

	if (new_val != s_last_val) {
		_om70X0X_write_byte(om_bat->client, 0x5D, new_val);
		s_last_val = new_val;
	}
}

/*
 * 温度两级滤波器
 * 第1级：滑动中值滤波（Median, win=11）
 * 第2级：持续性投票滤波（Persistence）
 */
#define TEMP_MEDIAN_WIN        11
#define TEMP_PERSIST_DRIFT_TH  3    /* 正常漂移阈值（°C），小于此值直接跟随 */
#define TEMP_PERSIST_N         12  	/* 大跳变需连续出现次数才接受 */

/* ---- 第1级：滑动中值滤波 ---- */
static int _om70X0X_median(const int *buf, int len)
{
	int tmp[TEMP_MEDIAN_WIN];
	int i, j, key;
	for (i = 0; i < len; i++)
		tmp[i] = buf[i];
	for (i = 1; i < len; i++) {
		key = tmp[i];
		j = i - 1;
		while (j >= 0 && tmp[j] > key) {
			tmp[j + 1] = tmp[j];
			j--;
		}
		tmp[j + 1] = key;
	}
	return tmp[len / 2];
}

static int _om70X0X_median_filter(const int new_temp)
{
	static int s_buf[TEMP_MEDIAN_WIN];
	static int s_buf_head = 0;
	static int s_buf_cnt  = 0;

	s_buf[s_buf_head] = new_temp;
	s_buf_head = (s_buf_head + 1) % TEMP_MEDIAN_WIN;
	if (s_buf_cnt < TEMP_MEDIAN_WIN)
		s_buf_cnt++;

	return _om70X0X_median(s_buf, s_buf_cnt);
}

/* ---- 第2级：持续性投票滤波 ---- */
static int _om70X0X_persistence_filter(const int new_temp)
{
	static bool s_inited   = false;
	static int  s_stable   = 0;
	static int  s_cand     = 0;
	static int  s_cand_cnt = 0;

	if (!s_inited) {
		s_stable = new_temp;
		s_inited = true;
		return s_stable;
	}

	if (abs(new_temp - s_stable) < TEMP_PERSIST_DRIFT_TH) {
		/* 正常漂移：直接跟随，重置候选 */
		s_stable   = new_temp;
		s_cand_cnt = 0;
	} else {
		/* 大跳变：等待持续确认 */
		if (s_cand_cnt > 0 && abs(new_temp - s_cand) < TEMP_PERSIST_DRIFT_TH) {
			s_cand_cnt++;
		} else {
			s_cand     = new_temp;
			s_cand_cnt = 1;
		}
		if (s_cand_cnt >= TEMP_PERSIST_N) {
			s_stable   = s_cand;
			s_cand_cnt = 0;
		}
	}
	return s_stable;
}

/* ---- 两级滤波入口 ---- */
static int _om70X0X_temp_filter(const int new_temp)
{
	int stage1 = _om70X0X_median_filter(new_temp);
	return _om70X0X_persistence_filter(stage1);
}

int om70X0X_get_temp(struct om_battery *om_bat)
{
	unsigned char register_value;
	int ret = 0;
	int register_value_int, temp_int, temp_raw;

	ret = _om70X0X_read_byte(om_bat->client, REG_TEMP, &register_value);
	if(ret < 0)
	{
		om_printk("%s fail! ret = %d addr = 0x%02X\n", __func__, ret, om_bat->client->addr);
		return OM70X0X_ERROR_IIC;
	}
	
	register_value_int = (int)register_value;
	temp_int = CALC_TEMP(register_value_int);	// 0.1 degree C
	temp_raw = _om70X0X_temp_fixed(temp_int) / TEMP_EXPANSION;	// 1 degree C
	om_bat->temp = _om70X0X_temp_filter(temp_raw);	// 滑动中值滤波
	om70X0X_temp_adjust(om_bat, temp_raw);  // 传入的温度单位为1 degree C

	return OM70X0X_ERROR_NONE;
}

static int om70X0X_get_temp_internal(struct om_battery *om_bat)
{
	unsigned char register_value;
	int ret = 0;
	int register_value_int, temp_int;

	ret = _om70X0X_read_byte(om_bat->client, REG_TEMP_INTERNAL, &register_value);
	if(ret < 0)
	{
		om_printk("%s fail! ret = %d\n", __func__, ret);
		return OM70X0X_ERROR_IIC;
	}

	register_value_int = (int)register_value;
	temp_int = CALC_TEMP(register_value_int);	// 0.1 degree C
	om_bat->temp_internal = temp_int / TEMP_EXPANSION;	// 1 degree C, no extra compensation
	return OM70X0X_ERROR_NONE;
}

static int inline _om70X0X_soc_compensation(struct om_battery *om_bat, const int rsoc)	//rsoc unit: 0.01%
{
	static int last_dsoc = -1, cur = 0;

	const int charge_cutoff_cur = 200;			    // 1 mA unit
	const int charge_soc_init_cur_hi = 100;		
	const int charge_soc_init_cur_lo = 0;			
	const int resting_cur = 10;						// mA
	const int SOC_MAX = 100 * SOC_EXPANSION;		//100 * 100 = 10000
	const int charge_step = 50;						//0.01%
	const int discharge_step = 50;					//0.01%
	const int min_cur_step_expansion = CHARGE_EP_THRESHOLD_NUMBER * 10;

	static int soc_init_flag = 0;
	static int SOC_cur = 0;
	static int min_cur_step;
	int dsoc = -1;
	static unsigned char using_index = CUR_ADJUST_THRESHOLD_NUMBER - 1;
	unsigned char index = 0;
	int cache_dsoc = 0;
	static unsigned char last_dsoc_save_value = REG_SAVE_DSOC_DEFAULT_VALUE;
	unsigned char dsoc_save_value = 0;
	int i;
	int ret = 0;

	// ----------------First time initialization-----------------
	if(last_dsoc < 0)
	{
		unsigned char org_dsoc = REG_SAVE_DSOC_DEFAULT_VALUE;
		_om70X0X_read_byte(om_bat->client, REG_SAVE_DSOC, &org_dsoc);
		cache_dsoc = (int)org_dsoc * SOC_EXPANSION;

		// Check if the saved value is valid and close to the current reading
		int valid = (cache_dsoc <= SOC_MAX) && (ABS(cache_dsoc - rsoc) <= (SOC_START_DIFF_MAX * SOC_EXPANSION));
		last_dsoc = valid ? cache_dsoc : rsoc;
		return last_dsoc;
	}
	// ----------------First time initialization end-----------------

	// ------------------------cur adjust begin-------------------------
	cur = om_bat->om_current;
	for(; index < CUR_ADJUST_THRESHOLD_NUMBER; index ++)
	{
		if(cur < g_cur_adjust_param_use->threshold[index])
		{
			break;
		}
	}
	
	if(using_index != index)
	{
		for(i = 0; i < CUR_ADJUST_REGISTER_NUMBER; i ++)
		{
			ret = _om70X0X_write_byte(om_bat->client, g_cur_adjust_param_use->addr[i], g_cur_adjust_param_use->value[index][i]);
			//om_printk("cur adjust, addr:0x%02x, value:0x%02x, ret = %d\n", g_cur_adjust_param_use->addr[i], g_cur_adjust_param_use->value[index][i], ret);
			if(ret < 0)
			{
				return last_dsoc;
			}
		}
		
		using_index = index;
	}
	// ------------------------cur adjust  end----------------------
	
	if(cur > resting_cur)   // charging    cur: mA  resting_cur: mA
	{		
		if(rsoc >= ChargeEndPointParam.soc_threshold_start)  // in charge endpoint
		{
			if(SOC_cur == 0)
			{
				SOC_cur = cur;
				min_cur_step = (SOC_cur - charge_cutoff_cur) / min_cur_step_expansion;	
			}

			for(i = 0; i < CHARGE_EP_THRESHOLD_NUMBER; i ++)
			{
				if(cur <= SOC_cur - min_cur_step * ChargeEndPointParam.coeff[i])
				{
					dsoc = ChargeEndPointParam.soc[i];
					break;
				}
			}
		
			if(dsoc < 0)
			{
				dsoc = ChargeEndPointParam.soc_threshold_start;
			}
		}
		else
		{
			dsoc = rsoc;
		}
		
		// Increase SOC slowly, but don't jump over rsoc
		dsoc = (dsoc > last_dsoc) ? (last_dsoc + charge_step) : last_dsoc;
		dsoc = _om70X0X_min(dsoc, SOC_MAX);

		if(om_bat->voltage > ChargeEndPointParam.charge_soc_init_vol 
			&& cur < charge_soc_init_cur_hi
			&& cur > charge_soc_init_cur_lo
			&& soc_init_flag == 0 
			&& (dsoc < SOC_MAX || rsoc < SOC_MAX))
 		{
			om_printk("OM Fuel Gauge: Entering initialization mode to calibrate rsoc. volt=%d, cur=%d, dsoc=%d, rsoc=%d\n", om_bat->voltage, cur, dsoc / SOC_EXPANSION, rsoc / SOC_EXPANSION);
			int ret = _om70X0X_write_byte(om_bat->client, REG_CONFIG, ACTIVE_MODE_MASK | SOC_INIT_MASK);
			if(ret == 0) // Success
			{
				soc_init_flag = 1;
				//dsoc = SOC_MAX;
			}
		}
	}
	else  // discharging or resting
	{
		// Discharging
		
		// ** FIX: Base the decrease on rsoc, not just last_dsoc **
		// If rsoc is lower than our last known value, decrease our value slowly.
		// This prevents rapid drops and provides smoothing.
		if (rsoc < last_dsoc) {
			dsoc = last_dsoc - discharge_step;
			// Ensure we don't drop below the real rsoc.
			if (dsoc < rsoc) {
				dsoc = rsoc;
			}
		} else {
			dsoc = last_dsoc;
		}

		if(dsoc <= ChargeEndPointParam.soc_threshold_start)
		{
			SOC_cur = 0;
			soc_init_flag = 0;
		}
	}

	dsoc_save_value = (unsigned char)(dsoc / SOC_EXPANSION);
	if(dsoc_save_value != last_dsoc_save_value)
	{
		ret = _om70X0X_write_byte(om_bat->client, REG_SAVE_DSOC, dsoc_save_value);
		if(ret == 0) // Success
		{
			last_dsoc_save_value = dsoc_save_value;
		}
	}
	
	last_dsoc = dsoc;
	return dsoc;
}

static int _om70X0X_software_reset(struct om_battery *om_bat)
{
	int ret = 0;
	om_printk("Performing software reset...\n");

	ret = _om70X0X_write_byte(om_bat->client, REG_CONFIG, 0x01);
	if (ret < 0) {
		om_printk_err("Software reset step 1 failed.\n");
		return ret;
	}

	msleep(50); // Wait for 50ms
	ret = _om70X0X_write_byte(om_bat->client, REG_CONFIG, 0x00);
	if (ret < 0) {
		om_printk_err("Software reset step 2 failed.\n");
		return ret;
	}
	
	soft_reset_flag = 1;

	ret = om70X0X_init(om_bat);
	if (ret < 0) {
		om_printk_err("Software reset step 3 (om70X0X_init) failed.\n");
		return ret;
	}

	soft_reset_flag = 0;

	return OM70X0X_ERROR_NONE;
}

static int _om70X0X_check_and_handle_anomaly(struct om_battery *om_bat)
{
	// 芯片异常状态还无错误, 回答: 当电压正常的时候,return; 或者芯片已经处于异常状态,后面的不再执行,直接return
#if GAUGE_SW_RESET_TEST_MODE
	if(!gauge_test_flag)
#endif
	{
		if (om_bat->voltage > MIN_SOFT_RESET_VOLT) {
			return OM70X0X_ERROR_NONE;
		}
	}

	unsigned char reg_config_val = 0xFF;
	unsigned char reg_cmd_val = 0xFF;
	int ret;
	int i;

	ret = _om70X0X_read_byte(om_bat->client, REG_CONFIG, &reg_config_val);
	if (ret < 0) {
		om_printk_err("Failed to read REG_CONFIG\n");
		return ret;
	}

	ret = _om70X0X_read_byte(om_bat->client, REG_CMD, &reg_cmd_val);
	if (ret < 0) {
		om_printk_err("Failed to read REG_CMD\n");
		return ret;
	}

#if 0
	// 睡眠的时候电压能读得到吗？ 回答:如果处于睡眠模式,get_vol==0, 出现睡眠模式不处理
	if(om_bat->voltage <= 5 && (reg_config_val & 0x02) == 0x00 && (reg_cmd_val & 0x04) == 0x00) {  // skip sleep mode
		return OM70X0X_ERROR_NONE;
	}
#endif

#if GAUGE_SW_RESET_TEST_MODE
	if(gauge_test_flag || (!gauge_test_flag && om_bat->voltage <= 5)) {
#else
	if (om_bat->voltage <= MIN_SOFT_RESET_VOLT) {
#endif
		om_printk_err("Anomaly detected! volt=%d, reg0x08=0x%02x, reg0x09=0x%02x. Attempting recovery.\n", om_bat->voltage, reg_config_val, reg_cmd_val);

		for (i = 0; i < MAX_SOFT_RESET_COUNT; i++) {
			om_printk("Soft reset attempt %d/%d\n", i + 1, MAX_SOFT_RESET_COUNT);
			ret = _om70X0X_software_reset(om_bat);	// 这里ret不判断, 回答:是的,这里有大的bug,下面加上
			if (ret < 0) {
				om_printk_err("Software reset failed on attempt %d\n", i + 1);

				//enable charger
				if(disable_charger_for_new_profile)
				{
					enable_charger(om_bat, 1);
					disable_charger_for_new_profile = 0;
				}

				msleep(50);
				continue;
			}
			
			msleep(100);
			//enable charger
			if(disable_charger_for_new_profile)
			{
				enable_charger(om_bat, 1);
				disable_charger_for_new_profile = 0;
			}

			ret = om70X0X_get_vol(om_bat);
			if (ret < 0) {
				om_printk_err("Failed to read voltage after reset attempt %d, read voltage = %d\n", i + 1, om_bat->voltage);
				continue;
			}

			if (om_bat->voltage > MIN_SOFT_RESET_VOLT) {
				is_chip_anomaly = false;
				om_printk("Chip recovery successful.\n");
				return OM70X0X_ERROR_NONE;
			}
		}

		om_printk_err("All software reset attempts failed. Locking SOC.\n");
		is_chip_anomaly = true;
		return -1;
	}

	return OM70X0X_ERROR_NONE;
}


#define REG_SOC_CONTROL      0x66
#define REG_SOC_CONTROL_CURR_CHARGE_DET_EN_MASK  0x10
static int _om_full_set_charge_detection(struct om_battery *om_bat, const uint8_t soc)
{
	static uint8_t last_soc_100_status = 255; 
	uint8_t current_soc_100_status = (soc == 100) ? 1 : 0;
	
	if(last_soc_100_status != current_soc_100_status)
	{
		uint8_t reg_value;
		int ret = _om70X0X_read_byte(om_bat->client, REG_SOC_CONTROL, &reg_value);
		if(ret < 0)
		{
			return OM70X0X_ERROR_IIC;
		}
		
		if(current_soc_100_status)
		{
			reg_value &= ~REG_SOC_CONTROL_CURR_CHARGE_DET_EN_MASK;
		}
		else
		{
			reg_value |= REG_SOC_CONTROL_CURR_CHARGE_DET_EN_MASK;
		}

		ret = _om70X0X_write_byte(om_bat->client, REG_SOC_CONTROL, reg_value);
		if(ret < 0)
		{
			om_printk_err("i2c error\n");
			return OM70X0X_ERROR_IIC;
		}
		
		last_soc_100_status = current_soc_100_status;
	}
	
	return OM70X0X_ERROR_NONE;
}



static int om70X0X_get_soc(struct om_battery *om_bat)
{
	// Check and handle anomaly state
	_om70X0X_check_and_handle_anomaly(om_bat);
	if (is_chip_anomaly) {
		om_printk_err("Chip is in anomaly state, returning locked SOC: %d\n", OM_LOCKED_SOC);
		om_bat->soc = OM_LOCKED_SOC;
		return OM70X0X_ERROR_NONE;
	}

	unsigned short register_value;
	int ret = _om70X0X_read_word(om_bat->client, REG_SOC_H, &register_value);
	if(ret < 0)
	{
		om_printk_err("i2c error\n");
		return OM70X0X_ERROR_IIC;
	}
	
	int register_value_int = (int)register_value;
	int soc_int = CALC_SOC(register_value_int);				//0.01%
	soc_int = _om70X0X_soc_compensation(om_bat, soc_int);	//0.01%

	om_bat->soc = soc_int * SOC_EXPANSION / SOC_ROUND_UP_BASE; // 1%

	_om_full_set_charge_detection(om_bat, (uint8_t)om_bat->soc);
	return 0;
}



static int om70X0X_active(struct om_battery *om_bat)
{
	return _om70X0X_write_byte(om_bat->client, REG_CONFIG, ACTIVE_MODE_MASK);
}

#if OM70X0X_DRIVER_VERSION_TYPE == 2
static int om70X0X_get_cur(struct om_battery *om_bat)
{
	unsigned short register_value;
	int ret = _om70X0X_read_word(om_bat->client, REG_CURRENT_H, &register_value);
	if(ret < 0)
	{
		om_printk_err("i2c error\n");
		return OM70X0X_ERROR_IIC;
	}
	
	short reg_val_short = (short)register_value;
	int reg_val_int = (int)reg_val_short;
	int cur_int = CALC_CUR(reg_val_int);
	om_bat->om_current = cur_int / CUR_EXPANSION;
	return OM70X0X_ERROR_NONE;
}

static int om70X0X_get_soh(struct om_battery *om_bat)
{
	unsigned char register_value;
	int ret = _om70X0X_read_byte(om_bat->client, REG_SOH, &register_value);
	if(ret < 0)
	{
		om_printk_err("i2c error\n");
		return OM70X0X_ERROR_IIC;
	}
	
	om_bat->soh = (int)register_value;
#if OM70X0X_USE_SOH_HOST == 0
	//save soh to flash at proper time
#endif
	return OM70X0X_ERROR_NONE;
}

static int om70X0X_get_cycle_cnt(struct om_battery *om_bat)
{
	unsigned short tmp;
	int ret = _om70X0X_read_word(om_bat->client, REG_CYCLE_H, &tmp);
	if(ret < 0)
	{
		om_printk_err("i2c error\n");
		return OM70X0X_ERROR_IIC;
	}
	
	om_bat->cycle = CALC_CYCLE_CNT(tmp);
	return OM70X0X_ERROR_NONE;
}

static int om70X0X_set_soh(struct om_battery *om_bat, const int soh)
{
	return _om70X0X_write_byte(om_bat->client, REG_SOH, (unsigned char)soh);
}

static int om70X0X_set_cycle_cnt(struct om_battery *om_bat, const uint16_t cycle_cnt)
{
	uint16_t cycle_cnt_register_value = CALC_CYCLE_CNT_INVERSE(cycle_cnt);
	return _om70X0X_write_word(om_bat->client, REG_CYCLE_H, cycle_cnt_register_value);
}

#if OM70X0X_USE_SOH_HOST
static int om70X0X_set_soh_by_cycle_cnt(struct om_battery *om_bat)
{
	static int last_soh = MAX_SOH;
	int soh = CALC_SOH(om_bat->cycle);
	if(soh == last_soh)
	{
		return OM70X0X_ERROR_NONE;
	}
	
	int ret = om70X0X_set_soh(om_bat, soh);
	if(ret < 0)
	{
		om_printk_err("i2c error\n");
		return OM70X0X_ERROR_IIC;
	}
	
	last_soh = soh;
	return OM70X0X_ERROR_NONE;
}
#endif
#endif

static int enable_charger(struct om_battery *om_bat, int enable)
{
	int ret = 0;
	unsigned short old_addr = 0;
	unsigned char dat = 0;

	if(!om_bat || !om_bat->client)
	{
		om_printk("om_bat or i2c client is NULL\n");
		return -1;
	}

	mutex_lock(&(om_bat->battery_dev.i2c_lock));

	if(enable) 
	{
		dat = 1;
	}
	else
	{
		dat = 0;
	}

	old_addr = om_bat->client->addr;
	om_bat->client->addr = IP2315_SLAVE_ID;
	ret = i2c_smbus_write_i2c_block_data(om_bat->client, 0x01, 1, &dat);
	om_printk("enable_charger dat = 0x%x, ret = %d\n", dat, ret);
	om_bat->client->addr = OM7X0X0_SLAVE_ID;
	if(old_addr != OM7X0X0_SLAVE_ID)
	{
		om_printk("i2c addr error: old_addr=0x%02X\n", old_addr);
	}

	mutex_unlock(&(om_bat->battery_dev.i2c_lock));

	return (ret < 0) ? (-1) : (0);
}

static BATTERY_TYPE_E read_battery_config(void)
{
	BATTERY_TYPE_E bat_type = SS_BATTERY_TYPE;
	struct file *filp = NULL;
    char buf[256 + 1];
    ssize_t ret;
    loff_t pos = 0;
	char *p1, *p2 = NULL;
	char *sn_key = NULL;
	char *sn_val = NULL;
	char battery_type[3] = {0};

	om_printk("enter %s\n", __func__);

    filp = filp_open("/device_data/info.ini", O_RDONLY, 0);
    if (IS_ERR(filp)) {
        pr_err("Error opening file\n");
        return PTR_ERR(filp);
    }

	memset(buf, 0, sizeof(buf));
    ret = kernel_read(filp, buf, sizeof(buf), &pos);
    if (ret >= 0) {
        om_printk("Read %zd bytes\n", ret);
		buf[256] = 0;
		sn_key = strstr(buf, BATTERY_SN_KEY_LABEL);
		if(sn_key)
		{
			*(sn_key + strlen(BATTERY_SN_KEY_LABEL)) = 0;
			p1 = sn_key + strlen(BATTERY_SN_KEY_LABEL) + 1;
			sn_val = strstr(p1, BATTERY_SN_VAL_LABEL);
			p2 = strstr(p1, "[");
			if(p2) *p2 = 0;

			if(sn_val)
			{
				om_printk("battery SN: %s\n", sn_val);
				battery_type[0] = *(sn_val + strlen(BATTERY_SN_VAL_LABEL) + 14);
				battery_type[1] = *(sn_val + strlen(BATTERY_SN_VAL_LABEL) + 15);
				battery_type[2] = 0;
				om_printk("battery_type: %s\n", battery_type);

				if(!strcmp(battery_type, RD_BATTERY_LABEL))
					bat_type = RD_BATTERY_TYPE;
				else
					bat_type = SS_BATTERY_TYPE;
			}
			else
			{
				om_printk("<1>battery SN not found\n");
			}
		}
		else
		{
			om_printk("<2>battery SN not found\n");
		}
    } else {
        om_printk_err("Error reading file\n");
    }

    filp_close(filp, NULL);

	om_printk("leave %s: bat_type=%d\n", __func__, bat_type);
	return bat_type;
}

static int om70X0X_init(struct om_battery *om_bat)
{
	unsigned char user_config;
	unsigned char configValue;

	om_printk("battery_profile_type=%d reinstall_module_flag=%d\n", battery_profile_type, reinstall_module_flag);
	reinstall_module_flag = 0;  //使用无灯显充电IC时，无需此功能。如是用有灯显IC，注释本行。

    /* xbotgo add */
	switch (battery_profile_type)
	{
		case BAT_SS:
		    profile_data_use = &g_battery_profile_data_SS;
			g_cur_adjust_param_use = &g_CurAdjustParam_SS;
		    om_printk("Using battery profile SS\n");
		    break;
        case BAT_RSTD:
		    profile_data_use = &g_battery_profile_data_RSTD;
			g_cur_adjust_param_use = &g_CurAdjustParam_RSTD;
		    om_printk("Using battery profile RSDT\n");
		    break;

		case BAT_RSTD2:
		    profile_data_use = &g_battery_profile_data_RSTD2;
			g_cur_adjust_param_use = &g_CurAdjustParam_RSTD2;
		    om_printk("Using battery profile RSTD2\n");
		    break;
			
		case BAT_SS2:
		    profile_data_use = &g_battery_profile_data_SS2;
			g_cur_adjust_param_use = &g_CurAdjustParam_SS2;
		    om_printk("Using battery profile SS2\n");
		    break;

#if 0
		case BAT_YJN:
		    profile_data_use = &g_battery_profile_data_YJN;
		    om_printk("Using battery profile YJN\n");
		    break;
		case BAT_SW:
		    profile_data_use = &g_battery_profile_data_SW;
		    om_printk("Using battery profile SW\n");
		    break;
#endif
        default:
		    profile_data_use = &g_battery_profile_data_RSTD2;
			g_cur_adjust_param_use = &g_CurAdjustParam_RSTD2;
		    om_printk("<default> Using battery profile RSTD2\n");
		    break;
	}

	if (!profile_data_use)
	{
        om_printk_err("No battery profile selected!\n");
        return OM70X0X_ERROR_NO_PROFILE;
    }


	
	int ret = om70X0X_get_id(om_bat);
	if(ret < 0)
	{
		om_printk_err("Failed to get battery ID, read ID = 0x%02x\n", om_bat->chip_id);
		om_bat->battery_dat.battery_status = 0;//xbotgo add
		return ret;
	}
	om_bat->battery_dat.battery_status = 1;//xbotgo add

	/* If reinstall flag is set, force write profile then active, regardless of REG_USER_CONF */
	if (reinstall_module_flag == 1 && !soft_reset_flag) {
		om_printk("reinstall_module_flag=1, force profile write and active\n");
		/* disable charger during profile write */
		enable_charger(om_bat, 0);
		disable_charger_for_new_profile = 1;
		msleep(100);

		ret = _om70X0X_set_battery_profile(om_bat->client, profile_data_use);
		if (ret < 0) {
			om_printk_err("Failed to set battery profile (reinstall)\n");
			return ret;
		}

		/* active */
		unsigned char configValue_local = ACTIVE_MODE_MASK;
		ret = _om70X0X_write_byte(om_bat->client, REG_CONFIG, configValue_local);
		if (ret < 0) {
			om_printk_err("Failed to write REG_CONFIG (reinstall)\n");
			return OM70X0X_ERROR_IIC;
		}

		/* record profile version */
		om_printk("<REG_USER_CONF>ver=%02x (reinstall)\n", profile_data_use->ver);
		ret = _om70X0X_write_byte(om_bat->client, REG_USER_CONF, profile_data_use->ver);
		if (ret < 0) {
			om_printk_err("Failed to write REG_USER_CONF (reinstall)\n");
			return OM70X0X_ERROR_IIC;
		}

		reinstall_module_flag = 0; // reset flag

		return OM70X0X_ERROR_NONE;
	}

	ret = _om70X0X_read_byte(om_bat->client, REG_USER_CONF, &user_config);
	if(ret < 0)
	{
		om_printk_err("Failed to read REG_USER_CONF\n");
		return OM70X0X_ERROR_IIC;
	}
	
	if(user_config == profile_data_use->ver)	//already init, only change to active mode
	{
		om_printk_err("already init, only change to active mode\n");
		return om70X0X_active(om_bat);
	}
	
	//disable charger
	enable_charger(om_bat, 0);
	disable_charger_for_new_profile = 1;
	msleep(100);

	ret = _om70X0X_set_battery_profile(om_bat->client, profile_data_use);
	if(ret < 0)
	{
		om_printk_err("Failed to set battery profile\n");
		return ret;
	}

	configValue = ACTIVE_MODE_MASK | SOC_INIT_MASK;
	
#if OM70X0X_DRIVER_VERSION_TYPE == 2
#if OM70X0X_USE_SOH_HOST == 1
	// cache_cycle_cnt = xxx();	// get saved cycle cnt from flash 
#else
	// cache_cycle_soh = xxx();	// get saved soh from flash
#endif

	if(cache_soh > 0)
	{
		ret = om70X0X_set_soh(om_bat, cache_soh);
		if(ret < 0)
		{
			om_printk_err("Failed to set SOH\n");
			return OM70X0X_ERROR_IIC;
		}
		configValue |= SOH_INIT_MASK;
	}
	
	if(cache_cycle_cnt > 0)
	{
		ret = om70X0X_set_cycle_cnt(om_bat, cache_cycle_cnt);
		if(ret < 0)
		{
			om_printk_err("Failed to set Cycle Count\n");
			return OM70X0X_ERROR_IIC;
		}
		configValue |= CYCLE_CNT_INIT_MASK;
	}
#endif
	
	ret = _om70X0X_write_byte(om_bat->client, REG_CONFIG, configValue);
	if(ret < 0)
	{
		om_printk_err("Failed to write REG_CONFIG\n");
		return OM70X0X_ERROR_IIC;
	}
	
	om_printk("<REG_USER_CONF>ver=%02x\n", profile_data_use->ver);
	ret = _om70X0X_write_byte(om_bat->client, REG_USER_CONF, profile_data_use->ver);
	if(ret < 0)
	{
		om_printk_err("Failed to write REG_USER_CONF\n");
		return OM70X0X_ERROR_IIC;
	}
	
	return OM70X0X_ERROR_NONE;
}

static int om_update_data(struct om_battery *om_bat)
{
	int ret = 0;

	if(OM7X0X0_SLAVE_ID != om_bat->client->addr)
	{
		om_printk("%s: addr=0x%02X\n", __func__, om_bat->client->addr);
		return 0;
	}

	ret += om70X0X_get_vol(om_bat);  // mV
	ret += om70X0X_get_temp(om_bat); // 1 degree C
	ret += om70X0X_get_temp_internal(om_bat); // 1 degree C, internal

#if OM70X0X_DRIVER_VERSION_TYPE == 2	
	ret += om70X0X_get_cur(om_bat);  // mA
	ret += om70X0X_get_soh(om_bat);  // %
	ret += om70X0X_get_cycle_cnt(om_bat);  // count
#endif
	ret += om70X0X_get_soc(om_bat);  // %



#if (OM70X0X_DRIVER_VERSION_TYPE == 2 && OM70X0X_USE_SOH_HOST)
	ret += om70X0X_set_soh_by_cycle_cnt(om_bat);
#endif

	return ret;
}

static int om_init_data(struct om_battery *om_bat)
{
	int ret = om70X0X_get_id(om_bat);
	if(ret != 0)
	{
		om_printk_err("get chip ID error\n");
		return ret;
	}

	om_bat->design_capacity = BATTERY_DESIGNE_CAPACITY;
	return om_update_data(om_bat);
}

static void om_bat_work(struct work_struct *work)
{
	struct delayed_work *delay_work;
	struct om_battery *om_bat;
	unsigned short old_addr;
	unsigned char dat;
	int ret;

	delay_work = container_of(work, struct delayed_work, work);
	om_bat = container_of(delay_work, struct om_battery, battery_delay_work);

#if GAUGE_SW_RESET_TEST_MODE
	if(gauge_test_num < GAUGE_SW_RESET_TEST_NUM_MAX)
	{
		gauge_test_num++;
		if(!gauge_test_flag)
			gauge_test_flag = 1;
		om_printk_err("gauge_test_num=%d\n", gauge_test_num);
	}
	else
	{
		if(gauge_test_flag) 
			gauge_test_flag = 0;
	}
#endif

	ret = om_update_data(om_bat);
	if (ret < 0)
	{
		om_printk_err("iic read error when update data");
	}


	/*xbotgo add*/
#if 1
	(void)old_addr;
	(void)dat;
#if 0
	old_addr = om_bat->client->addr;
	om_bat->client->addr = IP2315_SLAVE_ID;

	// 加载profile前都要diable charger，加载profile完毕后enable charger。
	// 当处于充电状态时，电池温度大于等于65度或电池温度小于等于0度，disable charger。
	// 当处于停充状态时，电池温度在2到50度之间，enable charger。
	// 但这个过程之间，可能因为i2c的总线或设备异常，导致charger使能/禁用操作失败。（目前没有实际测试出来）
	// 这里做个兜底，保证charger充电状态和程序标志位om_bat->stop_charger_flag一致，避免逻辑混乱。
	dat = 0;
	ret = i2c_smbus_read_i2c_block_data(om_bat->client, 0x01, 1, &dat);
	om_bat->client->addr = old_addr;
	if(ret > 0)
	{
		if((0x00 == dat) && 
			((false == om_bat->low_temp_stop_charger_flag) && (false == om_bat->high_temp_stop_charger_flag)))
		{
			enable_charger(om_bat, 1);
			dev_err(&om_bat->client->dev, "sync stop_charger_flag to 0\n");
		}
		else if((0x01 == dat) && 
				((true == om_bat->low_temp_stop_charger_flag) || (true == om_bat->high_temp_stop_charger_flag)))
		{
			enable_charger(om_bat, 0);
			dev_err(&om_bat->client->dev, "sync stop_charger_flag to 1\n");
		}
	}

	om_bat->client->addr = old_addr;
#endif

#if CHARGER_AUTO_STOP_FOR_TEMP_TEST_MODE
	{
		static unsigned int charger_auto_stop_test_flag = 0;
		static unsigned int skip_num = 0;

		// 一轮测试包括两阶段：
		// 第一阶段（正常顺寻）：45度充电-》65度停充-》45度充电-》0度停充-》2度充电
		// 第二阶段（杂乱顺序）：45度充电-》0度充电-》45度充电-》65度停充-》2度充电
		// 注意：此处测试系列可自行定义，但要考虑应用端现有的65度停充，70度高温关机逻辑。
		if(skip_num < 5)
		{
			skip_num++;
		}
		else if(0 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 10)
		{
			om_bat->temp = 45;
			if(0 == charger_auto_stop_test_flag)
				printk("charger loop test ...\n");
			charger_auto_stop_test_flag++;
		}
		else if(10 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 20)
		{
			om_bat->temp = 65;
			if(10 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(20 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 30)
		{
			om_bat->temp = 45;
			if(20 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(30 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 40)
		{
			om_bat->temp = 0;
			if(30 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(40 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 50)
		{
			om_bat->temp = 45;
			if(40 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(50 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 60)
		{
			om_bat->temp = 65;
			if(50 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(60 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 70)
		{
			om_bat->temp = 2;
			if(60 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(70 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 80)
		{
			om_bat->temp = 0;
			if(70 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(80 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 90)
		{
			om_bat->temp = 45;
			if(80 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(90 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 100)
		{
			om_bat->temp = 65;
			if(90 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else if(100 <= charger_auto_stop_test_flag && charger_auto_stop_test_flag < 110)
		{
			om_bat->temp = 2;
			if(100 == charger_auto_stop_test_flag)
				printk("switch to %d ...\n", om_bat->temp);
			charger_auto_stop_test_flag++;
		}
		else
		{
			printk("switch to head ...\n");
			charger_auto_stop_test_flag = 0;
		}
	}
#endif

#if CONFIG_VBUS_CONN_GPIO
#else
	/*if temperaturn more than 65, stop charger*/
	if(((false == om_bat->low_temp_stop_charger_flag) || (false == om_bat->high_temp_stop_charger_flag)) && 
		((om_bat->temp >= BATTERY_HIGHT_TEMP) || (om_bat->temp <= BATTERY_SUSPEND_LOW_TEMP)))
	{
		if((om_bat->temp >= BATTERY_HIGHT_TEMP) && (false == om_bat->high_temp_stop_charger_flag)) 
		{
			om_bat->high_temp_num++;
			if(om_bat->high_temp_num >= BATTERY_HIGH_TEMP_COUNT_THESHOLD)
			{
				om_bat->high_temp_stop_charger_flag = true;
				enable_charger(om_bat, 0);
				dev_err(&om_bat->client->dev, "stop charger at temp %d\n", om_bat->temp);
			}

			if(om_bat->low_temp_num != 0)
				om_bat->low_temp_num = 0;
			if(om_bat->low_temp_stop_charger_flag)
				om_bat->low_temp_stop_charger_flag = false;
		}
		else if((om_bat->temp <= BATTERY_SUSPEND_LOW_TEMP) && (false == om_bat->low_temp_stop_charger_flag)) 
		{
			om_bat->low_temp_num++;
			if(om_bat->low_temp_num >= BATTERY_LOW_TEMP_COUNT_THESHOLD)
			{
				om_bat->low_temp_stop_charger_flag = true;
				enable_charger(om_bat, 0);
				dev_err(&om_bat->client->dev, "stop charger at temp %d\n", om_bat->temp);
			}

			if(om_bat->high_temp_num != 0)
				om_bat->high_temp_num = 0;
			if(om_bat->high_temp_stop_charger_flag)
				om_bat->high_temp_stop_charger_flag = false;
		}
	}

	/*if temperaturn is suitable, start charger again*/
	if(((true == om_bat->low_temp_stop_charger_flag) || (true == om_bat->high_temp_stop_charger_flag)) && 
		((om_bat->temp < BATTERY_SUIT_TEMP) && (om_bat->temp >= BATTERY_RESUME_LOW_TEMP)))
	{
		enable_charger(om_bat, 1);
		dev_info(&om_bat->client->dev, "start charger at temp %d\n", om_bat->temp);

		if(om_bat->low_temp_num != 0)
			om_bat->low_temp_num = 0;
		if(om_bat->low_temp_stop_charger_flag)
			om_bat->low_temp_stop_charger_flag = false;

		if(om_bat->high_temp_num != 0)
			om_bat->high_temp_num = 0;
		if(om_bat->high_temp_stop_charger_flag)
			om_bat->high_temp_stop_charger_flag = false;
	}
#endif
#endif

#if CONFIG_VBUS_CONN_GPIO
	//
#else
	#ifdef OM_PROPERTIES
	#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
	power_supply_changed(&om_bat->om_bat); 
	#else
	power_supply_changed(om_bat->om_bat); 
	#endif
	#endif
#endif

	queue_delayed_work(om_bat->omfg_workqueue, &om_bat->battery_delay_work, msecs_to_jiffies(queue_delayed_work_time));
}

#ifdef OM_PROPERTIES
static int om_battery_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	int ret = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
	struct om_battery *om_bat;
	om_bat = container_of(psy, struct om_battery, om_bat); 
#else
	//struct om_battery *om_bat = power_supply_get_drvdata(psy); 
#endif

	switch(psp) {
	default:
		ret = -EINVAL; 
		break; 
	}

	return ret;
}

static int om_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	int ret = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
	struct om_battery *om_bat;
	om_bat = container_of(psy, struct om_battery, om_bat); 
#else
	struct om_battery *om_bat = power_supply_get_drvdata(psy); 
#endif
	union power_supply_propval pval = {0};

	if (!om_bat->usb_psy)
		om_bat->usb_psy = power_supply_get_by_name("usb");

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if(om_bat->usb_psy)
		{
			power_supply_get_property(om_bat->usb_psy, POWER_SUPPLY_PROP_STATUS, &pval);
			val->intval = pval.intval;
		} 
		else 
		{
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = om_bat->soc;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = om_bat->voltage <= 0 ? 0 : 1;
		break;  
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = om_bat->voltage * VOL_UNIT;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_TEMP: 
		val->intval = om_bat->temp;
		break;
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		val->intval = om_bat->temp_internal;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
            val->intval = om_bat->design_capacity * CAPACITY_UNIT;
            break;
    case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN: 
            val->intval = om_bat->design_capacity * CAPACITY_UNIT;	
            break;
    case POWER_SUPPLY_PROP_CHARGE_COUNTER:
	    val->intval = CHARGE_COUNTER;
	    break;
    case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
	    if(om_bat->soc > 0)
	    	val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	    else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	    break;
#if OM70X0X_DRIVER_VERSION_TYPE == 2
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = om_bat->om_current;
		break;	
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval= POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = om_bat->cycle;
		break;
#endif
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = bat_get_charge_status(om_bat);
		//om_printk("POWER_SUPPLY_PROP_ONLINE: %d %d\n", val->intval, om_bat->stop_charger_flag);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		{
			if(false == model_name_inited)
			{
				if(RD_BATTERY_TYPE == battery_profile_type)
				{
					sprintf(model_name, "%s_0x%02X_%s", RD_BATTERY_LABEL, profile_data_use->ver, OM_DRV_VER);
					val->strval = model_name;
				}
				else
				{
					sprintf(model_name, "%s_0x%02X_%s", SS_BATTERY_LABEL, profile_data_use->ver, OM_DRV_VER);
					val->strval = model_name;
				}

				model_name_inited = true;
			}
			else
			{
				val->strval = model_name;
			}
		}
		break;
	default:
		ret = -EINVAL; 
		break;
	}

	return ret;
}

static enum power_supply_property om_battery_properties[] = {
	
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
#if OM70X0X_DRIVER_VERSION_TYPE == 2
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
#endif
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_MODEL_NAME,
};
#endif 


/* xbotgo add */
static int usb_charger_notifier_call(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	int ret = 0;
    struct power_supply *psy = data;
	union power_supply_propval prop;
	struct om_battery *om_bat = container_of(nb, struct om_battery, psy_nb); 
#if CHARGER_LED_MODE_PROTECT
	struct kernel_siginfo info = {0};
	struct task_struct *p = NULL;
	int find_misc_app_flag = 0;
#endif

	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	
	if (strcmp(psy->desc->name, "usb-charger"))
	{
		return NOTIFY_OK;
	}
	
#if CONFIG_VBUS_CONN_GPIO
	(void)ret;
	(void)prop;
	(void)om_bat;
	(void)info;
	(void)find_misc_app_flag;
	(void)p;
#else	
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &prop);
	if(ret)
	{
		dev_err(&psy->dev, "get usb-charger supply property fail\n");
		return NOTIFY_BAD;
	}

	info.si_signo = SIGIO;
	p = &init_task;
    for_each_process(p)
	{
	    //printk("p->pid=%d ,p->comm=%s,SIG_ETX=%d\r\n", p->pid,p->comm,SIG_ETX);
		if (  strcmp(p->comm, "misc_app") == 0 )
		{
		    printk("find misc_app\n");
			find_misc_app_flag = 1;
		    break;
		}
	}

	/* online = 1: USB in */
	if (prop.intval == 1)
	{
	#if CHARGER_LED_MODE_PROTECT
		/* 发送异步通知给所有注册的进程 */
        if (g_async_queue && find_misc_app_flag) {
			info.si_code = USB_CHARGER_INSERTED;
			send_sig_info(SIGIO, &info, p);
            printk("fasync: sent SIGIO 1 signal\n");
        }
	#endif

		om_bat->battery_dat.charger_status = 1;
		dev_info(&psy->dev, "USB power supply is charger, battery_dat.charger_status = %d\n", om_bat->battery_dat.charger_status);
	}
	else if (prop.intval == 0)
	{
	#if CHARGER_LED_MODE_PROTECT
		/* 发送异步通知给所有注册的进程 */
        if (g_async_queue && find_misc_app_flag) {
			info.si_code = USB_CHARGER_REMOVED;
			send_sig_info(SIGIO, &info, p);
            printk("fasync: sent SIGIO 0 signal\n");
        }
	#endif

		om_bat->battery_dat.charger_status = 0;
		dev_info(&psy->dev, "USB power supply is not charger,  battery_dat.charger_status = %d\n", om_bat->battery_dat.charger_status);
	}
#endif
    
    return NOTIFY_OK;
}

/* xbotgo add */
static int i2c_battery_open(struct inode *inode, struct file *file)
{
	struct cdev *cdevp = inode->i_cdev;
	struct battery_dev_data *battery_dev = container_of(cdevp, struct battery_dev_data, cdev);

	file->private_data = battery_dev;
	
	return 0;
}

static int bat_get_vbus_status(struct om_battery *om_bat)
{
	int vbus_online = om_bat->vbus_gpiod ?
						gpiod_get_value_cansleep(om_bat->vbus_gpiod) : -1;

	return (1 == vbus_online) ? 1 : 0;
}

static int bat_get_charge_status(struct om_battery *om_bat)
{
	int ret = 0;
	int vbus_online = bat_get_vbus_status(om_bat);
	int charge_status = 0;
	unsigned short old_addr = 0;
	unsigned char dat = 0;

	static int pre_vbus_online = -1;
	static int pre_charge_status = -1;
	static int pre_stop_charger_flag = false;

	if(1 == vbus_online)
	{
		mutex_lock(&(om_bat->battery_dev.i2c_lock));

		old_addr = om_bat->client->addr;
		om_bat->client->addr = IP2315_SLAVE_ID;
		dat = 0;
		ret = i2c_smbus_read_i2c_block_data(om_bat->client, 0x01, 1, &dat);
		om_bat->client->addr = OM7X0X0_SLAVE_ID;
		if(ret > 0)
		{
			if(0x01 == dat)
				charge_status = 1;
		}

		if(old_addr != OM7X0X0_SLAVE_ID)
		{
			om_printk("i2c addr error: old_addr=0x%02X\n", old_addr);
		}

		mutex_unlock(&(om_bat->battery_dev.i2c_lock));
	}

	if(vbus_online != pre_vbus_online || charge_status != pre_charge_status || pre_stop_charger_flag != om_bat->stop_charger_flag)
	{
		om_printk("%s: %d %d %d\n", __func__, vbus_online, charge_status, om_bat->stop_charger_flag);
		pre_vbus_online = vbus_online;
		pre_charge_status = charge_status;
		pre_stop_charger_flag = om_bat->stop_charger_flag;
	}

	return charge_status;
}

/* xbotgo add */
static int i2c_battery_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct battery_dev_data *battery_dev = file->private_data;
	void __user *argp = (void __user *)arg;
	struct battery_data *battery_dat = &battery_dev->om_bat->battery_dat;
	struct om_battery *om_bat = battery_dev->om_bat;


    if(_IOC_TYPE(cmd) != I2CBAT_BASE)
	{
		om_printk_err("i2c ioctl cmd error\n");
        return -ENOTTY;
	}

	switch(cmd)
	{
		case I2CBAT_GET_STATUS:
			battery_dat->capacity = om_bat->soc;
			battery_dat->present  = om_bat->voltage <= 0 ? 0 : 1;
			battery_dat->voltage_now = om_bat->voltage * VOL_UNIT;
			battery_dat->technology = "Li-ion";
			battery_dat->temp = om_bat->temp;
			battery_dat->charge_full = om_bat->design_capacity * CAPACITY_UNIT;
			battery_dat->charge_counter = CHARGE_COUNTER;
			battery_dat->capacity_level = om_bat->soc > 0 ? 1 : 0;
			battery_dat->current_now = om_bat->om_current;
			battery_dat->health = POWER_SUPPLY_HEALTH_GOOD;
			battery_dat->cycle_count = om_bat->cycle;
			battery_dat->charger_status = bat_get_charge_status(om_bat);

			if (copy_to_user(argp, battery_dat, sizeof(struct battery_data)))
				return -EFAULT;
			break;
		case I2CBAT_GET_VBUS_STATUS:
			{
				int vbus_online = bat_get_vbus_status(om_bat);

				//om_printk("get vbus status %d\n", vbus_online);
				if (copy_to_user(argp, &vbus_online, sizeof(int))) {
					om_printk_err("ioctl I2CBAT_GET_VBUS_STATUS failed\n");
					return -EFAULT;
				}
			}
			break;
		case I2CBAT_SET_CHARGE_STATUS:
			{
				int val = 0;
				int vbus_online = bat_get_vbus_status(om_bat);

				if(vbus_online)
				{
					if (copy_from_user(&val, argp, sizeof(int))) {
						om_printk_err("ioctl I2CBAT_SET_CHARGE_STATUS failed\n");
						return -EFAULT;
					}

					om_printk("set charge status to %d\n", val);
					if(val)
					{
						om_bat->stop_charger_flag = false;
					}
					else
					{
						om_bat->stop_charger_flag = true;
					}

					enable_charger(om_bat, val);
				}
			}
			break;
		case I2CBAT_GET_CHARGE_STATUS:
			{
				int charge_status = bat_get_charge_status(om_bat);

				//om_printk("get charge status %d\n", charge_status);
				if (copy_to_user(argp, &charge_status, sizeof(int))) {
					om_printk_err("ioctl I2CBAT_GET_CHARGE_STATUS failed\n");
					return -EFAULT;
				}
			}
			break;
		case I2CBAT_GET_MODEL_NAME:
			{
				if (copy_to_user(argp, &model_name, sizeof(model_name))) {
					om_printk_err("ioctl I2CBAT_GET_CHARGE_STATUS failed\n");
					return -EFAULT;
				}
			}
			break;
		default:
			break;
	}



	return 0;
}

/* xbotgo add */
static long i2c_battery_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct battery_dev_data *battery_dev = file->private_data;
	int ret;

	mutex_lock(&battery_dev->ibe_lock);
	ret = i2c_battery_ioctl(file, cmd, arg);
	mutex_unlock(&battery_dev->ibe_lock);

	return ret;
}

#if CHARGER_LED_MODE_PROTECT
/* fasync函数实现 */
static int fasync_dev_fasync(int fd, struct file *filp, int on)
{
    //struct fasync_dev *dev = filp->private_data;
    return fasync_helper(fd, filp, on, &g_async_queue);
}

/* 释放函数 */
static int fasync_dev_release(struct inode *inode, struct file *filp)
{
    /* 将文件从异步通知列表中删除 */
    fasync_dev_fasync(-1, filp, 0);
    return 0;
}
#endif

/* xbotgo add */
static const struct file_operations i2c_battery_fops = {
    .owner 			= THIS_MODULE,
	.open 			= i2c_battery_open,
	.unlocked_ioctl = i2c_battery_unlocked_ioctl,
#if CHARGER_LED_MODE_PROTECT
	.release		= fasync_dev_release,
	.fasync			= fasync_dev_fasync,
#endif
};



static int om70X0X_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	om_printk("om70X0X driver version: %s\n", OM_DRV_VER);
	int ret;
	int loop = 0;
	struct om_battery *om_bat;
	
#ifdef OM_PROPERTIES
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg = {0};
#endif
#endif
	struct power_supply *usb_charge_psy;
	union power_supply_propval val;
	BATTERY_TYPE_E bat_type = SS_BATTERY_TYPE;

	om_printk("\n");

	if(UNKNOWN_BATTERY_TYPE == battery_profile_type)
	{
		bat_type = read_battery_config();
		if(RD_BATTERY_TYPE == bat_type)
			battery_profile_type = RD_BATTERY_TYPE;
		else
			battery_profile_type = SS_BATTERY_TYPE;
	}

	om_bat = devm_kzalloc(&client->dev, sizeof(*om_bat), GFP_KERNEL);
	if (!om_bat) 
	{
		om_printk_err("om_bat create fail!\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, om_bat);
	om_bat->client = client;

#if CONFIG_VBUS_CONN_GPIO
	om_bat->vbus_gpiod = devm_gpiod_get_optional(&client->dev, "vbus", GPIOD_IN);
	if (IS_ERR(om_bat->vbus_gpiod))
		return PTR_ERR(om_bat->vbus_gpiod);

	if (!om_bat->vbus_gpiod) {
		dev_err(&client->dev, "failed to get gpios\n");
		return -ENODEV;
	}
#endif

	ret = om70X0X_init(om_bat);
	while ((loop++ < OM_RETRY_COUNT) && (ret != 0))
	{
		msleep(OM_SLEEP_200MS);
		ret = _om70X0X_software_reset(om_bat);
	}

	if (ret) 
	{
		om_printk_err("om70X0X init fail!\n");

		//enable charger
		if(disable_charger_for_new_profile)
		{
			enable_charger(om_bat, 1);
			disable_charger_for_new_profile = 0;
		}

		return ret;
	}

	om_printk("sleep 100ms\n");
	msleep(OM_SLEEP_100MS);	//soc data will be ready after 70ms~100ms
	
	//enable charger
	if(disable_charger_for_new_profile)
	{
		enable_charger(om_bat, 1);
		disable_charger_for_new_profile = 0;
	}
	om_printk("flag 0\n");

	// check and handle chip anomaly
	ret = om70X0X_get_vol(om_bat);
	if (ret < 0) 
	{
		om_printk_err("iic read error when get vol\n");
		return ret;
	}
	ret = _om70X0X_check_and_handle_anomaly(om_bat);
	if(ret < 0)
	{
		om_printk_err("chip is in anomaly state, probe fail\n");
		return ret;
	}

	ret = om_init_data(om_bat);
	if (ret) 
	{
		om_printk_err("om70X0X init data fail!\n");
		return ret;
	}
	om_printk("flag 1\n");

#ifdef OM_PROPERTIES
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
	om_bat->om_bat.name = OM_PROPERTIES;
	om_bat->om_bat.type = POWER_SUPPLY_TYPE_BATTERY;
	om_bat->om_bat.properties = om_battery_properties;
	om_bat->om_bat.num_properties = ARRAY_SIZE(om_battery_properties);
	om_bat->om_bat.get_property = om_battery_get_property;
	om_bat->om_bat.set_property = om_battery_set_property;
	ret = power_supply_register(&client->dev, &om_bat->om_bat);
	if (ret < 0) {
		power_supply_unregister(&om_bat->om_bat);
		return ret;
	}
#else
	psy_desc = devm_kzalloc(&client->dev, sizeof(*psy_desc), GFP_KERNEL);
	if (!psy_desc)
	{
		om_printk_err("psy_desc is NULL\n");
		return -ENOMEM;
	}
		
	psy_cfg.drv_data = om_bat;
	psy_desc->name = OM_PROPERTIES;
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->properties = om_battery_properties;
	psy_desc->num_properties = ARRAY_SIZE(om_battery_properties);
	psy_desc->get_property = om_battery_get_property;
	psy_desc->set_property = om_battery_set_property;
	om_bat->om_bat = power_supply_register(&client->dev, psy_desc, &psy_cfg);
	if (IS_ERR(om_bat->om_bat)) 
	{
		ret = PTR_ERR(om_bat->om_bat);
		om_printk_err("failed to register battery: %d\n", ret);
		return ret;
	}
	om_printk("flag 2\n");
#endif
#endif

	mutex_init(&om_bat->battery_dev.ibe_lock);
	mutex_init(&om_bat->battery_dev.i2c_lock);

	om_bat->omfg_workqueue = create_singlethread_workqueue("omfg_gauge");
	INIT_DELAYED_WORK(&om_bat->battery_delay_work, om_bat_work);
	queue_delayed_work(om_bat->omfg_workqueue, &om_bat->battery_delay_work , msecs_to_jiffies(queue_start_work_time));

/* xbotgo add
	* 监控某个电源事件*/
#if CONFIG_VBUS_CONN_GPIO
	(void)usb_charger_notifier_call;
#else
	om_bat->psy_nb.notifier_call = usb_charger_notifier_call;
	ret = power_supply_reg_notifier(&om_bat->psy_nb);
	if (ret)
	{
		dev_err(&client->dev, "register power supply notifier fail, ret = 0x%x\n", ret);
		return ret;
	}
#endif
	i2c_set_clientdata(client, om_bat);
	
	om_bat->battery_dev.name = OM_PROPERTIES;
	om_bat->battery_dev.om_bat = om_bat;
	
	alloc_chrdev_region(&om_bat->battery_dev.devnu, 0, 32, om_bat->battery_dev.name);
	cdev_init(&om_bat->battery_dev.cdev, &i2c_battery_fops);
	cdev_add(&om_bat->battery_dev.cdev, om_bat->battery_dev.devnu, 256);

	om_bat->battery_dev.cdev_class = class_create(THIS_MODULE, om_bat->battery_dev.name);
	if(!device_create(om_bat->battery_dev.cdev_class, NULL, om_bat->battery_dev.devnu, NULL, om_bat->battery_dev.name))
	{
		dev_err(&client->dev, "create device fail\n");
		return -ENODEV;
	}
#if CONFIG_VBUS_CONN_GPIO
	om_bat->stop_charger_flag = false;
#else
	om_bat->low_temp_num = 0;
	om_bat->high_temp_num = 0;
	om_bat->low_temp_stop_charger_flag = false;
	om_bat->high_temp_stop_charger_flag = false;
#endif
	om_printk("flag 3\n");

#if CONFIG_VBUS_CONN_GPIO
	(void)usb_charge_psy;
	(void)val;
#else
	usb_charge_psy = power_supply_get_by_name("usb-charger");
	if (!usb_charge_psy) {
		 pr_err("USB charger power supply not found\n");
		 return -ENODEV;
	}

	ret = power_supply_get_property(usb_charge_psy, POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret) {
		 pr_err("Failed to get online status: %d\n", ret);
		 power_supply_put(usb_charge_psy);
		 return ret;
	}
	om_bat->battery_dat.charger_status = val.intval;
	power_supply_put(usb_charge_psy);
	om_printk("flag 4\n");
#endif

    /* xbotgo add */
#if 0
	old_addr = client->addr;
	client->addr = 0x75;
	ret = i2c_smbus_read_i2c_block_data(client, 0x01, 1, &read_dat);
	printk("i2c_smbus_read_i2c_block_data read_dat = 0x%x, ret = %d\n", read_dat, ret);

	client->addr = old_addr;
#endif


	om_printk("om70X0X driver probe success!\n");

	return 0;
}

static void om70X0X_remove(struct i2c_client *client)	 
{
    /* Add log printing to help locate /sys/class/power_supply node loss */
	struct om_battery *om_bat = i2c_get_clientdata(client);
	int bus_nr = (client && client->adapter) ? (client->adapter->nr) : -1;

	dev_warn(&client->dev,
		"om70X0X_remove(): called. bus=%d addr=0x%02x dev=%s om_bat=%p\n",
		bus_nr, client ? client->addr : -1, dev_name(&client->dev), om_bat);

	if (!om_bat) {
		dev_err(&client->dev, "om70X0X_remove(): om_bat is NULL, nothing to cleanup\n");
		return;
	}

	dev_info(&client->dev, "remove: cancel delayed work=%p\n", &om_bat->battery_delay_work);
	cancel_delayed_work_sync(&om_bat->battery_delay_work);

	dev_info(&client->dev, "remove: unregister power_supply notifier=%p\n", &om_bat->psy_nb);
	power_supply_unreg_notifier(&om_bat->psy_nb);

	dev_info(&client->dev, "remove: unregister power_supply=%p\n", om_bat->om_bat);
	power_supply_unregister(om_bat->om_bat);

	dev_info(&client->dev, "remove: cdev_del cdev=%p\n", &om_bat->battery_dev.cdev);
	cdev_del(&om_bat->battery_dev.cdev);

	dev_info(&client->dev, "remove: device_destroy class=%p dev=0x%x\n",
			 om_bat->battery_dev.cdev_class, om_bat->battery_dev.devnu);
	device_destroy(om_bat->battery_dev.cdev_class, om_bat->battery_dev.devnu);

	dev_info(&client->dev, "remove: class_destroy class=%p\n", om_bat->battery_dev.cdev_class);
	class_destroy(om_bat->battery_dev.cdev_class);

	dev_info(&client->dev, "remove: unregister_chrdev_region dev=0x%x count=%d\n",
			 om_bat->battery_dev.devnu, 32);
	unregister_chrdev_region(om_bat->battery_dev.devnu, 32);

	dev_warn(&client->dev, "om70X0X_remove(): done.\n");
}

#ifdef CONFIG_PM
static int om_bat_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct om_battery *om_bat = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&om_bat->battery_delay_work);
	return 0;
}

static int om_bat_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct om_battery *om_bat = i2c_get_clientdata(client);

	queue_delayed_work(om_bat->omfg_workqueue, &om_bat->battery_delay_work, msecs_to_jiffies(20));
	return 0;
}

static const struct dev_pm_ops om_bat_pm_ops = {
	.suspend  = om_bat_suspend,
	.resume   = om_bat_resume,
};
#endif

static const struct i2c_device_id om70X0X_id_table[] = {
	{ OMFG_NAME, 0 },
	{ }
};

static struct of_device_id om70X0X_match_table[] = {
	{ .compatible = "onmicro,om70X0X", },
	{ },
};

static struct i2c_driver om70X0X_driver = {
	.driver   = {
		.name = OMFG_NAME,
#ifdef CONFIG_PM
		.pm = &om_bat_pm_ops,
#endif
		.owner = THIS_MODULE,
		.of_match_table = om70X0X_match_table,
	},
	.probe = om70X0X_probe,
	.remove = om70X0X_remove,
	.id_table = om70X0X_id_table,
};



static int __init om70X0X_module_init(void)
{
	om_printk("\n");
	i2c_add_driver(&om70X0X_driver);
	return 0; 
}

static void __exit om70X0X_module_exit(void)
{
	i2c_del_driver(&om70X0X_driver);
}

module_init(om70X0X_module_init);
module_exit(om70X0X_module_exit);

module_param(battery_profile_type, int, 0644);
MODULE_PARM_DESC(battery_profile_type, "Battery profile type (1,2,3,4,5,6)");

module_param(reinstall_module_flag, int, 0644);
MODULE_PARM_DESC(reinstall_module_flag, "Force write profile and active on init if set to 1");

MODULE_AUTHOR("ONMICRO MIX SOFTWARE");
MODULE_DESCRIPTION("OM70X0X Device Driver V1.43");
MODULE_LICENSE("GPL");
