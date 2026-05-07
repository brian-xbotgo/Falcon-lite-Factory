#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>	/* for -EBUSY */
#include <linux/cdev.h>
#include <linux/slab.h>		/* for container_of */
#include <linux/compat.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/byteorder/generic.h>
#include <linux/pinctrl/consumer.h>

#include "spi_motor_drv.h"



// 水平电机速度配置表，每个条目包含速度档位和对应的配置参数
const motor_speed_entry motor_speed_table_ch12[] = {
	/*gear                       subdivide       prediv  div  pwmset*/
	{MOTOR_SPEED_0_111_0,  {SUBDIVIDE32,  0, 4, 0}},  // 111.071 deg/s
	{MOTOR_SPEED_1_94_3,   {SUBDIVIDE64,  0, 2, 0}},  // 94.348 deg/s
	{MOTOR_SPEED_2_81_2,   {SUBDIVIDE32,  0, 6, 0}},  // 81.277 deg/s
	{MOTOR_SPEED_3_76_1,   {SUBDIVIDE16,  0, 4, 2}},  // 76.117 deg/s
	{MOTOR_SPEED_4_71_8,   {SUBDIVIDE128, 1, 0, 0}},  // 71.830 deg/s
	{MOTOR_SPEED_5_58_1,   {SUBDIVIDE64,  0, 4, 0}},  // 58.192 deg/s
	{MOTOR_SPEED_6_55_4,   {SUBDIVIDE16,  0, 6, 2}},  // 55.498 deg/s
	{MOTOR_SPEED_7_48_8,   {SUBDIVIDE128, 0, 2, 0}},  // 48.839 deg/s
	{MOTOR_SPEED_8_41_9,   {SUBDIVIDE64,  0, 6, 0}},  // 41.975 deg/s
	{MOTOR_SPEED_9_39_1,   {SUBDIVIDE32,  0, 4, 2}},  // 39.196 deg/s
	{MOTOR_SPEED_10_36_8,  {SUBDIVIDE128, 2, 0, 1}},  // 36.819 deg/s
	{MOTOR_SPEED_11_33_7,  {SUBDIVIDE16,  0, 6, 4}},  // 33.788 deg/s
	{MOTOR_SPEED_12_29_7,  {SUBDIVIDE128, 0, 4, 4}},  // 29.707 deg/s
	{MOTOR_SPEED_13_28_2,  {SUBDIVIDE32,  0, 6, 2}},  // 28.240 deg/s
	{MOTOR_SPEED_14_24_7,  {SUBDIVIDE128, 1, 2, 0}},  // 24.775 deg/s
	{MOTOR_SPEED_15_21_3,  {SUBDIVIDE128, 0, 6, 0}},  // 21.321 deg/s
	{MOTOR_SPEED_16_19_9,  {SUBDIVIDE64,  0, 2, 4}},  // 19.937 deg/s
	{MOTOR_SPEED_17_18_7,  {SUBDIVIDE128, 2, 1, 3}},  // 18.713 deg/s
	{MOTOR_SPEED_18_17_1,  {SUBDIVIDE32,  0, 6, 4}},  // 17.128 deg/s
	{MOTOR_SPEED_19_14_9,  {SUBDIVIDE128, 0, 1, 4}},  // 14.992 deg/s
	{MOTOR_SPEED_20_14_2,  {SUBDIVIDE64,  0, 6, 2}},  // 14.281 deg/s
	{MOTOR_SPEED_21_12_5,  {SUBDIVIDE128, 0, 2, 3}},  // 12.524 deg/s
	{MOTOR_SPEED_22_10_7,  {SUBDIVIDE64,  0, 6, 3}},  // 10.745 deg/s
	{MOTOR_SPEED_23_10_0,  {SUBDIVIDE64,  1, 2, 4}},  // 10.044 deg/s
	{MOTOR_SPEED_24_08_6,  {SUBDIVIDE64,  0, 6, 4}},  // 8.618 deg/s
	{MOTOR_SPEED_25_07_5,  {SUBDIVIDE64,  2, 1, 4}},  // 7.545 deg/s
	{MOTOR_SPEED_26_07_1,  {SUBDIVIDE128, 0, 6, 2}},  // 7.190 deg/s
	{MOTOR_SPEED_27_06_2,  {SUBDIVIDE128, 1, 3, 2}},  // 6.290 deg/s
	{MOTOR_SPEED_28_05_3,  {SUBDIVIDE128, 0, 6, 3}},  // 5.399 deg/s
	{MOTOR_SPEED_29_05_0,  {SUBDIVIDE128, 0, 5, 4}},  // 5.038 deg/s
	{MOTOR_SPEED_30_04_3,  {SUBDIVIDE128, 0, 6, 4}},  // 4.320 deg/s
	{MOTOR_SPEED_31_03_7,  {SUBDIVIDE128, 1, 3, 4}},  // 3.782 deg/s
	{MOTOR_SPEED_00,       {SUBDIVIDE128, 0, 0, 0}},  // 电机停止
};

// 垂直电机速度配置表，每个条目包含速度档位和对应的配置参数
const motor_speed_entry motor_speed_table_ch34[] = {
	/*gear                       subdivide       prediv  div  pwmset*/
	{MOTOR_SPEED_0_111_0,  {SUBDIVIDE32,  0, 4, 0}},  // 111.071 deg/s
	{MOTOR_SPEED_1_94_3,   {SUBDIVIDE64,  0, 2, 0}},  // 94.348 deg/s
	{MOTOR_SPEED_4_71_8,   {SUBDIVIDE128, 1, 0, 0}},  // 71.830 deg/s
	{MOTOR_SPEED_5_58_1,   {SUBDIVIDE64,  0, 4, 0}},  // 58.192 deg/s
	{MOTOR_SPEED_7_48_8,   {SUBDIVIDE128, 0, 2, 0}},  // 48.839 deg/s
	{MOTOR_SPEED_9_39_1,   {SUBDIVIDE32,  0, 4, 2}},  // 39.196 deg/s
	{MOTOR_SPEED_10_36_8,  {SUBDIVIDE128, 2, 0, 1}},  // 36.819 deg/s
	{MOTOR_SPEED_12_29_7,  {SUBDIVIDE128, 0, 4, 4}},  // 29.707 deg/s
	{MOTOR_SPEED_14_24_7,  {SUBDIVIDE128, 1, 2, 0}},  // 24.775 deg/s
	{MOTOR_SPEED_16_19_9,  {SUBDIVIDE64,  0, 2, 4}},  // 19.937 deg/s
	{MOTOR_SPEED_19_14_9,  {SUBDIVIDE128, 0, 1, 4}},  // 14.992 deg/s
	{MOTOR_SPEED_20_14_2,  {SUBDIVIDE128, 1, 0, 4}},  // 14.281 deg/s
	{MOTOR_SPEED_21_12_5,  {SUBDIVIDE128, 0, 2, 3}},  // 12.524 deg/s
	{MOTOR_SPEED_23_10_0,  {SUBDIVIDE64,  1, 2, 4}},  // 10.044 deg/s
	{MOTOR_SPEED_29_05_0,  {SUBDIVIDE128, 0, 5, 4}},  // 5.038 deg/s
	{MOTOR_SPEED_00,       {SUBDIVIDE128, 0, 0, 0}},  // 电机停止
};


/**
 * 获取指定速度档位的配置
 * @param gear: 速度档位枚举值
 * @return: 成功返回配置指针，失败返回NULL
 * 
 * 注意：
 * - MOTOR_SPEED_00 是停止配置，不应用于CHAN_START
 * - 有效速度范围：MOTOR_SPEED_0_111_0 到 MOTOR_SPEED_34_02_1
 */
static inline const motor_speed_config *get_motor_speed_config(enum channel_select chx, enum motor_speed_gear gear)
{
	int i;
	const motor_speed_entry *table;
	int table_size;

	switch (chx)
	{
	case CH12:
		table = motor_speed_table_ch12;
		table_size = ARRAY_SIZE(motor_speed_table_ch12);
		break;
	case CH34:
		table = motor_speed_table_ch34;
		table_size = ARRAY_SIZE(motor_speed_table_ch34);
		break;
	default:
		pr_err("tmi8152: Invalid channel %d\n", chx);
		return NULL;
	}

	// 检查是否为停止速度（不应用于启动电机）
	if (gear == MOTOR_SPEED_00)
	{
		pr_err("tmi8152: MOTOR_SPEED_00 is for stopping motor, cannot be used for CHAN_START\n");
		return NULL;
	}

	// 检查范围有效性
	if (gear < 0 || gear >= MOTOR_SPEED_MAX)
	{
		pr_err("tmi8152: Speed gear %d out of range (valid: 0-%d)\n", gear, MOTOR_SPEED_MAX - 1);
		return NULL;
	}

	// 遍历查找表，匹配速度档位
	for (i = 0; i < table_size; i++)
	{
		if (table[i].gear == gear)
		{
			return &table[i].config;
		}
	}

	// 未找到匹配的速度档位（理论上不应该发生）
	pr_err("tmi8152: Speed gear %d not found in configuration table\n", gear);
	return NULL;
}

// // 编译期检查：确保配置表大小与枚举定义匹配
// // 如果不匹配，编译时会报错
// _Static_assert(ARRAY_SIZE(motor_speed_table_ch12) == MOTOR_SPEED_MAX,
// 	       "motor_speed_table_ch12 size must match MOTOR_SPEED_MAX");
// _Static_assert(ARRAY_SIZE(motor_speed_table_ch34) == MOTOR_SPEED_MAX,
// 	       "motor_speed_table_ch34 size must match MOTOR_SPEED_MAX");


#define	 MOTOR_GPIO_NUM	12

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))


static struct motor_data ch12 = {
	.phase_align_base = 2,

};

static struct motor_data ch34 = {
	.phase_align_base = 2,
};

// 将细分枚举值转换为实际细分数
static int subdivide_to_actual(enum subdivide_select subdivide)
{
	switch (subdivide) {
	case SUBDIVIDE16:
		return 16;
	case SUBDIVIDE32:
		return 32;
	case SUBDIVIDE64:
		return 64;
	case SUBDIVIDE128:
		return 128;
	default:
		return 128; // 默认128细分
	}
}

static void enable_channel(struct spi_device *spi, enum channel_select channel_num);
static void global_ctrl_enable(struct spi_device *spi, bool reset);

static int get_actual_divide(u32 src_div, u8* pre_div, u8* div)
{
    u8 pre_div_list[] = {1, 2, 4, 8, 16, 32, 64, 128};
    int num_pre_div = sizeof(pre_div_list) / sizeof(pre_div_list[0]);
    u32 min_diff = 0xFFFFFFFF;
    u8 best_pre = 1, best_div = 1;

    for (int i = 0; i < num_pre_div; i++) {
        u8 p = pre_div_list[i];     
        u32 temp_div = (src_div + (p / 2)) / p;
        u8 d;

        if (temp_div < 1) {
            d = 1;
        } 
		else if (temp_div > 32) {
            d = 32;
        } 
		else {
            d = (u8)temp_div;
        }

        u32 actual = p * d;
        u32 diff = (actual > src_div) ? (actual - src_div) : (src_div - actual);

        if (diff < min_diff) {
            min_diff = diff;
            best_pre = p;
            best_div = d;
        }
    }

    *pre_div = best_pre;
    *div = best_div;
    return 0;
}


//write register
static void spi_tranf_tmi8152(struct spi_device *spi, u16 cmd)
{
	u16 command = ((cmd & 0xff) << 8)| (cmd >> 8);

	
	if(spi_write(spi, &command, 2))
	{
		dev_err(&spi->dev, "spi transfer data fail, cmd = 0x%x\n", cmd);	
	}
}


//read register
static u8 readb_tmi8152(struct spi_device *spi, u8 reg)
{
	u8 ret;
	if(spi_write_then_read(spi, &reg, 1, &ret, 1))
	{
		dev_err(&spi->dev, "tmi8152 read register fail\n");
		return 0;
	}

	return ret;
}


static void writeb_tmi8152(struct spi_device *spi, u8 reg, u8 dat)
{
	u16 cmd = (dat << 8) | (SPI_WRITE_CMD | reg);
	
	//printk_tmi8152("%s, cmd=0x%x\n", __func__, cmd);
	
	if(spi_write(spi, &cmd, 2))
	{
		dev_err(&spi->dev, "tmi8152 write register fail\n");
	}
}

static u32 set_clk_divide(struct spi_device *spi, enum channel_select channel_num, u32 freq_hz, enum divide_select csel, u8 prediv)
{
	u16 cmd;
    u8 pre_div, div;
	u32 divide = SPI_MOTOR_SYSTEM_CLOCK/256/freq_hz, actual_freq;
	// printk_tmi8152("channel_num = %d, freq_hz = %d, csel = %d, prediv = %d\n", channel_num, freq_hz, csel, prediv);

	if (!freq_hz && csel == NOT_DIV) {
		dev_err(&spi->dev, "please set clk correct\n");
		return 0;
	}


	if (channel_num == CH12) {
		cmd = (u16)((SPI_WRITE_CMD | CH12_CLOCK_CTRL) << 8);
	}
	else if (channel_num == CH34) {
		cmd = (u16)((SPI_WRITE_CMD | CH34_CLOCK_CTRL) << 8);
	}


	if (!freq_hz) {
		goto csel_branch;
	}

	get_actual_divide(divide, &pre_div, &div);
	
	// 如果用户指定了prediv参数（0-7），使用用户指定的值，否则使用计算出的值
	// 0xFF表示使用默认值
	if (prediv <= 7) {
		pre_div = prediv;
		// printk_tmi8152("Using user-specified prediv = %d\n", prediv);
	}
	
	cmd |= ((pre_div << 5) | (div));

	// printk_tmi8152("Final: prediv = 0x%x, div = 0x%x\n", pre_div, div);

	spi_tranf_tmi8152(spi, cmd);

	actual_freq = (SPI_MOTOR_SYSTEM_CLOCK/256/(pre_div*div));
	return actual_freq;

csel_branch:

	if (csel == NOT_DIV) {
		return 0;
	}

	// 根据寄存器定义：
	// bit 7-5: PREDIV (0-7)，时钟分频 = 2^PREDIV
	// bit 4-0: DIV (0-31)，PWM分频 = DIV + 1
	// 直接将csel写入DIV字段，prediv写入PREDIV字段
	
	u8 reg_prediv = 0;  // 默认PREDIV=0
	u8 reg_div = csel;  // DIV字段直接使用csel值
	
	// 如果用户指定了prediv参数，使用用户指定的值
	if (prediv <= 7) {
		reg_prediv = prediv;
		// printk_tmi8152("csel_branch: Using user-specified prediv = %d\n", prediv);
	}
	
	cmd |= ((reg_prediv << 5) | reg_div);
	
	// printk_tmi8152("csel_branch: csel=%d, reg_prediv=%d (clock_div=%d), reg_div=%d (pwm_div=%d), final_cmd=0x%x\n",
	//                csel, reg_prediv, (1 << reg_prediv), reg_div, reg_div + 1, cmd);
	
	spi_tranf_tmi8152(spi, cmd);
	
	// // 读回寄存器验证
	// u8 reg_addr = (channel_num == CH12) ? CH12_CLOCK_CTRL : CH34_CLOCK_CTRL;
	// u8 reg_val = readb_tmi8152(spi, reg_addr);
	// u8 read_prediv = (reg_val >> 5) & 0x07;
	// u8 read_div = reg_val & 0x1F;
	// if((reg_prediv != read_prediv) || (reg_div != reg_val))
	// 	printk_tmi8152("check error: Register readback: addr=0x%x, val=0x%x, prediv=%d (clock_div=%d), div=%d (pwm_div=%d)\n",
	// 				reg_addr, reg_val, read_prediv, (1 << read_prediv), read_div, read_div + 1);
	
	// 计算实际频率：28MHz / (2^PREDIV) / 256 / (DIV + 1)
	actual_freq = (SPI_MOTOR_SYSTEM_CLOCK / (1 << reg_prediv) / 256 / (reg_div + 1));
	// printk_tmi8152("csel_branch: Calculated frequency = %d Hz\n", actual_freq);
	
	return actual_freq;
}


static void set_subdivide_and_direction(struct spi_device *spi, enum channel_select channel_num, 
	enum subdivide_select subdivide, enum direct_select direct)
{
	u16 cmd = 0;
	u8 reg, val;

	reg = (channel_num == CH12) ? CH12_SET : CH34_SET;
		
    if(direct == STOP || direct == OUTPUT_OPEN_CHANNEL)
	{
		val = readb_tmi8152(spi, reg);
		val &= ~0xf;
		val |= direct;
		writeb_tmi8152(spi, reg, val);
		
	}
	else if (direct == FORWARD || direct == BACK)
	{
		cmd = ((SPI_WRITE_CMD | reg) << 8) | (subdivide << 4) | direct;
		spi_tranf_tmi8152(spi, cmd);
		int actual_subdivide = subdivide_to_actual(subdivide);
		if (channel_num == CH12)
		{
			ch12.phase_align_base = 256 / actual_subdivide;
			// printk_tmi8152("[CH12] subdivide=%d, actual=%d, phase_align_base=%d\n",
			// 			   subdivide, actual_subdivide, ch12.phase_align_base);
		}
		else if (channel_num == CH34)
		{
			ch34.phase_align_base = 256 / actual_subdivide;
			// printk_tmi8152("[CH34] subdivide=%d, actual=%d, phase_align_base=%d\n",
			// 			   subdivide, actual_subdivide, ch34.phase_align_base);
		}
	}
}

static void set_pwmset(struct spi_device *spi, enum channel_select channel_num, u8 pwmset)
{
	u8 reg_addr = 0x1F;
	u8 reg_data;
	
	// 读取当前寄存器值
	reg_data = readb_tmi8152(spi, reg_addr);
	
	// 根据通道修改对应的pwmset字段
	if (channel_num == CH12) {
		// CH12_PWMSET在位2-0
		reg_data = (reg_data & 0xF8) | (pwmset & 0x07);
		// printk_tmi8152("Set CH12 pwmset=%d, register 0x%02X = 0x%02X\n", pwmset, reg_addr, reg_data);
	} else {
		// CH34_PWMSET在位6-4
		reg_data = (reg_data & 0x8F) | ((pwmset & 0x07) << 4);
		// printk_tmi8152("Set CH34 pwmset=%d, register 0x%02X = 0x%02X\n", pwmset, reg_addr, reg_data);
	}
	
	// 写回寄存器
	writeb_tmi8152(spi, reg_addr, reg_data);
}

//static int compensation = 0;
//static enum direct_select power_on_dir;
static int set_cycles_and_phase(struct spi_device *spi, enum channel_select channel_num, enum direct_select direct, u32 phase, u32 cycles)
{
	u32 val;
	u8 high1, low, high2, bit01_dat;
	u32 max_retries = 3, retry_count = 0;
	u16 val1, val2, val3;
	// printk_tmi8152("set_cycles_and_phase: channel_num=%d, direct=%d, phase=%d, cycles=%d\n", channel_num, direct, phase, cycles);

	if (cycles > MOTOR_CYCLES_MAX)
	{
		dev_err(&spi->dev, "spi motor cycles max value is 8192\n");
		return -EINVAL;
	}

	if (!((direct == FORWARD) || (direct == BACK)))
	{
		dev_err(&spi->dev, "channel %d control direct is not FORWARD or BACK, not set cycles and phase, direct = %d\n", channel_num, direct);
		return -EINVAL;
	}

	if (phase > CYCLES_CONVERT_PHASE)
	{
		dev_err(&spi->dev, "channel %d set phase > 1024, it is a error\n", channel_num);
		return -EINVAL;
	}

	// 相位和周期的计算设置
	if (channel_num == CH12)
	{
		// 先进行相位的设置
		do
		{
			high1 = readb_tmi8152(spi, CH12_PHASE_HIGH) & 0x03;
			low = readb_tmi8152(spi, CH12_PHASE_CNTL);
			high2 = readb_tmi8152(spi, CH12_PHASE_HIGH) & 0x03;

			val1 = (high1 << 8) | low;
			val2 = (high2 << 8) | low;

			retry_count++;
		} while (high1 != high2 && retry_count < max_retries);

		if (val1 == val2)
		{
			val3 = val1;
		}
		else
		{
			dev_err(&spi->dev, "ch12 phase reg data diff, high1 != high2 occur, use high2\n");
			val3 = val2;
		}

		// val3 += phase;
		if (direct == FORWARD)
		{
			val3 += phase;
			if (val3 >= CYCLES_CONVERT_PHASE)
			{
				cycles++;
				val3 -= CYCLES_CONVERT_PHASE;
			}
		}
		else if (direct == BACK)
		{
			//printk_tmi8152("[CH12 BACK] before: val3=%d, phase=%d, cycles=%d\n", val3, phase, cycles);

			// 反向运动：需要从当前相位减去phase
			if (val3 >= phase)
			{
				// 当前相位足够，直接减
				val3 -= phase;
				// printk_tmi8152("[CH12 BACK] val3 >= phase, after subtract: val3=%d\n", val3);
			}
			else
			{
				// 当前相位不够，需要从上一个cycle借位
				// 新相位 = 1024 + val3 - phase
				// 借位后需要多减一个cycle
				val3 = CYCLES_CONVERT_PHASE + val3 - phase;
				cycles++;
				//printk_tmi8152("[CH12 BACK] val3 < phase, borrow: val3=%d, cycles=%d\n", val3, cycles);
			}
		}
		else
		{
			return -EINVAL;
		}

		// 相位对齐
		// 写到相位目标寄存器, 相位寄存器必须按照（256/细分数）的整数倍对齐
		low = ALIGN_UP((val3 & 0xff), ch12.phase_align_base);

		// 检查对齐后是否溢出（低8位对齐后可能变成256）
		if (low >= 256)
		{
			// 溢出，需要进位到高位
			val3 = ((val3 >> 8) + 1) << 8; // 高位+1，低位清0
			low = 0;

			// 如果进位后val3 >= 1024，需要增加cycles
			if (val3 >= CYCLES_CONVERT_PHASE)
			{
				cycles++;
				val3 = 0;
				// printk_tmi8152("[ALIGN] Phase overflow after align, cycles++\n");
			}
		}
		else
		{
			// 没有溢出，但需要重新组合val3（高位 + 对齐后的低位）
			val3 = (val3 & 0x300) | low; // 保留高位，更新低位
		}

		// printk_tmi8152("[ALIGN_UP] low = %d, val3 = %d, cycles = %d\n", low, val3, cycles);
		writeb_tmi8152(spi, CH12_PHASE_SETL, low);
		bit01_dat = (val3 >> 8) & 0x03;
		high2 &= ~(3 << 4); // 清零bit[5:4]
		high2 |= (bit01_dat << 4);
		writeb_tmi8152(spi, CH12_PHASE_HIGH, high2);

		// 进行周期的设置
		val = ((readb_tmi8152(spi, CH12_CYCLE_CNTH) & 0x1f) << 8) | readb_tmi8152(spi, CH12_CYCLE_CNTL);
		// printk_tmi8152("[CYCLES] current_cnt = %d, cycles = %d, direct = %d\n", val, cycles, direct);

		if (direct == FORWARD)
		{
			val += cycles;
			// 正向运动溢出处理
			if (val >= MOTOR_CYCLES_MAX)
			{
				val -= MOTOR_CYCLES_MAX;
				// printk_tmi8152("[CYCLES] Forward overflow, wrapped to %d\n", val);
			}
		}
		else if (direct == BACK)
		{
			// 反向运动：需要处理下溢
			if (val < cycles)
			{
				// 下溢，需要环绕
				val = MOTOR_CYCLES_MAX - (cycles - val);
				// printk_tmi8152("[CYCLES] Backward underflow, wrapped to %d\n", val);
			}
			else
			{
				val -= cycles;
			}
		}
		else
		{
			return -EINVAL;
		}

		// printk_tmi8152("[CYCLES] target_cycles = %d (0x%x)\n", val, val);
		writeb_tmi8152(spi, CH12_CYCLE_SETH, val / 256);
		writeb_tmi8152(spi, CH12_CYCLE_SETL, val % 256);
	}
	else if (channel_num == CH34)
	{
		// 将相位计数寄存器的值平移到相位目标寄存器
		do
		{
			high1 = readb_tmi8152(spi, CH34_PHASE_HIGH) & 0x03;
			low = readb_tmi8152(spi, CH34_PHASE_CNTL);
			high2 = readb_tmi8152(spi, CH34_PHASE_HIGH) & 0x03;

			val1 = (high1 << 8) | low;
			val2 = (high2 << 8) | low;

			retry_count++;
		} while (high1 != high2 && retry_count < max_retries);

		if (val1 == val2)
		{
			val3 = val1;
		}
		else
		{
			dev_err(&spi->dev, "ch12 phase reg data diff, high1 != high2 occur, use high2\n");
			val3 = val2;
		}

		// val3 += phase;
		if (direct == FORWARD)
		{
			val3 += phase;
			if (val3 >= CYCLES_CONVERT_PHASE)
			{
				cycles++;
				val3 -= CYCLES_CONVERT_PHASE;
			}
		}
		else if (direct == BACK)
		{
			// 反向运动：需要从当前相位减去phase
			if (val3 >= phase)
			{
				// 当前相位足够，直接减
				val3 -= phase;
			}
			else
			{
				// 当前相位不够，需要从上一个cycle借位
				// 新相位 = 1024 + val3 - phase
				// 借位后需要多减一个cycle
				val3 = CYCLES_CONVERT_PHASE + val3 - phase;
				cycles++;
			}
		}
		else
		{
			return -EINVAL;
		}

		// 写到相位目标寄存器，相位寄存器必须按照（256/细分数）的整数倍对齐
		low = ALIGN_UP((val3 & 0xff), ch34.phase_align_base);

		// 检查对齐后是否溢出（低8位对齐后可能变成256）
		if (low >= 256)
		{
			// 溢出，需要进位到高位
			val3 = ((val3 >> 8) + 1) << 8; // 高位+1，低位清0
			low = 0;

			// 如果进位后val3 >= 1024，需要增加cycles
			if (val3 >= CYCLES_CONVERT_PHASE)
			{
				cycles++;
				val3 = 0;
				// printk_tmi8152("[ALIGN] Phase overflow after align, cycles++\n");
			}
		}
		else
		{
			// 没有溢出，但需要重新组合val3（高位 + 对齐后的低位）
			val3 = (val3 & 0x300) | low; // 保留高位，更新低位
		}

		// printk_tmi8152("[ALIGN_UP] low = %d, val3 = %d, cycles = %d\n", low, val3, cycles);
		writeb_tmi8152(spi, CH34_PHASE_SETL, low);
		bit01_dat = (val3 >> 8) & 0x03;
		high2 &= ~(3 << 4);
		high2 |= (bit01_dat << 4);
		writeb_tmi8152(spi, CH34_PHASE_HIGH, high2);

		val = ((readb_tmi8152(spi, CH34_CYCLE_CNTH) & 0x1f) << 8) | readb_tmi8152(spi, CH34_CYCLE_CNTL);
		// printk_tmi8152("[CYCLES] current_cnt = %d, cycles = %d, direct = %d\n", val, cycles, direct);

		if (direct == FORWARD)
		{
			val += cycles;
			// 正向运动溢出处理
			if (val >= MOTOR_CYCLES_MAX)
			{
				val -= MOTOR_CYCLES_MAX;
				// printk_tmi8152("[CYCLES] Forward overflow, wrapped to %d\n", val);
			}
		}
		else if (direct == BACK)
		{
			// 反向运动：需要处理下溢
			if (val < cycles)
			{
				// 下溢，需要环绕
				val = MOTOR_CYCLES_MAX - (cycles - val);
				// printk_tmi8152("[CYCLES] Backward underflow, wrapped to %d\n", val);
			}
			else
			{
				val -= cycles;
			}
		}
		else
		{
			return -EINVAL;
		}

		// 圈数目标寄存器
		// printk_tmi8152("[CYCLES] target_cycles = %d (0x%x)\n", val, val);
		writeb_tmi8152(spi, CH34_CYCLE_SETH, val / 256);
		writeb_tmi8152(spi, CH34_CYCLE_SETL, val % 256);
	}

		return 0;
}

static void enable_channel(struct spi_device *spi, enum channel_select channel_num)
{
	u8 src, reg;

	if(channel_num == CH12)
	{
		src = readb_tmi8152(spi, CH12_CTRL);
		reg = CH12_CTRL;
	}
	else if(channel_num == CH34)
	{
		src = readb_tmi8152(spi, CH34_CTRL);
		reg = CH34_CTRL;
	}
	src &= ~0x07;
	src |= (7 << 0);//手动控制模式
	//src |= (2 << 0);//1-2模式
	//src |= (3 << 0);//2-2模式
	src |= EN_CH;//bit7 = 1

	writeb_tmi8152(spi, reg, src);
}

static void disable_channel(struct spi_device *spi, enum channel_select channel_num)
{
	u8 src, reg;

	if(channel_num == CH12)
	{
		src = readb_tmi8152(spi, CH12_CTRL);
		reg = CH12_CTRL;
	}
	else if(channel_num == CH34)
	{
		src = readb_tmi8152(spi, CH34_CTRL);
		reg = CH34_CTRL;
	}
	src &= ~EN_CH;

	writeb_tmi8152(spi, reg, src);
}


static void set_work_mode(struct spi_device *spi, enum channel_select channel_num, enum work_mode mode)
{
	u8 src, reg;

	if(channel_num == CH12)
	{
		src = readb_tmi8152(spi, CH12_CTRL);
		reg = CH12_CTRL;
	}
	else if(channel_num == CH34)
	{
		src = readb_tmi8152(spi, CH34_CTRL);
		reg = CH34_CTRL;
	}
	src &= ~WORK_MASK;
	src |= mode;

	writeb_tmi8152(spi, reg, src);
}

static void global_ctrl_enable(struct spi_device *spi, bool reset)
{
	u16 cmd;

	if(reset)
	{
		//chip reset & init
		cmd = (u16)((SPI_WRITE_CMD | GCTRL) << 8);
		spi_tranf_tmi8152(spi, cmd);//GCTRL, reset
	}


	cmd = (u16)((SPI_WRITE_CMD | GCTRL) << 8) | GRESET | EN_DIS | GENABLE | LOCK_CNT;
	spi_tranf_tmi8152(spi, cmd);//GCTRL, stop reset, out stanby, no use ext pin to contro power, enable channel, cycles reg writable	
}

static void global_ctrl_disable(struct spi_device *spi)
{
	u16 cmd;

	//chip reset & init
	cmd = (u16)((SPI_WRITE_CMD | GCTRL) << 8) | GRESET | EN_DIS;
	spi_tranf_tmi8152(spi, cmd);//GCTRL, disable all channel，cycles reg not writable	
}


static void set_global_reset(struct spi_device *spi, bool rst)
{
	u8 src = readb_tmi8152(spi, GCTRL);

	if(rst)
	{
		src &= ~GRESET;
	}
	else
	{
		src |= GRESET;
	}

	writeb_tmi8152(spi, GCTRL, src);
}

static void set_chip_stanby(struct spi_device *spi, bool enter)
{
	u8 src = readb_tmi8152(spi, GCTRL);

	if(enter)
	{
		src |= STANBY;
	}
	else
	{
		src &= ~STANBY;
	}

	writeb_tmi8152(spi, GCTRL, src);
}

static void set_power_pinctrl(struct spi_device *spi, bool allow)
{
	u8 src = readb_tmi8152(spi, GCTRL);

	if(allow)
	{
		src &= ~EN_DIS;
	}
	else
	{
		src |= EN_DIS;
	}

	writeb_tmi8152(spi, GCTRL, src);
}

static u8 get_chip_temp(struct spi_device *spi)
{
	u8 src = readb_tmi8152(spi, GCTRL);

    //1: higt temp, 0: normal
	return (src & TFS);
}

static u8 get_oscillator_status(struct spi_device *spi)
{
	u8 src = readb_tmi8152(spi, GCTRL);

    //1: ok, 0: error
	return (src & CLK_RDY);
}

// 设置电机速度和方向
static void set_speed_and_direction(struct spi_device *spi, enum channel_select channel_num, enum direct_select direct,
							enum subdivide_select subdivide, u8 prediv, enum divide_select div, u8 pwmset)
{
	set_subdivide_and_direction(spi, channel_num, subdivide, direct);
	set_clk_divide(spi, channel_num, 0, div, prediv);
	set_pwmset(spi, channel_num, pwmset);
}

static void working_change_param(struct spi_device *spi, enum channel_select channel_num,
	enum subdivide_select subdivide, enum direct_select direct, u32 phase, u32 cycles, u8 pwmset)
{
	set_subdivide_and_direction(spi, channel_num, subdivide, direct);
	//set_clk_divide(spi, channel_num, 0, csel);
	if (direct == FORWARD || direct == BACK)
		set_cycles_and_phase(spi, channel_num, direct, phase, cycles);
	set_pwmset(spi, channel_num, pwmset);
	enable_channel(spi, channel_num);
}

static void start_motor_param(struct spi_device *spi, enum channel_select channel_num,
	enum subdivide_select subdivide, enum direct_select direct, u32 phase, u32 cycles,  enum divide_select csel, u8 prediv, u8 pwmset)
{
	set_speed_and_direction(spi, channel_num, direct, subdivide, prediv, csel, pwmset);
	set_cycles_and_phase(spi, channel_num, direct, phase, cycles);
	enable_channel(spi, channel_num);
}

static void set_stop_motor(struct spi_device *spi, enum channel_select ch)
{
	// working_change_param(spi, ch, SUBDIVIDE128, STOP, 0, 0, SPEED_09_00);
	// working_change_param(spi, ch, SUBDIVIDE128, OUTPUT_OPEN_CHANNEL, 0, 0, SPEED_09_00);
	working_change_param(spi, ch, SUBDIVIDE128, STOP, 0, 0, 0);
	working_change_param(spi, ch, SUBDIVIDE128, OUTPUT_OPEN_CHANNEL, 0, 0, 0);	
}


static void spi_motor_init_default(struct spi_device *spi)
{
	global_ctrl_enable(spi, TRUE);
	
	set_subdivide_and_direction(spi, CH12, DEFAULT_SUBDIV, FORWARD);
	set_subdivide_and_direction(spi, CH34, DEFAULT_SUBDIV, FORWARD);
	set_clk_divide(spi, CH12, 0, DEFAULT_DIVSEL, 0);
	set_clk_divide(spi, CH34, 0, DEFAULT_DIVSEL, 0);

	
	writeb_tmi8152(spi, CH12_PHASE_CNTL, 0x00);
	writeb_tmi8152(spi, CH12_PHASE_HIGH, 0x00);
	writeb_tmi8152(spi, CH12_PHASE_SETL, 0x00);
	
	writeb_tmi8152(spi, CH34_PHASE_CNTL, 0x00);
	writeb_tmi8152(spi, CH34_PHASE_HIGH, 0x00);
	writeb_tmi8152(spi, CH34_PHASE_SETL, 0x00);
	

	global_ctrl_enable(spi, FALSE);	
}


/* set current positon as center*/
static int motor_center_set(struct spi_device *spi, enum channel_select channel_num)
{
	if(channel_num == CH12)
	{
		set_stop_motor(spi, channel_num);
		disable_channel(spi, channel_num);

		writeb_tmi8152(spi, CH12_PHASE_HIGH, 0);
		//写到相位寄存器
		writeb_tmi8152(spi, CH12_PHASE_CNTL, 0);
		writeb_tmi8152(spi, CH12_PHASE_SETL, 0);
		writeb_tmi8152(spi, CH12_PHASE_HIGH, 0);

		//圈数寄存器
		writeb_tmi8152(spi, CH12_CYCLE_CNTH, 0x10);
		writeb_tmi8152(spi, CH12_CYCLE_CNTL, 0x0);
		writeb_tmi8152(spi, CH12_CYCLE_SETH, 0x10);
		writeb_tmi8152(spi, CH12_CYCLE_SETL, 0);

		enable_channel(spi, channel_num);
	}
	else if(channel_num == CH34)
	{
		set_stop_motor(spi, channel_num);
		disable_channel(spi, channel_num);
		//写到相位寄存器
		writeb_tmi8152(spi, CH34_PHASE_CNTL, 0);
		writeb_tmi8152(spi, CH34_PHASE_SETL, 0);
		writeb_tmi8152(spi, CH34_PHASE_HIGH, 0);

		//圈数目标寄存器
		writeb_tmi8152(spi, CH34_CYCLE_CNTH, 0x10);
		writeb_tmi8152(spi, CH34_CYCLE_CNTL, 0x0);
		writeb_tmi8152(spi, CH34_CYCLE_SETH, 0x10);
		writeb_tmi8152(spi, CH34_CYCLE_SETL, 0);

		enable_channel(spi, channel_num);
	}

	return 0;
}

static int spimotor_open(struct inode *inode, struct file *file)
{
	struct cdev *cdevp = inode->i_cdev;
	struct spi_motor_dev *spi_motor = container_of(cdevp, struct spi_motor_dev, cdev);

	file->private_data = spi_motor;
	return 0;
}

static int spimotor_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long spimotor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct spi_motor_dev *spi_motor = file->private_data;
	void __user *argp = (void __user *)arg;
	struct chx_enable chx_enable_dat;
	struct chx_mode mode;
	struct motor_status status;
	struct get_angle angle_dat;
	enum channel_select channel;
	struct chx_speed speed_dat;
	struct motor_data *motor;
	u8 high1, low, cycle_cnth, cycle_cntl, low_t1, low_t2, phase_cntl, phase_cnth;
	u32 cycles_cnt_cur = 0, phase_cnt_cur = 0;
	struct get_targ targ;
	const motor_speed_config *speed_config = NULL;
	long retval = 0;


    if (_IOC_TYPE(cmd) != SPIMIOC_BASE) {
        return -ENOTTY;
	}

	// printk_tmi8152("spimotor_ioctl: cmd=0x%x\n", cmd);
	switch(cmd)
	{
	case CHAN_START:
		mutex_lock(&spi_motor->start_lock);
		if (copy_from_user(&chx_enable_dat, argp, sizeof(struct chx_enable))) {
			pr_err("spi motor tmi8152 fail at CHAN_START\n");
			retval = -EFAULT;
			mutex_unlock(&spi_motor->start_lock);
			goto ioctl_out;
		}
		// printk_tmi8152("CHAN_START: chx=%d, subdivide=%d, direct=%d, phase=%d, cycles=%d\n",
		// 			chx_enable_dat.chx, chx_enable_dat.subdivide, chx_enable_dat.direct,
		// 			chx_enable_dat.phase, chx_enable_dat.cycles);
	
		set_stop_motor(spi_motor->spi, chx_enable_dat.chx);//先停住轴，防止设置过程中计数寄存器变动

		speed_config = get_motor_speed_config(chx_enable_dat.chx, chx_enable_dat.speed);
		if (!speed_config)
		{
			pr_err("[CHAN_START] Motor %d, Invalid speed gear %d (valid range: 0-%d, excluding MOTOR_SPEED_00=%d)\n", 
				chx_enable_dat.chx, chx_enable_dat.speed, MOTOR_SPEED_00-1, MOTOR_SPEED_00);
			retval = -EINVAL;  // 使用EINVAL表示参数无效
			mutex_unlock(&spi_motor->start_lock);
			goto ioctl_out;
		}
		// else
		// 	printk_tmi8152("[CHAN_START] chx=%d, speed_config: speed=%d, subdivide=%d, div=%d, prediv=%d, pwmset=%d\n", 
		// 		chx_enable_dat.chx, chx_enable_dat.speed, speed_config->subdivide, speed_config->div, speed_config->prediv, speed_config->pwmset);

		// global_ctrl_enable(spi_motor->spi, FALSE);
		start_motor_param(spi_motor->spi, chx_enable_dat.chx, speed_config->subdivide,
						  chx_enable_dat.direct, chx_enable_dat.phase, chx_enable_dat.cycles, speed_config->div, speed_config->prediv, speed_config->pwmset);
		global_ctrl_enable(spi_motor->spi, FALSE);

		mutex_unlock(&spi_motor->start_lock);
		break;

	case CHAN_START_TEST:
		mutex_lock(&spi_motor->start_lock);
		if (copy_from_user(&chx_enable_dat, argp, sizeof(struct chx_enable))) {
			pr_err("spi motor tmi8152 fail at CHAN_START_TEST\n");
			retval = -EFAULT;
			mutex_unlock(&spi_motor->start_lock);
			goto ioctl_out;
		}
		// printk_tmi8152("CHAN_START_TEST: chx=%d, subdivide=%d, direct=%d, phase=%d, cycles=%d\n",
		// 			chx_enable_dat.chx, chx_enable_dat.subdivide, chx_enable_dat.direct,
		// 			chx_enable_dat.phase, chx_enable_dat.cycles);
	
		set_stop_motor(spi_motor->spi, chx_enable_dat.chx);//先停住轴，防止设置过程中计数寄存器变动


		// printk_tmi8152("[CHAN_START_TEST] chx=%d, subdivide=%d, div=%d, prediv=%d, pwmset=%d\n", 
		// 		chx_enable_dat.chx, chx_enable_dat.subdivide, chx_enable_dat.div, chx_enable_dat.prediv, chx_enable_dat.pwmset);

		// global_ctrl_enable(spi_motor->spi, FALSE);
		start_motor_param(spi_motor->spi, chx_enable_dat.chx, chx_enable_dat.subdivide,
						  chx_enable_dat.direct, chx_enable_dat.phase, chx_enable_dat.cycles, chx_enable_dat.div, chx_enable_dat.prediv, chx_enable_dat.pwmset);
		global_ctrl_enable(spi_motor->spi, FALSE);

		mutex_unlock(&spi_motor->start_lock);
		break;

	case CHAN_STOP:
		mutex_lock(&spi_motor->stop_lock);
		if(copy_from_user(&channel, argp, sizeof(enum channel_select)))
		{
			pr_err("spi motor tmi8152 fail at CHAN_STOP\n");
			retval = -EFAULT;
			mutex_unlock(&spi_motor->stop_lock);
			goto ioctl_out;
		}
		// 清理补偿标志
		motor = (channel == CH12) ? &ch12 : &ch34;
		set_stop_motor(spi_motor->spi, channel);
		mutex_unlock(&spi_motor->stop_lock);
		break;
		
	case SET_SPEED:
		mutex_lock(&spi_motor->set_spe_lock);
		if (copy_from_user(&speed_dat, argp, sizeof(struct chx_speed))) {
			retval = -EFAULT;
			mutex_unlock(&spi_motor->set_spe_lock);
			goto ioctl_out;
		}
		
		// MOTOR_SPEED_00 用于停止电机
		if (speed_dat.speed == MOTOR_SPEED_00)
		{
			// printk_tmi8152("[SET_SPEED] chx=%d, speed=MOTOR_SPEED_00, stopping motor\n", speed_dat.chx);
			set_stop_motor(spi_motor->spi, speed_dat.chx);
		}
		else
		{
			speed_config = get_motor_speed_config(speed_dat.chx, speed_dat.speed);
			if (!speed_config) 
			{
				pr_err("[SET_SPEED] Motor %d, Invalid speed gear %d (valid range: 0-%d or MOTOR_SPEED_00=%d for stop)\n", 
					speed_dat.chx, speed_dat.speed, MOTOR_SPEED_00-1, MOTOR_SPEED_00);
				retval = -EINVAL;  // 使用EINVAL表示参数无效
				mutex_unlock(&spi_motor->set_spe_lock);
				goto ioctl_out;
			}

			spi_motor->cur_speed[speed_dat.chx] = speed_dat.speed;
			set_clk_divide(spi_motor->spi, speed_dat.chx, 0, speed_config->div, speed_config->prediv);

			// printk_tmi8152("[SET_SPEED] chx=%d, speed_config: speed=%d, subdivide=%d, div=%d, prediv=%d, pwmset=%d\n", 
			// 	speed_dat.chx, speed_dat.speed, speed_config->subdivide, speed_config->div, speed_config->prediv, speed_config->pwmset);			
		}
		
		mutex_unlock(&spi_motor->set_spe_lock);

		break;

	case CHANGE_WORKING_PARAM:
		mutex_lock(&spi_motor->start_lock);
		if (copy_from_user(&chx_enable_dat, argp, sizeof(struct chx_enable))) {
			pr_err("spi motor tmi8152 fail at CHANGE_WORK_PARAM\n");
			retval = -EFAULT;
			mutex_unlock(&spi_motor->start_lock);
			goto ioctl_out;
		}

		set_stop_motor(spi_motor->spi, chx_enable_dat.chx);//先停住轴，防止设置过程中计数寄存器变动

		speed_config = get_motor_speed_config(chx_enable_dat.chx, spi_motor->cur_speed[chx_enable_dat.chx]);
		if (!speed_config)
		{			
			pr_err("[CHANGE_WORKING_PARAM] Motor %d, Invalid speed gear %d for channel %d\n", 
				chx_enable_dat.chx, spi_motor->cur_speed[chx_enable_dat.chx], chx_enable_dat.chx);
			retval = -EINVAL;  // 使用EINVAL表示参数无效
			mutex_unlock(&spi_motor->start_lock);
			goto ioctl_out;
		}
		// printk_tmi8152("[CHANGE_WORKING_PARAM] chx=%d, speed_config: speed=%d, subdivide=%d, div=%d, prediv=%d, pwmset=%d\n", 
		// 	chx_enable_dat.chx, spi_motor->cur_speed[chx_enable_dat.chx], speed_config->subdivide, speed_config->div, speed_config->prediv, speed_config->pwmset);			

		working_change_param(spi_motor->spi, chx_enable_dat.chx, speed_config->subdivide,
			chx_enable_dat.direct, chx_enable_dat.phase, chx_enable_dat.cycles, speed_config->pwmset);	

		if (chx_enable_dat.chx == CH12)
		{
			low_t1 = readb_tmi8152(spi_motor->spi, CH12_CYCLE_CNTL);
			low_t2 = readb_tmi8152(spi_motor->spi, CH12_CYCLE_CNTL);
		}
		else
		{
			low_t1 = readb_tmi8152(spi_motor->spi, CH34_CYCLE_CNTL);
			low_t2 = readb_tmi8152(spi_motor->spi, CH34_CYCLE_CNTL);
		}

		if(low_t1 == low_t2)//当前电机停下来了
		{
			global_ctrl_enable(spi_motor->spi, FALSE);
		}
		
		mutex_unlock(&spi_motor->start_lock);

		break;
	case SET_MODE:
		// mutex_lock(&spi_motor->set_mode_lock);
		if(copy_from_user(&mode, argp, sizeof(struct chx_mode)))
		{
			pr_err("spi motor tmi8152 fail at SET_MODE\n");
			retval = -EFAULT;
			goto ioctl_out;
		}
		set_work_mode(spi_motor->spi, mode.chx, mode.mode);
		break;

	case ENTER_RESET:
		set_global_reset(spi_motor->spi, TRUE);
		break;

	case EXIT_RESET:
		set_global_reset(spi_motor->spi, FALSE);
		break;

	case ENTER_STANBY:
		set_chip_stanby(spi_motor->spi, TRUE);
		break;

	case EXIT_STANBY:
		set_chip_stanby(spi_motor->spi, FALSE);
		break;

	case POWER_PINC:
		set_power_pinctrl(spi_motor->spi, TRUE);
		break;

	case NPOWER_PINC:
		set_power_pinctrl(spi_motor->spi, FALSE);
		break;

	case GET_STATUS:
		status.temp = get_chip_temp(spi_motor->spi);
		status.work_status = get_oscillator_status(spi_motor->spi);

		if (copy_to_user(argp, &status, sizeof(struct motor_status))) {
			pr_err("spi motor tmi8152 fail at GET_STATUS\n");
			return -EFAULT;
		}

		// printk_tmi8152("CH12_CLOCK_CTRL = 0x%x\n", readb_tmi8152(spi_motor->spi, CH12_CLOCK_CTRL));
		// printk_tmi8152("CH34_CLOCK_CTRL = 0x%x\n", readb_tmi8152(spi_motor->spi, CH34_CLOCK_CTRL));
		break;

	case DISABLE_CH12:
		disable_channel(spi_motor->spi, CH12);
		break;
	
	case DISABLE_CH34:
		disable_channel(spi_motor->spi, CH34);
		break;

	case CLOSE_CHIP:
		global_ctrl_disable(spi_motor->spi);
		break;

	case GET_CYCLE_CNT:	
		mutex_lock(&spi_motor->cycles_cnt_lock);
		if (copy_from_user(&angle_dat, argp, sizeof(struct get_angle))) {
			pr_err("spi motor tmi8152 ioctl GET_CYCLE_CNT fail at copy from user\n");
			retval = -EFAULT;
			mutex_unlock(&spi_motor->cycles_cnt_lock);
			goto ioctl_out;
		}
		// printk_tmi8152("GET_CYCLE_CNT chx = %d\n", angle_dat.chx);
		   
		if(angle_dat.chx == CH12)
		{
			cycle_cnth = CH12_CYCLE_CNTH;
			cycle_cntl = CH12_CYCLE_CNTL;

			phase_cnth = CH12_PHASE_HIGH;
			phase_cntl = CH12_PHASE_CNTL;
		}
		else
		{
			cycle_cnth = CH34_CYCLE_CNTH;
			cycle_cntl = CH34_CYCLE_CNTL;
			
			phase_cnth = CH34_PHASE_HIGH;
			phase_cntl = CH34_PHASE_CNTL;			
		}

		u8 cycle_l, cycle_h, cycle_h_check, phase_l, phase_h, phase_h_check;
		int retry_count = 0;
		const int MAX_RETRY = 3;
		// 读取圈数计数 (加入双重检查逻辑)
		do {
			cycle_h = readb_tmi8152(spi_motor->spi, cycle_cnth) & 0x1f;
			cycle_l = readb_tmi8152(spi_motor->spi, cycle_cntl);
			cycle_h_check = readb_tmi8152(spi_motor->spi, cycle_cnth) & 0x1f;

			if (cycle_h == cycle_h_check) break;
			retry_count++;
		} while (retry_count < MAX_RETRY);
		
		if (retry_count >= MAX_RETRY) {
    		printk_tmi8152("WARNING: Cycle count read failed after %d retries\n", MAX_RETRY);
		}
		cycles_cnt_cur = (cycle_h << 8) | cycle_l;

		retry_count = 0; // 重置计数器

		// 读取相位计数 (加入双重检查逻辑)
		do {
			phase_h = readb_tmi8152(spi_motor->spi, phase_cnth) & 0x3;
			phase_l = readb_tmi8152(spi_motor->spi, phase_cntl);
			phase_h_check = readb_tmi8152(spi_motor->spi, phase_cnth) & 0x3;
			
			if (phase_h == phase_h_check) break;
			retry_count++;
		} while (retry_count < MAX_RETRY);
		
		if (retry_count >= MAX_RETRY) {
    		printk_tmi8152("WARNING: Phase count read failed after %d retries\n", MAX_RETRY);
		}
		phase_cnt_cur = (phase_h << 8) | phase_l;

		// u64 phase_done_toltal = cycles_cnt_cur * CYCLES_CONVERT_PHASE + phase_cnt_cur;
		// printk_tmi8152("chx=%d, cycles_cnt_cur(%d), phase_cnt_cur(%d), phase_done_toltal(%llu)\n", angle_dat.chx, cycles_cnt_cur, phase_cnt_cur, phase_done_toltal);
		// 判断方向：当恰好在零点时（cycles=4096, phase=0），判断为 dir=0
		if (cycles_cnt_cur > DEFAULT_CNT_VAL || 
		    (cycles_cnt_cur == DEFAULT_CNT_VAL && phase_cnt_cur >= 0))
		{
			angle_dat.phase_done_toltal = (cycles_cnt_cur * CYCLES_CONVERT_PHASE + phase_cnt_cur) - (DEFAULT_CNT_VAL * CYCLES_CONVERT_PHASE);//获取前进的角度
			angle_dat.dir = 1;
			// printk_tmi8152("chx=%d, cycles_cnt_cur(%d) > DEFAULT_CNT_VAL(%d), phase_cnt_cur(%d), dir=1, phase_done_toltal(%llu)\n", angle_dat.chx, cycles_cnt_cur, DEFAULT_CNT_VAL, phase_cnt_cur, angle_dat.phase_done_toltal);
		}
		else
		{
			angle_dat.phase_done_toltal = (DEFAULT_CNT_VAL * CYCLES_CONVERT_PHASE) - (cycles_cnt_cur * CYCLES_CONVERT_PHASE + phase_cnt_cur);//获取后退的角度
			angle_dat.dir = 0;
			// printk_tmi8152("chx=%d, cycles_cnt_cur(%d) <= DEFAULT_CNT_VAL(%d), phase_cnt_cur(%d), dir=0, phase_done_toltal(%llu)\n", angle_dat.chx, cycles_cnt_cur, DEFAULT_CNT_VAL, phase_cnt_cur, angle_dat.phase_done_toltal);
		}
		
		if (copy_to_user(argp, &angle_dat, sizeof(struct get_angle))) {
			pr_err("spi motor tmi8152 ioctl GET_CYCLE_CNT fail at copy to user\n");
			retval = -EFAULT;
			mutex_unlock(&spi_motor->cycles_cnt_lock);
			goto ioctl_out;
		}
		mutex_unlock(&spi_motor->cycles_cnt_lock);
		break;

	case CLR_CYCLE_CNT:	
		if (copy_from_user(&channel, argp, sizeof(enum channel_select))) {
			retval = -EFAULT;
			goto ioctl_out;
		}

		
		if(channel == CH12)
		{
			writeb_tmi8152(spi_motor->spi, CH12_PHASE_SETL, 0x00);
			writeb_tmi8152(spi_motor->spi, CH12_PHASE_HIGH, 0x00);
			
			writeb_tmi8152(spi_motor->spi, CH12_CYCLE_CNTH, 0x10);
			writeb_tmi8152(spi_motor->spi, CH12_CYCLE_CNTL, 0x00);
		}
		else
		{
			writeb_tmi8152(spi_motor->spi, CH34_PHASE_SETL, 0x00);
			writeb_tmi8152(spi_motor->spi, CH34_PHASE_HIGH, 0x00);
			
			writeb_tmi8152(spi_motor->spi, CH34_CYCLE_CNTH, 0x10);
			writeb_tmi8152(spi_motor->spi, CH34_CYCLE_CNTL, 0x00);
		}
		
		
		break;
	
	case GET_TARG_CNT:
		if (copy_from_user(&targ, argp, sizeof(struct get_targ))) {
			pr_err("spi motor tmi8152 fail at GET_TARG_CNT\n");
			retval = -EFAULT;
			goto ioctl_out;
		}

		if (targ.chx == CH12) {
			targ.cycles_targ = (readb_tmi8152(spi_motor->spi, CH12_CYCLE_SETH) << 8) | readb_tmi8152(spi_motor->spi, CH12_CYCLE_SETL);
			targ.phase_targ  = (((readb_tmi8152(spi_motor->spi, CH12_PHASE_HIGH) & 0x30) >> 4) << 8) | readb_tmi8152(spi_motor->spi, CH12_PHASE_SETL);
		} else {
			targ.cycles_targ = (readb_tmi8152(spi_motor->spi, CH34_CYCLE_SETH) << 8) | readb_tmi8152(spi_motor->spi, CH34_CYCLE_SETL);
			targ.phase_targ  = (((readb_tmi8152(spi_motor->spi, CH34_PHASE_HIGH) & 0x30) >> 4) << 8) | readb_tmi8152(spi_motor->spi, CH34_PHASE_SETL);
		}
		
		// 根据目标cycles寄存器的值计算总相位数，与GET_CYCLE_CNT逻辑一致
		if (targ.cycles_targ >= DEFAULT_CNT_VAL) {
			// 正向运动
			targ.phase_targ_toltal = (targ.cycles_targ * CYCLES_CONVERT_PHASE + targ.phase_targ) - (DEFAULT_CNT_VAL * CYCLES_CONVERT_PHASE);
			targ.dir = 1;
		} else {
			// 反向运动
			targ.phase_targ_toltal = (DEFAULT_CNT_VAL * CYCLES_CONVERT_PHASE) - (targ.cycles_targ * CYCLES_CONVERT_PHASE + targ.phase_targ);
			targ.dir = 0;
		}
	
		if (copy_to_user(argp, &targ, sizeof(struct get_targ))) {
			pr_err("spi motor tmi8152 ioctl GET_CYCLE_CNT fail at copy to user\n");
			retval = -EFAULT;
			goto ioctl_out;
		}

		break;

	case SET_CENTER:
		if(copy_from_user(&channel, argp, sizeof(enum channel_select)))
		{
			pr_err("spi motor tmi8152 fail at SET_CENTER\n");
			retval = -EFAULT;
			goto ioctl_out;
		}
		motor_center_set(spi_motor->spi, channel);
		break;

		case READ_REGISTERS:
		{
			struct reg_read_request req;
			u8 *buf = NULL;

			// 1. 从用户空间复制读取请求
			if (copy_from_user(&req, (void __user *)arg, sizeof(struct reg_read_request)))
			{
				pr_err("Failed to copy read request from user\n");
				retval = -EFAULT;
				goto ioctl_out;
			}

			// 2. 验证参数
			if (req.len == 0 || req.len > 64)
			{ // 限制最大读取长度
				pr_err("Invalid read length: %d\n", req.len);
				retval = -EINVAL;
				goto ioctl_out;
			}
			
			// 验证寄存器地址范围
			if (req.addr > 0x1f || (req.addr + req.len - 1) > 0x1f)
			{
				pr_err("Invalid register address range: 0x%02x + %d\n", req.addr, req.len);
				retval = -EINVAL;
				goto ioctl_out;
			}
			
			// 验证用户空间指针
			if (!access_ok(req.data, req.len))
			{
				pr_err("Invalid user space pointer\n");
				retval = -EFAULT;
				goto ioctl_out;
			}

			// 3. 分配内核缓冲区
			buf = kmalloc(req.len, GFP_KERNEL);
			if (!buf)
			{
				pr_err("Failed to allocate kernel buffer\n");
				retval = -ENOMEM;
				goto ioctl_out;
			}

			// 4. 读取多个寄存器
			for (int i = 0; i < req.len; i++)
			{
				buf[i] = readb_tmi8152(spi_motor->spi, req.addr + i);
			}

			// 5. 将数据复制回用户空间
			if (copy_to_user(req.data, buf, req.len))
			{
				pr_err("Failed to copy register data to user\n");
				retval = -EFAULT;
				goto free_buf;
			}

			printk_tmi8152("[READ_REGISTERS] addr=0x%02x, len=%d\n", req.addr, req.len);
			retval = 0; // 成功

		free_buf:
			kfree(buf);
			goto ioctl_out;
		}
		break;

		case WRITE_REGISTERS:
		{
			struct reg_write_request req;
			
			// 1. 从用户空间复制请求
			if (copy_from_user(&req, (void __user *)arg, sizeof(struct reg_write_request))) 
			{
				pr_err("Failed to copy write request from user\n");
				retval = -EFAULT;
				goto ioctl_out;
			}

			// 2. 验证参数
			if (req.len == 0 || req.len > 64) 
			{
				pr_err("Invalid write length: %d\n", req.len);
				retval = -EINVAL;
				goto ioctl_out;
			}
			
			// 3. 验证寄存器地址范围
			if (req.addr > 0x1f || (req.addr + req.len - 1) > 0x1f) 
			{
				pr_err("Invalid register address range: 0x%02x + %d\n", req.addr, req.len);
				retval = -EINVAL;
				goto ioctl_out;
			}

			// 4. 写入多个寄存器
			for (int i = 0; i < req.len; i++) 
			{
				writeb_tmi8152(spi_motor->spi, req.addr + i, req.data[i]);
			}

			printk_tmi8152("[WRITE_REGISTERS] addr=0x%02x, len=%d\n", req.addr, req.len);
			retval = 0;
			goto ioctl_out;
		}
		break;

		case GET_CYCLE_PHASE:
		{
			struct get_cycle_phase cycle_phase_dat;
		
			if (copy_from_user(&cycle_phase_dat, argp, sizeof(struct get_cycle_phase))) {
				pr_err("spi motor tmi8152 ioctl GET_CYCLE_PHASE fail at copy from user\n");
				retval = -EFAULT;
				goto ioctl_out;
			}
		
			// printk_tmi8152("GET_CYCLE_PHASE chx = %d\n", cycle_phase_dat.chx);
			mutex_lock(&spi_motor->cycles_cnt_lock);
		   
			if(cycle_phase_dat.chx == CH12)
			{
				cycle_cnth = CH12_CYCLE_CNTH;
				cycle_cntl = CH12_CYCLE_CNTL;
				phase_cnth = CH12_PHASE_HIGH;
				phase_cntl = CH12_PHASE_CNTL;
			}
			else
			{
				cycle_cnth = CH34_CYCLE_CNTH;
				cycle_cntl = CH34_CYCLE_CNTL;
				phase_cnth = CH34_PHASE_HIGH;
				phase_cntl = CH34_PHASE_CNTL;			
			}

			// 读取当前圈数计数寄存器（先低位后高位）
			low = readb_tmi8152(spi_motor->spi, cycle_cntl);
			high1 = readb_tmi8152(spi_motor->spi, cycle_cnth) & 0x1f;
			cycle_phase_dat.cycles_cnt = (high1 << 8) | low;

			// 读取当前相位计数寄存器（先低位后高位）
			low = readb_tmi8152(spi_motor->spi, phase_cntl);
			high1 = readb_tmi8152(spi_motor->spi, phase_cnth) & 0x3;
			cycle_phase_dat.phase_cnt = (high1 << 8) | low;
		
			mutex_unlock(&spi_motor->cycles_cnt_lock);
		
			// printk_tmi8152("[GET_CYCLE_PHASE] CH%s: cycles_cnt=%u, phase_cnt=%u\n",
			//                (cycle_phase_dat.chx == CH12) ? "12" : "34",
			//                cycle_phase_dat.cycles_cnt, cycle_phase_dat.phase_cnt);

			if (copy_to_user(argp, &cycle_phase_dat, sizeof(struct get_cycle_phase))) {
				pr_err("spi motor tmi8152 ioctl GET_CYCLE_PHASE fail at copy to user\n");
				retval = -EFAULT;
				goto ioctl_out;
			}
		
			retval = 0;
		}
		break;

	default:
		spi_motor_init_default(spi_motor->spi);
		break;
	}


ioctl_out:
	return retval;
}



static long spimotor_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	//struct spi_motor_dev* spi_motor = file->private_data;
	long ret = 0;
	
	//mutex_lock(&spi_motor->start_lock);

	ret = spimotor_ioctl(file, cmd, arg);

	//mutex_unlock(&spi_motor->start_lock);

	return ret;
}


#ifdef CONFIG_COMPAT
static long spimotor_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return spimotor_unlocked_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations spi_motor_fops = {
    .owner 			= THIS_MODULE,
	.open 			= spimotor_open,
	.release		= spimotor_release,
	.unlocked_ioctl = spimotor_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= spimotor_compat_ioctl,
#endif
};

static struct device_node *spi_motor_of_find_node_by_alias_name(const char *alias)
{
	struct device_node *aliases;
	const char *path;
	int len;

	aliases = of_find_node_by_path("/aliases");
	if (!aliases)
	{
		pr_err("spi motor tmi8152 fail at spi_motor_of_find_node_by_alias_name: %s\n", alias);
		return NULL;
	}

	path = of_get_property(aliases, alias, &len);
	of_node_put(aliases);
	if (!path || len <= 1)
	{
		pr_err("spi motor tmi8152 fail at get property: %s\n", alias);
		return NULL;
	}

	return of_find_node_by_path(path);
}

static int spi_motor_uart9_switch_to_uart_pins(struct device *dev)
{
	struct device_node *np;
	struct device_node *cand;
	struct platform_device *pdev;
	struct pinctrl *p;
	struct pinctrl_state *s;
	struct resource res;
	int ret;

	np = spi_motor_of_find_node_by_alias_name("serial9");
	if (!np) {
		dev_warn(dev, "uart9 node not found by alias serial9, fallback to reg match\n");
		cand = NULL;
		for_each_compatible_node(cand, NULL, "rockchip,rk3576-uart") {
			ret = of_address_to_resource(cand, 0, &res);
			if (ret)
				continue;
			if (res.start == 0x2adc0000) {
				np = of_node_get(cand);
				break;
			}
		}
		if (!np) {
			dev_warn(dev, "Failed to find uart9 node (reg 0x2adc0000)\n");
			return -ENODEV;
		}
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_warn(dev, "Failed to find uart9 platform device\n");
		return -ENODEV;
	}

    p = pinctrl_get(&pdev->dev);
    if (IS_ERR(p)) {
        ret = PTR_ERR(p);
        dev_warn(dev, "Failed to get uart9 pinctrl: %d\n", ret);
        return ret;
    }

    s = pinctrl_lookup_state(p, "uart");
    if (IS_ERR(s)) {
        ret = PTR_ERR(s);
        dev_warn(dev, "Failed to lookup uart9 pinctrl state 'uart': %d\n", ret);
        pinctrl_put(p);
        return ret;
    }

    ret = pinctrl_select_state(p, s);
    pinctrl_put(p);
    return ret;
}

static int spi_motor_probe(struct spi_device *spi)
{
	u8 cmd, data;
	struct spi_motor_dev *spi_motor; 
	int ret;

	spi_motor = kzalloc(sizeof(struct spi_motor_dev), GFP_KERNEL);
	if (IS_ERR(spi_motor)) {
		dev_info(&spi->dev, "alloc memory fail, line %d\n", __LINE__);
		ret = -ENOMEM;
		goto error;
	}
	spi_motor->spi = spi;
	spi_motor->name = SPI_MOTOR_NAME;
	mutex_init(&spi_motor->start_lock);
	mutex_init(&spi_motor->cycles_cnt_lock);
	mutex_init(&spi_motor->set_spe_lock);
	mutex_init(&spi_motor->stop_lock);


	spi_motor->motor_gpio = devm_gpiod_get_index(&spi->dev, "motor", 0, GPIOD_ASIS);
	if (IS_ERR(spi_motor->motor_gpio)) {
		ret = devm_gpio_request(&spi->dev, MOTOR_GPIO_NUM, "motor_gpio");
		if (ret) {
			dev_err(&spi->dev, "Failed to request GPIO %d\n", MOTOR_GPIO_NUM);
			goto error;
		}
		
		ret = gpio_direction_output(MOTOR_GPIO_NUM, 0);
		if (ret) {
			dev_err(&spi->dev, "Failed to set GPIO %d as output\n", MOTOR_GPIO_NUM);
			gpio_free(MOTOR_GPIO_NUM);
			goto error;
		}
		
		gpio_set_value(MOTOR_GPIO_NUM, 1);
		goto power_ok;
	}

	ret = gpiod_direction_output(spi_motor->motor_gpio, 0);
	if (ret) {
		dev_err(&spi->dev, "set direction output gpio for motor tmi8152 power supply failed\n");
		goto error;
	}

	ret = gpiod_get_direction(spi_motor->motor_gpio);
	if (ret) {
		dev_err(&spi->dev, "supply power gpio set direction error, should set to output\n");
		goto error;
	}

	gpiod_set_value(spi_motor->motor_gpio, 1);
power_ok:
	udelay(500);
	// ret = spi_motor_uart9_switch_to_uart_pins(&spi->dev);
	// if (ret)
	// 	dev_warn(&spi->dev, "uart9 pinctrl switch to uart failed: %d\n", ret);
	// else
	// 	dev_info(&spi->dev, "uart9 pinctrl switched to uart successfully\n");

	cmd = CMD_READID0;
	ret = spi_write_then_read(spi, &cmd, 1, &data, 1);
	if (ret) {
		dev_err(&spi->dev, "read ID 0 error, spi communication fail\n");
		goto error;
	}
	else {
		dev_info(&spi->dev, "read 0x00 reg, val is 0x%x\n", data);
	}

	cmd = CMD_READID1;
	ret = spi_write_then_read(spi, &cmd, 1, &data, 1);
	if (ret) {
		dev_err(&spi->dev, "read ID 1 error, spi communication fail\n");
		goto error;
	}
	else {
		dev_info(&spi->dev, "read 0x01 reg, val is 0x%x\n", data);
	}


	alloc_chrdev_region(&spi_motor->devnu, 0, 32, spi_motor->name);
	cdev_init(&spi_motor->cdev, &spi_motor_fops);
	cdev_add(&spi_motor->cdev, spi_motor->devnu, 256);
	spi_motor->cdev_class = class_create(THIS_MODULE, spi_motor->name);
	if(!device_create(spi_motor->cdev_class, NULL, spi_motor->devnu, NULL, spi_motor->name))
	{
		dev_err(&spi->dev, "create device fail\n");
		ret = -ENODEV;
		goto release_cdev;
	}

	spi_set_drvdata(spi, spi_motor);
	spi_motor_init_default(spi);

	return 0;

release_cdev:
	cdev_del(&spi_motor->cdev);
	class_destroy(spi_motor->cdev_class);
	unregister_chrdev_region(spi_motor->devnu, 32);
error:
	kfree(spi_motor);
	return ret;
}

static void spi_motor_remove(struct spi_device *spi)
{
	struct spi_motor_dev *spi_motor = spi_get_drvdata(spi);	

	if (gpio_is_valid(MOTOR_GPIO_NUM)) {
			gpio_free(MOTOR_GPIO_NUM);
	}


	cdev_del(&spi_motor->cdev);
	device_destroy(spi_motor->cdev_class, spi_motor->devnu);
	class_destroy(spi_motor->cdev_class);
	//unregister_chrdev(spi_motor->major, spi_motor->name);
	unregister_chrdev_region(spi_motor->devnu, 32);
	kfree(spi_motor);
}


static const struct of_device_id spi_motor_of_match[] = {
	{ .compatible = "xbotgo,spi_motor_tmi8152", },
	{ }
};
MODULE_DEVICE_TABLE(of, spi_motor_of_match);

static const struct spi_device_id spi_motor_id[] = {
	{ "xbotgo,spi_motor_tmi8152", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, spi_motor_id);

static struct spi_driver spi_motor = {
	.driver = {
		.name = SPI_MOTOR_NAME,
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
		.of_match_table = spi_motor_of_match,
	},
	.probe = spi_motor_probe,
	.remove = spi_motor_remove,
	.id_table = spi_motor_id,
};

static int spi_motor_init(void)
{
	int ret = 0;
	
	printk(KERN_INFO "Motor tmi8152 driver init, version: %s\n", MOTOR_DRIVER_VERSION);

    ret = spi_register_driver(&spi_motor);
	if(ret)
	{
		printk(KERN_ERR "spi motor tmi8152 driver register fail, ret=%d\n", ret);
	}
	return ret;
}

static void spi_motor_exit(void)
{
	spi_unregister_driver(&spi_motor);
}

module_init(spi_motor_init);
module_exit(spi_motor_exit);
MODULE_AUTHOR("zhangjiaqi@xbotgo.com, 2025.08");
MODULE_LICENSE("GPL");
