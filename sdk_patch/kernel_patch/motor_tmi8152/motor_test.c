#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "tmi8152.h"

#define BACKLASH_CALIBRATE_ANGLE 5


/*
   512个cycles = 360度
   512 * 1024个phase = 360度
   524288个phase = 360度
   1个phase = 360.0 / 524288.0 度;
 */
static double phases_to_angles(u64 phases)
{
	double angle = 0;

	angle = (double)(phases * 360.0 / MOTOR_TOTAL_PHASES);

	return angle;
}


/* 360度 = 512cycles
     1度 = 512/360cycles
 */
static int angles_to_phases(struct chx_enable *chx, float angle)
{
    double cycles = 0;
    float integer_part;
    double fractional_part;
    double phase_cnt;

	if (!(chx->direct == FORWARD || chx->direct == BACK))
	{
		printf("not set motor direct, please set direct before START_CHAN\n");
		return -1;
	}


    cycles = angle * 512.0 / 360.0; //获取总的cycles数
    fractional_part = modff(cycles, &integer_part);

    printf("[DEBUG] angle=%.2f, cycles=%.6f\n", angle, cycles);
    printf("[DEBUG] integer_part=%.0f, fractional_part=%.6f\n", integer_part, fractional_part);

    chx->cycles = (int)integer_part;
    phase_cnt = fractional_part * 1024.0f;
	    
	printf("[DEBUG] phase_cnt=%.6f\n", phase_cnt);
	chx->phase = (int)phase_cnt;
	printf("[DEBUG] chx->phase=%d (before align)\n", chx->phase);

	// 相位对齐：必须按照（256/细分数）的整数倍对齐
	// 对于128细分，phase_align_base = 256/128 = 2
	// 需要将枚举值转换为实际细分数
	int actual_subdivide;
	switch (chx->subdivide) {
	case SUBDIVIDE16:
		actual_subdivide = 16;
		break;
	case SUBDIVIDE32:
		actual_subdivide = 32;
		break;
	case SUBDIVIDE64:
		actual_subdivide = 64;
		break;
	case SUBDIVIDE128:
		actual_subdivide = 128;
		break;
	default:
		actual_subdivide = 128;
		break;
	}
	
	int phase_align_base = 256 / actual_subdivide;
	int aligned_phase = ALIGN_UP(chx->phase, phase_align_base);
	printf("[DEBUG] subdivide_enum=%d, actual=%d, phase_align_base=%d, aligned_phase=%d\n", 
		chx->subdivide, actual_subdivide, phase_align_base, aligned_phase);
	
	// 对齐后可能超过1024，需要进位
	if (aligned_phase >= CYCLES_CONVERT_PHASE)
	{
		printf("aligned_phase(%d) >= CYCLES_CONVERT_PHASE(%d)\n", aligned_phase, CYCLES_CONVERT_PHASE);
		chx->cycles++;
		aligned_phase -= CYCLES_CONVERT_PHASE;
	}
	
	// // 检查cycles是否超过最大值（360° = 512 cycles）
	// if (chx->cycles > 512)
	// {
	// 	printf("Error: cycles(%d) exceeds maximum (512), angle too large\n", chx->cycles);
	// 	return -1;
	// }
	
	chx->phase = aligned_phase;

    // 打印整数部分和小数部分
    printf("cycles = %d\n", chx->cycles);
    printf("phase = %d (aligned)\n", chx->phase);

	return 0;
}





// 等待电机运动完成
// 返回值: 0=正常到达, 1=位置稳定但未到达目标, -1=超时
// 虚位补偿校准函数
static int backlash_calibrate(int fd)
{
    struct chx_enable chx;
    struct get_angle calc_val;
    int ret;
    double calibrate_angle = BACKLASH_CALIBRATE_ANGLE;

    printf("\n=== Backlash Calibration ===\n");
    printf("Starting calibration for CH12...\n");

    // // 清零CH12位置到中心（4096 cycles）
    // enum channel_select ch = CH12;
    // ret = ioctl(fd, CLR_CYCLE_CNT, &ch);
    // if (ret < 0)
    // {
    //     printf("Error: Failed to clear cycle count\n");
    //     return -1;
    // }
    // usleep(10000);
    // printf("Reset CH12 position to center\n");


    // 先调用GET_BACKLASH_COMPENSATION获取当前的虚位补偿值
    struct backlash_config backlash;
    backlash.chx = CH12;
    ret = ioctl(fd, GET_BACKLASH_COMPENSATION, &backlash);
    if (ret < 0)
    {
        printf("Error: Failed to get backlash compensation\n");
        return -1;
    }
    printf("Current backlash compensation: %d\n", backlash.compensation_phases);
    // 然后调用SET_BACKLASH_COMPENSATION禁用虚位补偿值
    backlash.enabled = false;
    ret = ioctl(fd, SET_BACKLASH_COMPENSATION, &backlash);
    if (ret < 0)
    {
        printf("Error: Failed to set backlash compensation\n");
        return -1;
    }
    printf("Backlash compensation set to %d\n", backlash.compensation_phases);

    // 配置电机参数
    chx.chx = CH12;
    chx.subdivide = SUBDIVIDE128;
    chx.speed = MOTOR_SPEED_15_21_3; // 约14.5度/秒

    // 1. 正向运动校准角度
    printf("\nStep 1: Forward motion %.1f degrees...\n", calibrate_angle);
    chx.direct = FORWARD;
    if (angles_to_phases(&chx, calibrate_angle) < 0)
    {
        printf("Error: Failed to convert angle\n");
        return -1;
    }

    ret = ioctl(fd, CHAN_START, &chx);
    if (ret < 0)
    {
        printf("Error: Failed to start forward motion\n");
        return -1;
    }

    // 等待正向运动完成
    calc_val.chx = CH12;
    u64 last_pos = 0;
    int stable_count = 0;
    int timeout = 30;

    for (int i = 0; i < timeout; i++)
    {
        usleep(100000);
        ioctl(fd, GET_CYCLE_CNT, &calc_val);

        if (calc_val.phase_done_toltal == last_pos)
        {
            stable_count++;
            if (stable_count >= 3)
            {
                printf("Forward motion completed at position: %d\n", calc_val.phase_done_toltal);
                break;
            }
        }
        else
        {
            stable_count = 0;
            last_pos = calc_val.phase_done_toltal;
        }
    }

    if (stable_count < 3)
    {
        printf("Warning: Forward motion timeout\n");
    }

    // 2. 反向运动校准角度
    calibrate_angle = BACKLASH_CALIBRATE_ANGLE;
    printf("\nStep 2: Backward motion %.1f degrees (with backlash compensation)...\n", calibrate_angle);
    chx.direct = BACK;
    if (angles_to_phases(&chx, calibrate_angle) < 0)
    {
        printf("Error: Failed to convert angle\n");
        return -1;
    }

    ret = ioctl(fd, CHAN_START, &chx);
    if (ret < 0)
    {
        printf("Error: Failed to start backward motion\n");
        return -1;
    }

    // 等待反向运动完成
    last_pos = 0;
    stable_count = 0;

    for (int i = 0; i < timeout; i++)
    {
        usleep(100000); // 100ms
        ioctl(fd, GET_CYCLE_CNT, &calc_val);

        if (calc_val.phase_done_toltal == last_pos)
        {
            stable_count++;
            if (stable_count >= 3)
            {
                printf("Backward motion completed at position: %d\n", calc_val.phase_done_toltal);
                break;
            }
        }
        else
        {
            stable_count = 0;
            last_pos = calc_val.phase_done_toltal;
        }
    }

    if (stable_count < 3)
    {
        printf("Warning: Backward motion timeout\n");
    }

    // 停止电机
    chx.direct = STOP;
    ioctl(fd, CHANGE_WORKING_PARAM, &chx);
    backlash.enabled = true;
    ret = ioctl(fd, SET_BACKLASH_COMPENSATION, &backlash);
    if (ret < 0)
    {
        printf("Error: Failed to set backlash compensation\n");
        return -1;
    }
    printf("Backlash compensation set to %d\n", backlash.compensation_phases);    

    printf("\nCalibration completed!\n");
    printf("Backlash compensation is now active.\n");
    printf("===========================\n\n");

    return 0;
}

static int wait_motor_idle(int fd, struct chx_enable *chx, u64 start_phase, u64 target_phase)
{
    struct get_angle calc_val;
    u64 current_phase;
    u64 movement_phase; // 运动的相位增量
    int check_count = 0;
    const int stable_checks = 5;      // 需要连续5次位置不变才认为停止
    const int timeout_seconds = 200;   // 最大等待30秒
    const int check_interval_ms = 50; // 每50ms检查一次
    int total_checks = (timeout_seconds * 1000) / check_interval_ms;
    int check_counter = 0;
    u64 last_phase = 0;
    int first_check = 1;

    calc_val.chx = chx->chx;

    // 直接使用传入的目标位置（由驱动计算得出）
    printf("Waiting for motor to complete...\n");
    printf("  Start: %llu phases, Target: %llu phases\n",
           start_phase, target_phase);

    while (check_counter < total_checks)
    {
        usleep(check_interval_ms * 1000); // 每50ms检查一次
        check_counter++;

        ioctl(fd, GET_CYCLE_CNT, &calc_val);
        current_phase = calc_val.phase_done_toltal;

        // 第一次检查，记录初始位置
        if (first_check)
        {
            last_phase = current_phase;
            first_check = 0;
            continue;
        }

        // 检查位置是否稳定（不再变化）
        if (current_phase == last_phase)
        {
            check_count++;
            if (check_count >= stable_checks)
            {
                // 位置已稳定，检查是否到达目标
                u64 phase_diff;
                if (current_phase >= target_phase)
                {
                    phase_diff = current_phase - target_phase;
                }
                else
                {
                    phase_diff = target_phase - current_phase;
                }

                // 128细分时，相位对齐基准为2，允许的误差范围应该更大
                // 考虑到对齐和机械误差，允许100个相位的误差（约0.07°）
                if (phase_diff <= 100)
                {
                    printf("Motor reached target and stopped.\n");
                    return 0; // 正常到达
                }
                else
                {
                    printf("Motor stopped but did not reach target.\n");
                    printf("  Target: %llu phases, Actual: %llu phases, Diff: %llu\n",
                           target_phase, current_phase, phase_diff);
                    return 1; // 位置稳定但未到达目标（可能堵转或失步）
                }
            }
        }
        else
        {
            // 位置还在变化，重置计数器
            check_count = 0;
            last_phase = current_phase;
        }

        // 打印进度（每秒打印一次）
        if (check_counter % 20 == 0) // 20 * 50ms = 1s
        {
            double current_angle = phases_to_angles(current_phase);
            printf("  Current: %llu phases (%.2f degrees)\n", current_phase, current_angle);
        }
    }

    // 超时
    printf("Timeout: Motor did not stop within %d seconds.\n", timeout_seconds);
    return -1;
}

// 清除周期相位计数器
static int clear_cycle_counter(int fd)
{
    enum channel_select ch = CH12;
    int ret;

    printf("Clearing cycle and phase counter for CH12...\n");
    
    ret = ioctl(fd, CLR_CYCLE_CNT, &ch);
    if (ret < 0)
    {
        printf("Error: Failed to clear cycle counter\n");
        return -1;
    }
    
    usleep(10000); // 等待10ms确保清零操作完成
    printf("Cycle and phase counter cleared successfully\n");
    
    return 0;
}

// 显示当前电机状态
static int show_status(int fd)
{
    struct get_angle calc_val;
    int ret;

    printf("\n=== Motor Status (CH12) ===\n");
    
    calc_val.chx = CH12;
    ret = ioctl(fd, GET_CYCLE_CNT, &calc_val);
    if (ret < 0)
    {
        printf("Error: Failed to get cycle count\n");
        return -1;
    }

    u64 total_phase = calc_val.phase_done_toltal;
    int cycle = total_phase / 1024;
    int phase = total_phase % 1024;
    double angle = phases_to_angles(total_phase);
    if (calc_val.dir == 1)
        angle *= 1;
    else
        angle *= -1;

    printf("Current Position:\n");
    printf("  Cycle       : %d\n", cycle);
    printf("  Phase       : %d\n", phase);
    printf("  Total Phase : %llu\n", total_phase);
    printf("  Angle       : %.2f degrees\n", angle);
    printf("===========================\n\n");

    return 0;
}

// 打印使用说明
void print_usage(const char *prog_name)
{
    printf("Usage:\n");
    printf("  Mode 1 (with speed gear):\n");
    printf("  %s -cycles <num> -phase <num> -speed <0-41> -dir <0|1> [-ch <ch12|ch34>] [-subdivide <16|32|64|128>]\n", prog_name);
    printf("  %s -angle <degrees> -speed <0-41> -dir <0|1> [-ch <ch12|ch34>] [-subdivide <16|32|64|128>]\n", prog_name);
    printf("\n");
    printf("  Mode 2 (test mode with manual parameters):\n");
    printf("  %s -angle <degrees> -dir <0|1> -ch <ch12|ch34> -subdivide <16|32|64|128> -prediv <0-7> -div <0-7> -pwmset <0-7>\n", prog_name);
    printf("  %s -cycles <num> -phase <num> -dir <0|1> -ch <ch12|ch34> -subdivide <16|32|64|128> -prediv <0-7> -div <0-7> -pwmset <0-7>\n", prog_name);
    printf("\n");
    printf("  %s stop\n", prog_name);
    printf("  %s calibrate\n", prog_name);
    printf("  %s clear\n", prog_name);
    printf("  %s status\n", prog_name);
    printf("  %s get_phase <ch12|ch34> : 获取原始寄存器值\n", prog_name);
    printf("  %s set_backlash <ch12|ch34> <phases> <angle> [enabled] : 设置虚位补偿\n", prog_name);
    printf("  %s get_backlash <ch12|ch34> : 获取虚位补偿配置\n", prog_name);
    printf("  %s read_reg <addr> <len>  : 读取寄存器 (地址, 长度)\n", prog_name);
    printf("  %s write_reg <addr> <data...> : 写入寄存器 (地址, 数据...)\n", prog_name);
    printf("\n");
    printf("Options:\n");
    printf("  -cycles <num>    : 设置运行周期数 (0-8191)\n");
    printf("  -phase <num>     : 设置相位 (0-1023)\n");
    printf("  -angle <degrees> : 设置转动角度（自动转换为cycles和phase）\n");
    printf("  -speed <0-41>    : 设置速度档位 (0=最快88deg/s, 41=最慢2.1deg/s, 42=停止) [Mode 1]\n");
    printf("  -dir <0|1>       : 设置方向 (0=后退, 1=前进)\n");
    printf("  -ch <ch12|ch34>  : 指定通道 (默认: ch12)\n");
    printf("  -subdivide <num> : 设置细分数 (16/32/64/128)\n");
    printf("  -prediv <0-7>    : 设置预分频 [Mode 2 only]\n");
    printf("  -div <0-7>       : 设置分频 [Mode 2 only]\n");
    printf("  -pwmset <0-7>    : 设置PWM [Mode 2 only]\n");
    printf("  stop             : 停止电机\n");
    printf("  calibrate        : 校准虚位补偿\n");
    printf("  clear            : 清除周期相位计数器\n");
    printf("  status           : 显示当前电机位置状态\n");
    printf("  get_phase        : 获取原始 cycles 和 phase 寄存器值\n");
    printf("  set_backlash     : 设置虚位补偿参数 (通道, 相位数, 角度, 启用状态)\n");
    printf("  get_backlash     : 获取当前虚位补偿配置\n");
    printf("  read_reg         : 读取寄存器值\n");
    printf("  write_reg        : 写入寄存器值\n");
    printf("\n");
    printf("Examples:\n");
    printf("  Mode 1 (Speed Gear):\n");
    printf("  %s -cycles 10 -phase 512 -speed 9 -dir 1 -ch ch12\n", prog_name);
    printf("  %s -angle 180.0 -speed 1 -dir 1 -ch ch34\n", prog_name);
    printf("  %s -angle 0.5 -speed 23 -dir 1 -ch ch12\n", prog_name);
    printf("\n");
    printf("  Mode 2 (Test Mode):\n");
    printf("  %s -angle 90.0 -dir 1 -ch ch12 -subdivide 128 -prediv 2 -div 5 -pwmset 1\n", prog_name);
    printf("  %s -cycles 100 -phase 512 -dir 0 -ch ch34 -subdivide 64 -prediv 1 -div 3 -pwmset 2\n", prog_name);
    printf("  %s stop\n", prog_name);
    printf("  %s calibrate\n", prog_name);
    printf("  %s clear\n", prog_name);
    printf("  %s status\n", prog_name);
    printf("  %s get_phase ch12\n", prog_name);
    printf("  %s set_backlash ch12 1024 90\n", prog_name);
    printf("  %s set_backlash ch12 1024 90 1\n", prog_name);
    printf("  %s set_backlash ch12 0 0 0\n", prog_name);
    printf("  %s get_backlash ch12\n", prog_name);
    printf("  %s read_reg 0x02 1\n", prog_name);
    printf("  %s write_reg 0x04 0x80\n", prog_name);
}

// 解析命令行参数
int parse_args(int argc, char **argv, struct chx_enable *chx, int *use_angle, float *angle, int *channel, int *has_speed_param)
{
    int i;
    char *endptr;
    int has_cycles = 0, has_phase = 0, has_speed = 0, has_dir = 0;
    int has_prediv = 0, has_div = 0, has_pwmset = 0;

    *use_angle = 0;
    *channel = CH12; // 默认通道为CH12
    *has_speed_param = 0; // 默认未使用speed参数

    for (i = 1; i < argc; i++)
    {
        if (!strcmp("-cycles", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -cycles requires a value\n");
                return -1;
            }
            chx->cycles = atoi(argv[++i]);
            if (chx->cycles > 8191)
            {
                printf("Error: cycles must be 0-8191\n");
                return -1;
            }
            has_cycles = 1;
        }
        else if (!strcmp("-phase", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -phase requires a value\n");
                return -1;
            }
            chx->phase = atoi(argv[++i]);
            if (chx->phase > 1023)
            {
                printf("Error: phase must be 0-1023\n");
                return -1;
            }
            has_phase = 1;
        }
        else if (!strcmp("-angle", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -angle requires a value\n");
                return -1;
            }
            *angle = atof(argv[++i]);
            *use_angle = 1;
        }
        else if (!strcmp("-speed", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -speed requires a value\n");
                return -1;
            }
            int speed_val = atoi(argv[++i]);
            if (speed_val < 0 || speed_val >= MOTOR_SPEED_MAX)
            {
                printf("Error: speed must be 0-%d\n", MOTOR_SPEED_MAX - 1);
                return -1;
            }
            chx->speed = (enum motor_speed_gear)speed_val;
            has_speed = 1;
            *has_speed_param = 1;
        }
        else if (!strcmp("-dir", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -dir requires a value\n");
                return -1;
            }
            int dir = atoi(argv[++i]);
            if (dir == 1)
            {
                chx->direct = FORWARD;
            }
            else if (dir == 0)
            {
                chx->direct = BACK;
            }
            else
            {
                printf("Error: dir must be 0 or 1\n");
                return -1;
            }
            has_dir = 1;
        }
        else if (!strcmp("-ch", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -ch requires a value\n");
                return -1;
            }
            char *ch_str = argv[++i];
            if (!strcmp(ch_str, "ch12"))
            {
                *channel = CH12;
            }
            else if (!strcmp(ch_str, "ch34"))
            {
                *channel = CH34;
            }
            else
            {
                printf("Error: channel must be ch12 or ch34\n");
                return -1;
            }
        }
        else if (!strcmp("-subdivide", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -subdivide requires a value\n");
                return -1;
            }
            int subdivide_val = atoi(argv[++i]);
            switch (subdivide_val)
            {
                case 16:
                    chx->subdivide = SUBDIVIDE16;
                    break;
                case 32:
                    chx->subdivide = SUBDIVIDE32;
                    break;
                case 64:
                    chx->subdivide = SUBDIVIDE64;
                    break;
                case 128:
                    chx->subdivide = SUBDIVIDE128;
                    break;
                default:
                    printf("Error: subdivide must be 16, 32, 64, or 128\n");
                    return -1;
            }
        }
        else if (!strcmp("-prediv", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -prediv requires a value\n");
                return -1;
            }
            chx->prediv = (u8)atoi(argv[++i]);
            has_prediv = 1;
        }
        else if (!strcmp("-div", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -div requires a value\n");
                return -1;
            }
            chx->div = (u8)atoi(argv[++i]);
            has_div = 1;
        }
        else if (!strcmp("-pwmset", argv[i]))
        {
            if (i + 1 >= argc)
            {
                printf("Error: -pwmset requires a value\n");
                return -1;
            }
            chx->pwmset = (u8)atoi(argv[++i]);
            has_pwmset = 1;
        }
        else
        {
            printf("Error: Unknown option '%s'\n", argv[i]);
            return -1;
        }
    }

    // 检查必需参数
    if (*use_angle)
    {
        // 使用角度模式，不需要cycles和phase
        if (has_cycles || has_phase)
        {
            printf("Error: Cannot use -angle with -cycles or -phase\n");
            return -1;
        }
    }
    else
    {
        // 使用cycles/phase模式
        if (!has_cycles || !has_phase)
        {
            printf("Error: Must specify both -cycles and -phase, or use -angle\n");
            return -1;
        }
    }

    if (!has_dir)
    {
        printf("Error: Must specify -dir\n");
        return -1;
    }

    // 检查参数组合的有效性
    if (has_speed)
    {
        // 使用speed模式，不应该有prediv/div/pwmset参数
        if (has_prediv || has_div || has_pwmset)
        {
            printf("Error: Cannot use -speed with -prediv/-div/-pwmset\n");
            return -1;
        }
    }
    else
    {
        // 使用测试模式，必须有prediv/div/pwmset参数
        if (!has_prediv || !has_div || !has_pwmset)
        {
            printf("Error: Must specify -prediv, -div, and -pwmset when not using -speed\n");
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct chx_enable chx;
    struct get_angle calc_val;
    struct get_targ targ_val;
    int fd, ret;
    int use_angle = 0;
    float angle = 0.0;

    // 打开设备
    fd = open("/dev/tmi8152", O_RDWR);
    if (fd < 0)
    {
        printf("Error: Failed to open /dev/tmi8152\n");
        return -1;
    }

    // 检查是否是停止命令
    if (argc == 2 && !strcmp("stop", argv[1]))
    {
        printf("Stopping motor...\n");
        chx.chx = CH12;
        chx.direct = STOP;
        ret = ioctl(fd, CHANGE_WORKING_PARAM, &chx);
        if (ret < 0)
        {
            printf("Error: Failed to stop motor\n");
            close(fd);
            return -1;
        }
        printf("Motor stopped successfully\n");
        close(fd);
        return 0;
    }

    // 检查是否是校验命令
    if (argc == 2 && strcmp("calibrate", argv[1]) == 0)
    {
        printf("Calibrating backlash...\n");
        backlash_calibrate(fd);
        close(fd);
        return 0;
    }

    // 检查是否是清除计数器命令
    if (argc == 2 && strcmp("clear", argv[1]) == 0)
    {
        ret = clear_cycle_counter(fd);
        close(fd);
        return ret;
    }

    // 检查是否是读取寄存器命令
    if (argc == 4 && strcmp("read_reg", argv[1]) == 0)
    {
        u8 addr = (u8)strtoul(argv[2], NULL, 0);
        u8 len = (u8)strtoul(argv[3], NULL, 0);
        u8 *data = NULL;
        
        // 验证参数
        if (len == 0 || len > 64) {
            printf("Invalid length: %u (must be 1-64)\n", len);
            close(fd);
            return -1;
        }
        
        if (addr > 0x1f || (addr + len - 1) > 0x1f) {
            printf("Invalid register address range: 0x%02X + %u\n", addr, len);
            close(fd);
            return -1;
        }
        
        data = (u8 *)malloc(len);
        if (!data) {
            printf("Failed to allocate memory for read buffer\n");
            close(fd);
            return -1;
        }
        
        printf("Reading %u bytes from register 0x%02X...\n", len, addr);
        
        struct reg_read_request req = {
            .addr = addr,
            .len = len,
            .data = data
        };
        
        ret = ioctl(fd, READ_REGISTERS, &req);
        if (ret < 0) {
            perror("Failed to read registers");
            free(data);
            close(fd);
            return -1;
        }
        
        printf("Read data (address: 0x%02X, length: %u):\n", addr, len);
        for (int i = 0; i < len; i++) {
            if (i % 16 == 0) {
                if (i > 0) printf("\n");
                printf("0x%02X: ", addr + i);
            }
            printf("%02X ", data[i]);
        }
        printf("\n");
        
        free(data);
        close(fd);
        return 0;
    }
    
    // 检查是否是写入寄存器命令
    if (argc >= 4 && strcmp("write_reg", argv[1]) == 0)
    {
        u8 addr = (u8)strtoul(argv[2], NULL, 0);
        u8 len = argc - 3;
        struct reg_write_request req;
        
        // 验证参数
        if (len == 0 || len > 64) {
            printf("Invalid length: %u (must be 1-64)\n", len);
            close(fd);
            return -1;
        }
        
        if (addr > 0x1f || (addr + len - 1) > 0x1f) {
            printf("Invalid register address range: 0x%02X + %u\n", addr, len);
            close(fd);
            return -1;
        }
        
        // 解析数据（支持自动识别十六进制和十进制）
        for (int i = 0; i < len; i++) {
            req.data[i] = (u8)strtoul(argv[3 + i], NULL, 0);
        }
        
        req.addr = addr;
        req.len = len;
        
        printf("Writing %u bytes to register 0x%02X:", len, addr);
        for (int i = 0; i < len; i++) {
            printf(" %02X", req.data[i]);
        }
        printf("\n");
        
        ret = ioctl(fd, WRITE_REGISTERS, &req);
        if (ret < 0) {
            perror("Failed to write registers");
            close(fd);
            return -1;
        }
        
        printf("Write successful\n");
        close(fd);
        return 0;
    }
    
    // 检查是否是获取寄存器原始值命令
    if (argc == 3 && strcmp("get_phase", argv[1]) == 0)
    {
        struct get_cycle_phase phase_data;
        
        // 解析通道参数
        if (strcmp("ch12", argv[2]) == 0) {
            phase_data.chx = CH12;
        } else if (strcmp("ch34", argv[2]) == 0) {
            phase_data.chx = CH34;
        } else {
            printf("Invalid channel: %s (use ch12 or ch34)\n", argv[2]);
            close(fd);
            return -1;
        }
        
        printf("Getting raw register values for %s...\n", argv[2]);
        
        ret = ioctl(fd, GET_CYCLE_PHASE, &phase_data);
        if (ret < 0) {
            perror("Failed to get cycle phase");
            close(fd);
            return -1;
        }
        
        printf("Raw Register Values for %s:\n", argv[2]);
        printf("  cycles_cnt = %u (0x%04X)\n", phase_data.cycles_cnt, phase_data.cycles_cnt);
        printf("  phase_cnt  = %u (0x%04X)\n", phase_data.phase_cnt, phase_data.phase_cnt);
        printf("  Total phases = %llu\n", (u64)phase_data.cycles_cnt * CYCLES_CONVERT_PHASE + phase_data.phase_cnt);
        
        // 计算相对于中心点的位置
        u64 total_phases = (u64)phase_data.cycles_cnt * CYCLES_CONVERT_PHASE + phase_data.phase_cnt;
        u64 center_phases = (u64)DEFAULT_CNT_VAL * CYCLES_CONVERT_PHASE;
        
        if (total_phases >= center_phases) {
            u64 forward_phases = total_phases - center_phases;
            double forward_angle = phases_to_angles(forward_phases);
            printf("  Position: +%.3f° (forward)\n", forward_angle);
        } else {
            u64 backward_phases = center_phases - total_phases;
            double backward_angle = phases_to_angles(backward_phases);
            printf("  Position: -%.3f° (backward)\n", backward_angle);
        }
        
        close(fd);
        return 0;
    }

    // 检查是否是设置虚位补偿命令
    if ((argc == 5 || argc == 6) && strcmp("set_backlash", argv[1]) == 0)
    {
        struct backlash_config config;
        
        // 解析通道参数
        if (strcmp("ch12", argv[2]) == 0) {
            config.chx = CH12;
        } else if (strcmp("ch34", argv[2]) == 0) {
            config.chx = CH34;
        } else {
            printf("Invalid channel: %s (use ch12 or ch34)\n", argv[2]);
            close(fd);
            return -1;
        }
        
        // 解析相位数和角度参数
        config.compensation_phases = (u32)strtoul(argv[3], NULL, 0);
        config.compensation_angle = (u32)strtoul(argv[4], NULL, 0);
        
        // 解析 enabled 参数（可选，默认为 true）
        if (argc == 6) {
            int enabled_val = atoi(argv[5]);
            config.enabled = (enabled_val != 0);
        } else {
            config.enabled = true;  // 默认启用
        }
        
        printf("Setting backlash compensation for %s:\n", argv[2]);
        printf("  Phases: %u\n", config.compensation_phases);
        printf("  Angle: %u\n", config.compensation_angle);
        printf("  Enabled: %s\n", config.enabled ? "Yes" : "No");
        
        ret = ioctl(fd, SET_BACKLASH_COMPENSATION, &config);
        if (ret < 0) {
            perror("Failed to set backlash compensation");
            close(fd);
            return -1;
        }
        
        printf("Backlash compensation set successfully\n");
        close(fd);
        return 0;
    }

    // 检查是否是获取虚位补偿命令
    if (argc == 3 && strcmp("get_backlash", argv[1]) == 0)
    {
        struct backlash_config config;
        
        // 解析通道参数
        if (strcmp("ch12", argv[2]) == 0) {
            config.chx = CH12;
        } else if (strcmp("ch34", argv[2]) == 0) {
            config.chx = CH34;
        } else {
            printf("Invalid channel: %s (use ch12 or ch34)\n", argv[2]);
            close(fd);
            return -1;
        }
        
        printf("Getting backlash compensation for %s...\n", argv[2]);
        
        ret = ioctl(fd, GET_BACKLASH_COMPENSATION, &config);
        if (ret < 0) {
            perror("Failed to get backlash compensation");
            close(fd);
            return -1;
        }
        
        printf("Backlash Compensation Configuration for %s:\n", argv[2]);
        printf("  Compensation Phases: %u\n", config.compensation_phases);
        printf("  Compensation Angle: %u\n", config.compensation_angle);
        printf("  Enabled: %s\n", config.enabled ? "Yes" : "No");
        printf("  Equivalent Angle: %.3f degrees\n", 
               (double)config.compensation_phases * 360.0 / (CYCLES_CONVERT_PHASE * 8192));
        
        close(fd);
        return 0;
    }

    // 检查是否是状态查询命令
    if (argc == 2 && strcmp("status", argv[1]) == 0)
    {
        ret = show_status(fd);
        close(fd);
        return ret;
    }

    // 检查参数数量
    if (argc < 7)
    {
        print_usage(argv[0]);
        close(fd);
        return -1;
    }

    // 初始化结构体
    memset(&chx, 0, sizeof(chx));
    chx.chx = CH12;
    chx.subdivide = SUBDIVIDE64; // 默认64细分（会被speed配置覆盖）
    chx.speed = MOTOR_SPEED_22_10_7;  // 默认速度档位

    // 解析命令行参数
    int channel;
    int has_speed_param = 0;  // 标记是否使用speed参数
    if (parse_args(argc, argv, &chx, &use_angle, &angle, &channel, &has_speed_param) < 0)
    {
        print_usage(argv[0]);
        close(fd);
        return -1;
    }

    // 根据解析出的通道参数设置chx.chx
    chx.chx = (enum channel_select)channel;

    // 如果使用角度模式，转换为cycles和phase
    if (use_angle)
    {
        printf("Converting angle %.2f degrees to cycles and phase...\n", angle);
        if (angles_to_phases(&chx, angle) < 0)
        {
            printf("Error: Failed to convert angle to phases\n");
            close(fd);
            return -1;
        }
    }

    // 打印设置信息
    printf("\n=== Motor Configuration ===\n");
    printf("Mode         : %s\n", has_speed_param ? "Speed Gear Mode" : "Test Mode (Manual Parameters)");
    printf("Channel      : %s\n", (chx.chx == CH12) ? "CH12" : "CH34");
    int actual_subdivide = (chx.subdivide == SUBDIVIDE16) ? 16 :
                           (chx.subdivide == SUBDIVIDE32) ? 32 :
                           (chx.subdivide == SUBDIVIDE64) ? 64 : 128;
    printf("Subdivide    : %d\n", actual_subdivide);
    printf("Cycles       : %d\n", chx.cycles);
    printf("Phase        : %d\n", chx.phase);
    printf("Total Phase  : %d\n", chx.cycles * 1024 + chx.phase);
    if (has_speed_param)
    {
        printf("Speed Gear   : %d\n", chx.speed);
    }
    else
    {
        printf("Prediv       : %d\n", chx.prediv);
        printf("Div          : %d\n", chx.div);
        printf("Pwmset       : %d\n", chx.pwmset);
    }
    printf("Direction    : %s\n", (chx.direct == FORWARD) ? "FORWARD" : "BACK");
    if (use_angle)
    {
        printf("Angle        : %.2f degrees\n", angle);
    }
    printf("===========================\n\n");

    // 获取起始位置
    calc_val.chx = chx.chx;
    ioctl(fd, GET_CYCLE_CNT, &calc_val);
    
    // 注意：此时 CHAN_START 还没有调用，is_compensating 可能是上次运动的残留状态
    // 起始位置是静止状态，不应该有补偿的概念，直接使用驱动返回的逻辑位置
    u64 start_phase = calc_val.phase_done_toltal;
    
    // 根据逻辑位置计算角度的正负值
    // 使用驱动返回的 dir 来判断正负（dir 是基于原始硬件寄存器值的）
    double start_angle = phases_to_angles(start_phase);
    if (calc_val.dir == 0) {
        start_angle = -start_angle;  // dir=0 表示负角度
    }
    
    printf("Start Position:\n");
    printf("  Cycle: %d, Phase: %d, Total Phase: %llu\n",
           (int)(start_phase / 1024),
           (int)(start_phase % 1024),
           start_phase);
    printf("  Angle: %.2f degrees\n", start_angle);
    printf("\n");

    // 记录开始时间
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    // 启动电机
    printf("Starting motor...\n");
    if (has_speed_param)
    {
        // 使用速度档位模式，调用 CHAN_START
        printf("Using CHAN_START (speed gear mode)\n");
        ret = ioctl(fd, CHAN_START, &chx);
    }
    else
    {
        // 使用测试模式，调用 CHAN_START_TEST
        printf("Using CHAN_START_TEST (manual parameters mode)\n");
        ret = ioctl(fd, CHAN_START_TEST, &chx);
    }
    
    if (ret < 0)
    {
        printf("Error: Failed to start motor (ret = %d)\n", ret);
        close(fd);
        return -1;
    }

    printf("Motor started successfully\n");
    printf("\n");

    // 获取目标位置（启动后立即获取，此时目标已设定）
    targ_val.chx = chx.chx;
    ioctl(fd, GET_TARG_CNT, &targ_val);

    // 直接使用驱动返回的目标位置（物理位置，包含补偿）
    u64 target_phase_total = targ_val.phase_targ_toltal;
    double target_angle = phases_to_angles(target_phase_total);
    if (targ_val.dir == 1)
        target_angle *= 1;
    else    
        target_angle *= -1;
    printf("Target Position (Set):\n");
    printf("  Register: Cycle %d, Phase %d (hardware register value)\n", 
           targ_val.cycles_targ, targ_val.phase_targ);
    printf("  Target Absolute Position: Cycle %d, Phase %d\n",
           (int)(target_phase_total / 1024),
           (int)(target_phase_total % 1024));
    printf("  Target Angle: %.2f degrees\n", target_angle);
    printf("\n");


    // 等待电机运动完成
    int wait_result = wait_motor_idle(fd, &chx, start_phase, target_phase_total);
    
    // 记录结束时间
    gettimeofday(&end_time, NULL);
    printf("\n");

    if (wait_result < 0)
    {
        printf("Error: Motor operation timeout\n");
        close(fd);
        return -1;
    }
    else if (wait_result > 0)
    {
        printf("Warning: Motor stopped before reaching target (possible jam or step loss)\n");
    }

    // 获取最终位置（电机已停止）
    ioctl(fd, GET_CYCLE_CNT, &calc_val);
    
    // 直接使用驱动返回的逻辑位置计算角度
    // 注意：驱动返回的 phase_done_toltal 已经是逻辑位置（已减去补偿）
    double final_angle = phases_to_angles(calc_val.phase_done_toltal);
    if (calc_val.dir == 1)
        final_angle *= 1;
    else
        final_angle *= -1;
    
    // 驱动返回的已经是逻辑位置，直接使用
    u64 final_total = calc_val.phase_done_toltal;
    printf("\nCurrent Position:\n");
    printf("  Cycle: %d, Phase: %d, Total Phase: %llu\n",
           (int)(final_total / 1024),
           (int)(final_total % 1024),
           final_total);
    printf("  Angle: %.2f degrees\n", final_angle);

    printf("  Direction: %s, compensate_status: %d\n", (calc_val.dir == 1) ? "FORWARD" : "BACK", calc_val.compensate_status);

    // 计算转速
    double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + 
                         (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    // 计算实际转动角度（使用之前计算的start_angle）
    double actual_rotation = final_angle - start_angle;
    double rotation_speed = 0.0;
    
    if (elapsed_time > 0.001) { // 避免除零，最小时间1ms
        rotation_speed = fabs(actual_rotation) / elapsed_time;
    }
    
    printf("\n=== Motion Analysis ===\n");
    printf("Start Angle     : %.3f degrees\n", start_angle);
    printf("Final Angle     : %.3f degrees\n", final_angle);
    printf("Actual Rotation : %.3f degrees\n", actual_rotation);
    printf("Elapsed Time    : %.3f seconds\n", elapsed_time);
    printf("Average Speed   : %.3f degrees/second\n", rotation_speed);
    printf("=======================\n");

    // // 计算误差
    // u64 actual_total = calc_val.phase_done_toltal;
    // if (calc_val.compensate_status == COMPENSATE_FORWARD)
    //     actual_total += calc_val.compensation_phases;
    // else if (calc_val.compensate_status == COMPENSATE_BACK)
    //     actual_total -= calc_val.compensation_phases;
    // long long phase_error = (long long)actual_total - (long long)target_phase_total;
    // double angle_error = phase_error * 360.0 / 524288.0;

    // printf("\nPosition Error:\n");
    // printf("  Target: %llu phases, Actual: %llu phases\n", target_phase_total, actual_total);
    // printf("  Phase Error: %lld (%.4f degrees)\n", phase_error, angle_error);
    // printf("\nDone.\n");

    close(fd);
    return 0;
}
