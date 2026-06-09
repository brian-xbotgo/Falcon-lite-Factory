#ifndef __SPI_MOTOR__
#define	__SPI_MOTOR__

#define MOTOR_DRIVER_VERSION	"1.0.2"
#define SPI_MOTOR_NAME 			"tmi8152"


#define	CMD_READID0	0x00
#define	CMD_READID1	0x01

//#define	FAULT_FREQ_HZ 21875
#define	FAULT_FREQ_HZ 13672 //硬件固化


//register
#define		CHIP_ID0			0x00
#define		CHIP_ID1			0x01
#define		GCTRL				0x02
#define		Reserved			0x03
#define		CH12_CTRL			0x04
#define		CH12_SET			0x05
#define		CH12_CLOCK_CTRL		0x06
#define		CH12_PHASE_CNTL		0x07
#define		CH12_CYCLE_CNTL		0x08
#define		CH12_CYCLE_CNTH		0x09
#define		CH12_PHASE_SETL		0x0a
#define		CH12_CYCLE_SETL		0x0b
#define		CH12_CYCLE_SETH		0x0c
#define		CH1_PW_SETL			0x0d
#define		CH2_PW_SETL			0x0e
#define		CH12_PHASE_HIGH		0x0f
#define		CH12_PW_SETH		0x10
#define		IRCUT_CTRL			0x11
#define		CH34_PHASE_HIGH		0x12
#define		CH34_PW_SETH		0x13
#define		CH34_CTRL			0x14
#define		CH34_SET 			0x15
#define		CH34_CLOCK_CTRL		0x16
#define		CH34_PHASE_CNTL		0x17
#define		CH34_CYCLE_CNTL		0x18
#define		CH34_CYCLE_CNTH		0x19
#define		CH34_PHASE_SETL		0x1a
#define		CH34_CYCLE_SETL		0x1b
#define		CH34_CYCLE_SETH		0x1c
#define		CH3_PW_SETL			0x1d
#define		CH4_PW_SETL			0x1e
#define		PWMSET				0x1f

#define		SPI_WRITE_CMD	(1 << 7)
#define		SPI_MOTOR_SYSTEM_CLOCK	28000000
#define		MOTOR_CYCLES_MAX	8192

#define		EN_CH	(1 << 7)

//GCTRL
#define		GRESET 		(1 << 7)
#define 	STANBY 		(1 << 6)
#define 	EN_DIS 		(1 << 5)
#define 	TFS 		(1 << 4)
#define 	GENABLE 	(1 << 3)
#define		LOCK_CNT 	(1 << 1)
#define		CLK_RDY 	(1 << 0)


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

// 电机速度档位枚举，用于索引motor_speed_config_list数组
// 命名格式：MOTOR_SPEED_IDX_SPEED，IDX为索引值，SPEED为速度值(deg/s)
enum motor_speed_gear {
	MOTOR_SPEED_0_111_0 = 0,  // 111.071 deg/s
	MOTOR_SPEED_1_94_3,       // 94.348 deg/s
	MOTOR_SPEED_2_81_2,       // 81.277 deg/s
	MOTOR_SPEED_3_76_1,       // 76.117 deg/s
	MOTOR_SPEED_4_71_8,       // 71.830 deg/s
	MOTOR_SPEED_5_58_1,       // 58.192 deg/s
	MOTOR_SPEED_6_55_4,       // 55.498 deg/s
	MOTOR_SPEED_7_48_8,       // 48.839 deg/s
	MOTOR_SPEED_8_41_9,       // 41.975 deg/s
	MOTOR_SPEED_9_39_1,       // 39.196 deg/s
	MOTOR_SPEED_10_36_8,      // 36.819 deg/s
	MOTOR_SPEED_11_33_7,      // 33.788 deg/s
	MOTOR_SPEED_12_29_7,      // 29.707 deg/s
	MOTOR_SPEED_13_28_2,      // 28.240 deg/s
	MOTOR_SPEED_14_24_7,      // 24.775 deg/s
	MOTOR_SPEED_15_21_3,      // 21.321 deg/s
	MOTOR_SPEED_16_19_9,      // 19.937 deg/s
	MOTOR_SPEED_17_18_7,      // 18.713 deg/s
	MOTOR_SPEED_18_17_1,      // 17.128 deg/s
	MOTOR_SPEED_19_14_9,      // 14.992 deg/s
	MOTOR_SPEED_20_14_2,      // 14.281 deg/s
	MOTOR_SPEED_21_12_5,      // 12.524 deg/s
	MOTOR_SPEED_22_10_7,      // 10.745 deg/s
	MOTOR_SPEED_23_10_0,      // 10.044 deg/s
	MOTOR_SPEED_24_08_6,      // 8.618 deg/s
	MOTOR_SPEED_25_07_5,      // 7.545 deg/s
	MOTOR_SPEED_26_07_1,      // 7.190 deg/s
	MOTOR_SPEED_27_06_2,      // 6.290 deg/s
	MOTOR_SPEED_28_05_3,      // 5.399 deg/s
	MOTOR_SPEED_29_05_0,      // 5.038 deg/s
	MOTOR_SPEED_30_04_3,      // 4.320 deg/s
	MOTOR_SPEED_31_03_7,      // 3.782 deg/s
	MOTOR_SPEED_00,           // 电机停止的速度
	MOTOR_SPEED_MAX           // 速度档位总数
};

#if 0
enum divide_select{
	DIVIDE1  = 0,
	DIVIDE2  = 1,/*5秒跑360度，角速度为72度每秒*/
	DIVIDE3  = 2,/*7秒跑360度，角速度为51.43度每秒*/
	DIVIDE4  = 3,/*10秒跑360度，角速度为36度每秒*/
	DIVIDE5  = 4,/*12秒跑360度，角速度为30度每秒*/
	DIVIDE6  = 5,/*14秒跑360度，角速度为25.71度每秒*/
	DIVIDE7  = 6,/*16.5秒跑360度，角速度为21.82度每秒*/
	DIVIDE8  = 7,/*19秒跑360度，角速度为18.95度每秒*/
	DIVIDE9  = 8,/*21秒跑360度，角速度为17.14度每秒*/
	DIVIDE10  = 9,/*23.5秒跑360度，角速度为15.32度每秒*/
	DIVIDE11  = 10,/*26秒跑360度，角速度为13.85度每秒*/
	DIVIDE12  = 11,/*28秒跑360度，角速度为12.85度每秒*/
	DIVIDE13  = 12,/*31秒跑360度，角速度为11.62度每秒*/
	DIVIDE14  = 13,/*33秒跑360度，角速度为10.91度每秒*/
	DIVIDE15  = 14,/*36秒跑360度，角速度为10.00度每秒*/
	DIVIDE16  = 15,/*38秒跑360度，角速度为9.47度每秒*/
	DIVIDE17  = 16,/*40秒跑360度，角速度为9.00度每秒*/
	DIVIDE18  = 17,/*43秒跑360度，角速度为8.37度每秒*/
	DIVIDE19  = 18,/*45秒跑360度，角速度为8.00度每秒*/
	DIVIDE20  = 19,/*48秒跑360度，角速度为7.50度每秒*/
	DIVIDE21  = 20,/*50秒跑360度，角速度为7.20度每秒*/
	DIVIDE22  = 21,/*52秒跑360度，角速度为6.92度每秒*/
	DIVIDE23  = 22,/*54.5秒跑360度，角速度为6.60度每秒*/
	DIVIDE24  = 23,/*57.5秒跑360度，角速度为6.26度每秒*/
	DIVIDE25  = 24,/*60.0秒跑360度，角速度为6.00度每秒*/
	DIVIDE26  = 25,/*62.0秒跑360度，角速度为5.81度每秒*/
	DIVIDE27  = 26,/*64.0秒跑360度，角速度为5.62度每秒*/
	DIVIDE28  = 27,/*66.0秒跑360度，角速度为5.45度每秒*/
	DIVIDE29  = 28,/*68.0秒跑360度，角速度为5.29度每秒*/
	DIVIDE30  = 29,/*71.0秒跑360度，角速度为5.07度每秒*/
	DIVIDE31  = 30,/*73.0秒跑360度，角速度为4.93度每秒*/
	DIVIDE32  = 31,/*75.5秒跑360度，角速度为4.77度每秒*/
	NOT_DIV  = -1,
};


enum divide_select{
	DIVIDE1  = 0, /*这个无法使用*/
	SPEED_72_00 = 1,/*5秒跑360度，角速度为72度每秒*/
	SPEED_51_43 = 2,/*7秒跑360度，角速度为51.43度每秒*/
	SPEED_36_00 = 3,/*10秒跑360度，角速度为36度每秒*/
	SPEED_30_00 = 4,/*12秒跑360度，角速度为30度每秒*/
	SPEED_25_71 = 5,/*14秒跑360度，角速度为25.71度每秒*/
	SPEED_21_82 = 6,/*16.5秒跑360度，角速度为21.82度每秒*/
	SPEED_18_95 = 7,/*19秒跑360度，角速度为18.95度每秒*/
	SPEED_17_14 = 8,/*21秒跑360度，角速度为17.14度每秒*/
	SPEED_15_32  = 9,/*23.5秒跑360度，角速度为15.32度每秒*/
	SPEED_13_85  = 10,/*26秒跑360度，角速度为13.85度每秒*/
	SPEED_12_85  = 11,/*28秒跑360度，角速度为12.85度每秒*/
	SPEED_11_62  = 12,/*31秒跑360度，角速度为11.62度每秒*/
	SPEED_10_91  = 13,/*33秒跑360度，角速度为10.91度每秒*/
	SPEED_10_00  = 14,/*36秒跑360度，角速度为10.00度每秒*/
	SPEED_09_47  = 15,/*38秒跑360度，角速度为9.47度每秒*/
	SPEED_09_00  = 16,/*40秒跑360度，角速度为9.00度每秒*/
	SPEED_08_37  = 17,/*43秒跑360度，角速度为8.37度每秒*/
	SPEED_08_00  = 18,/*45秒跑360度，角速度为8.00度每秒*/
	SPEED_07_50  = 19,/*48秒跑360度，角速度为7.50度每秒*/
	SPEED_07_20  = 20,/*50秒跑360度，角速度为7.20度每秒*/
	SPEED_06_92  = 21,/*52秒跑360度，角速度为6.92度每秒*/
	SPEED_06_60  = 22,/*54.5秒跑360度，角速度为6.60度每秒*/
	SPEED_06_26  = 23,/*57.5秒跑360度，角速度为6.26度每秒*/
	SPEED_06_00  = 24,/*60.0秒跑360度，角速度为6.00度每秒*/
	SPEED_05_81  = 25,/*62.0秒跑360度，角速度为5.81度每秒*/
	SPEED_05_62  = 26,/*64.0秒跑360度，角速度为5.62度每秒*/
	SPEED_05_45  = 27,/*66.0秒跑360度，角速度为5.45度每秒*/
	SPEED_05_29  = 28,/*68.0秒跑360度，角速度为5.29度每秒*/
	SPEED_05_07  = 29,/*71.0秒跑360度，角速度为5.07度每秒*/
	SPEED_04_93  = 30,/*73.0秒跑360度，角速度为4.93度每秒*/
	SPEED_04_77  = 31,/*75.5秒跑360度，角速度为4.77度每秒*/
	NOT_DIV  = -1,
};
#endif

enum divide_select{
	DIVIDE1  = 0, /*这个无法使用*/
	SPEED_72_00 = 1,/*5秒跑360度，角速度为72度每秒*/
	SPEED_51_43 = 2,/*7秒跑360度，角速度为51.43度每秒*/
	SPEED_36_00 = 3,/*10秒跑360度，角速度为36度每秒*/
	SPEED_30_00 = 4,/*12秒跑360度，角速度为30度每秒*/
	SPEED_25_71 = 5,/*14秒跑360度，角速度为25.71度每秒*/
	SPEED_21_82 = 6,/*16.5秒跑360度，角速度为21.82度每秒*/
	SPEED_18_95 = 7,/*19秒跑360度，角速度为18.95度每秒*/
	SPEED_17_14 = 8,/*21秒跑360度，角速度为17.14度每秒*/
	SPEED_15_32  = 9,/*23.5秒跑360度，角速度为15.32度每秒*/
	SPEED_13_85  = 10,/*26秒跑360度，角速度为13.85度每秒*/
	SPEED_12_85  = 11,/*28秒跑360度，角速度为12.85度每秒*/
	SPEED_11_62  = 12,/*31秒跑360度，角速度为11.62度每秒*/
	SPEED_10_91  = 13,/*33秒跑360度，角速度为10.91度每秒*/
	SPEED_10_00  = 14,/*36秒跑360度，角速度为10.00度每秒*/
	SPEED_09_47  = 15,/*38秒跑360度，角速度为9.47度每秒*/
	SPEED_09_00  = 16,/*40秒跑360度，角速度为9.00度每秒*/
	SPEED_08_37  = 17,/*43秒跑360度，角速度为8.37度每秒*/
	SPEED_08_00  = 18,/*45秒跑360度，角速度为8.00度每秒*/
	SPEED_07_50  = 19,/*48秒跑360度，角速度为7.50度每秒*/
	SPEED_07_20  = 20,/*50秒跑360度，角速度为7.20度每秒*/
	SPEED_06_92  = 21,/*52秒跑360度，角速度为6.92度每秒*/
	SPEED_06_60  = 22,/*54.5秒跑360度，角速度为6.60度每秒*/
	SPEED_06_26  = 23,/*57.5秒跑360度，角速度为6.26度每秒*/
	SPEED_06_00  = 24,/*60.0秒跑360度，角速度为6.00度每秒*/
	SPEED_05_81  = 25,/*62.0秒跑360度，角速度为5.81度每秒*/
	SPEED_05_62  = 26,/*64.0秒跑360度，角速度为5.62度每秒*/
	SPEED_05_45  = 27,/*66.0秒跑360度，角速度为5.45度每秒*/
	SPEED_05_29  = 28,/*68.0秒跑360度，角速度为5.29度每秒*/
	SPEED_05_07  = 29,/*71.0秒跑360度，角速度为5.07度每秒*/
	SPEED_04_93  = 30,/*73.0秒跑360度，角速度为4.93度每秒*/
	SPEED_04_77  = 31,/*75.5秒跑360度，角速度为4.77度每秒*/
	SPEED_00     = 32,/*电机停止的速度*/
	NOT_DIV  = -1,
};

typedef enum
{
	COMPENSATE_NONE,
	COMPENSATE_FORWARD,
	COMPENSATE_FORWARD_CHANGED,
	COMPENSATE_BACK,
	COMPENSATE_BACK_CHANGED
}COMPENSATE_STATUS;


enum direct_select{
	OUTPUT_OPEN_CHANNEL = 0,
	FORWARD = 5,
	BACK = 10,
	STOP = 0xf,
};


/*	 通道 12 工作模式控制
	 001：保留；
	 010： 1-2 相励磁模式；
	 011： 2-2 相励磁模式；
	 100： SPI 脉宽控制模式；
	 101：保留；
	 110：自动控制模式；
	 111：手动控制模式； */
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
#define WORK_MASK 0x7

struct spi_motor_dev {
	struct spi_device *spi;
	unsigned int major;
	const char * name;
	dev_t devnu;
	struct gpio_desc *motor_gpio;
	struct mutex start_lock; 
	struct mutex set_spe_lock; 
	struct mutex cycles_cnt_lock; 
	struct mutex stop_lock; 
	
	struct class *cdev_class;
	struct cdev cdev;
	u8 cur_speed[2];		//用于SET_SPEED->CHANGE_MOTOR_PARAM的调用场景下存储SET_SPEED中的speed参数， CHANGE_MOTOR_PARAM调用时，使用该参数来获取速度相关具体配置
};

typedef struct{	
	enum subdivide_select subdivide;
	u8 prediv;
	u8 div;
	u8 pwmset;
} motor_speed_config;

// 速度配置表项：包含速度档位枚举和对应的配置参数
typedef struct {
	enum motor_speed_gear gear;
	motor_speed_config config;
} motor_speed_entry;

//below for ioctrl
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

struct chx_speed {
	enum channel_select chx;
	// enum divide_select speed;
	enum motor_speed_gear speed;
}; 


struct chx_mode {
	enum channel_select chx;
	enum work_mode mode;
}; 

struct motor_status {
	u8 temp;
	u8 work_status;
};

struct get_angle {
	enum channel_select chx;
	u64 phase_done_toltal;
	u8 dir;/*1: 正方向，0：反方向*/
	COMPENSATE_STATUS compensate_status;
	u32 compensation_phases;/*实际的补偿相位数*/
}; 



struct get_targ {
	enum channel_select chx;
	u8 dir;/*1: 正方向，0：反方向*/
	u32 cycles_targ;
	u32 phase_targ;
	u64 phase_targ_toltal;
	COMPENSATE_STATUS compensate_status;
	u32 compensation_phases;/*实际的补偿相位数*/
};

// 虚位补偿配置结构体
struct backlash_config {
	enum channel_select chx;        // 通道
	u32 compensation_phases;        // 补偿相位数（可以用角度转换：角度 * 524288 / 360）
	u32 compensation_angle;         // 补偿角度
	bool enabled;                   // 是否启用
};

struct motor_data {
	int phase_align_base;
};

struct reg_read_request {
    u8 addr;      // 起始地址
    u8 len;       // 要读取的字节数
    u8 *data;     // 存储读取数据的缓冲区
};

struct reg_write_request {
    u8 addr;      // 起始地址
    u8 len;       // 要写入的字节数
    u8 data[64];  // 固定大小数组，最大支持64字节
};

struct get_cycle_phase {
    enum channel_select chx;    // 通道选择
    u32 cycles_cnt;            // 当前 cycles 寄存器值
    u32 phase_cnt;             // 当前 phase 寄存器值
};

#define SPIMIOC_BASE  	     'S'
#define CHAN_START      _IOW (SPIMIOC_BASE, 0, struct chx_enable*)
#define CHAN_STOP       _IOW (SPIMIOC_BASE, 1, enum channel_select*)
#define CHANGE_WORKING_PARAM  _IOW (SPIMIOC_BASE, 2, struct chx_enable*)
#define SET_MODE		_IOW (SPIMIOC_BASE, 3, struct chx_mode*)
#define	ENTER_RESET   	_IO (SPIMIOC_BASE,  4)
#define	EXIT_RESET   	_IO (SPIMIOC_BASE,  5)
#define ENTER_STANBY    _IO (SPIMIOC_BASE,  6)
#define EXIT_STANBY     _IO (SPIMIOC_BASE,  7)
#define POWER_PINC      _IO (SPIMIOC_BASE,  8)
#define NPOWER_PINC     _IO (SPIMIOC_BASE,  9)
#define GET_STATUS      _IOR (SPIMIOC_BASE, 10, struct motor_status*)
#define DISABLE_CH12	_IO (SPIMIOC_BASE,  11)
#define DISABLE_CH34	_IO (SPIMIOC_BASE,  12)
#define CLOSE_CHIP		_IO (SPIMIOC_BASE,  13)
#define SET_SPEED		_IOW (SPIMIOC_BASE, 14, struct chx_speed*)
#define GET_CYCLE_CNT   _IOW (SPIMIOC_BASE, 15, enum channel_select*)
#define CLR_CYCLE_CNT   _IOW (SPIMIOC_BASE, 16, enum channel_select*)
#define GET_TARG_CNT    _IOW (SPIMIOC_BASE, 17, enum channel_select*)
#define SET_CENTER      _IOW (SPIMIOC_BASE, 18, enum channel_select*)
#define SET_BACKLASH_COMPENSATION _IOW (SPIMIOC_BASE, 19, struct backlash_config*)
#define GET_BACKLASH_COMPENSATION _IOWR (SPIMIOC_BASE, 20, struct backlash_config*)
#define READ_REGISTERS  _IOWR (SPIMIOC_BASE, 21, struct reg_read_request)
#define WRITE_REGISTERS _IOW (SPIMIOC_BASE, 22, struct reg_write_request)
#define GET_CYCLE_PHASE _IOWR (SPIMIOC_BASE, 23, struct get_cycle_phase)
#define CHAN_START_TEST _IOW (SPIMIOC_BASE, 24, struct chx_enable*)

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif


#define	DEFAULT_SUBDIV	SUBDIVIDE128
#define	DEFAULT_DIVSEL	SPEED_13_85


#define ANGLE_CONVERT_PHASE	1456356ULL
#define CYCLES_CONVERT_PHASE 1024
#define DEFAULT_ANGLE_VAL 2880000
#define DEFAULT_PHASE_VAL 4194304
#define DEFAULT_CNT_VAL 4096

#define	PHASE_COST 4370  //切换方向时要补偿3度，大概是4370个相位


#define TMI8152_DEBUG

#ifdef	TMI8152_DEBUG
#define printk_tmi8152 printk
#else
#define printk_tmi8152(...) do {} while (0)
#endif



#endif

