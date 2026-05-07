#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "tmi8152.h"



static int print_time_jk(void)
{	
	struct timeval tv;
    struct tm *time_info;
    char time_string[80];
    
    // 获取当前时间（含微秒）
    gettimeofday(&tv, NULL);
    time_info = localtime(&tv.tv_sec);
    
    strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", time_info);
    printf("jk debug current time: %s.%06ld\n", time_string, tv.tv_usec);
    
    return 0;
}




static void print_cycles(int fd)
{
	int cycles;
	struct chx_enable chx;
	chx.chx = CH34;
	
	cycles = ioctl(fd, GET_CYCLE_CNT, &chx.chx);
	
	printf("cycles = %d\n", cycles);
}


static void wait_motor_idle(int fd, struct chx_enable *chx)
{
	int cycles, back_cycles;
	
	if(chx->direct == FORWARD)
	{
		do {
			cycles = ioctl(fd, GET_CYCLE_CNT, &chx->chx);
			//printf("drv ret cycles = %d, chx->cycles = %d\n", cycles, chx->cycles);
		} while(cycles < chx->cycles);
	}
	else if(chx->direct == BACK)
	{
	    back_cycles = MOTOR_CYCLES_MAX - chx->cycles;
		do {
			cycles = ioctl(fd, GET_CYCLE_CNT, &chx->chx);
			//printf("drv ret cycles = %d, back_cycles = %d\n", cycles, back_cycles);
		} while(cycles != back_cycles);
	}
}



int main(int argc, char** argv)
{
	struct chx_enable chx;
	struct motor_status status;
	int ret, angle, fd, subdiv, speed;
	unsigned int again, i = 1;


	//memset(&chx, 0, sizeof(struct chx_enable));
	//memset(&status, 0, sizeof(struct motor_status));
	//memset(&clk, 0, sizeof(struct chx_clk));
	//memset(&speed, 0, sizeof(struct chx_speed));

	

    fd = open("/dev/tmi8152", O_RDWR);
    if (fd < 0)
	{
        printf("open motor tmi8152 device failed!\n");
        return -1;
    }





	/* 启动电机
	 * ./spi_motor_test start -ch 1 f -speed h -angle 100
	 */

	if (!strcmp("start", argv[1]))
	{
		if (argc < 9)
		{	
			printf("Usge: ./spi_motor_test start -ch 1 f -speed h -angle 100\n");
			//printf("Usge: ./spi_motor_test start -ch 1 f -speed h -angle 100 -test 50\n");
			return -1;
		}

	
		if (!strcmp("-ch", argv[2]))
		{
			if (1 == atoi(argv[3]))
			{
				chx.chx = CH12;
			}
			else if (2 == atoi(argv[3]))
			{
				chx.chx = CH34;
			}
			else
			{
				printf("ch select only 1 or 2\n");
				return -1;
			}
		}
		else
		{
			printf("Usge: ./spi_motor_test start -ch 1 f -speed h -angle 100\n");
		}


		if (!strcmp("f", argv[4]))
		{
			chx.direct = FORWARD;
		}
		else if (!strcmp("b", argv[4]))
		{
			chx.direct = BACK;
		}
		else
		{
			printf("deirct select only in f or b\n");
			return -1;
		}

		//chx.subdivide = SUBDIVIDE64;
		chx.subdivide = SUBDIVIDE128;
		if (!strcmp("-subdiv", argv[5]))
		{
			subdiv = atoi(argv[6]);
			
			switch(subdiv)
			{
				case 16:
					printf("set subdiv 16\n");
					chx.subdivide = SUBDIVIDE16;
					break;
				
				case 32:
					printf("set subdiv 32\n");
					chx.subdivide = SUBDIVIDE32;
					break;
					
				case 64:
					printf("set subdiv 64\n");
					chx.subdivide = SUBDIVIDE64;
					break;
				
				case 128:
					printf("set subdiv 128\n");
					chx.subdivide = SUBDIVIDE128;
					break;
				default:
					printf("subdiv only select in 16/32/64/128\n");
					chx.subdivide = SUBDIVIDE_NONE;
			}
		}
		else
		{
			printf("please set -subdiv in 16/32/64/128\n");
			return -1;
		}



		if (!strcmp("-speed", argv[7]))
		{
			speed = atoi(argv[8]);
		
			switch(speed)
			{
				case 1:
					printf("set speed 1\n");
					chx.divsel = DIVIDE1;
					break;
				
				case 2:
					printf("set speed 2\n");
					chx.divsel = SPEED_72_00;
					break;
					
				case 3:
					printf("set speed 3\n");
					chx.divsel = SPEED_51_43;
					break;
				
				case 4:
					printf("set speed 4\n");
					chx.divsel = SPEED_36_00;
					break;
					
				case 5:
					printf("set speed 5\n");
					chx.divsel = SPEED_30_00;
					break;
				
				case 6:
					printf("set speed 6\n");
					chx.divsel = SPEED_25_71;
					break;
					
				case 7:
					printf("set speed 7\n");
					chx.divsel = SPEED_21_82;
					break;
				
				case 8:
					printf("set speed 8\n");
					chx.divsel = SPEED_18_95;
					break;

				case 9:
					printf("set speed 9\n");
					chx.divsel = SPEED_17_14;
					break;
				
				case 10:
					printf("set speed 10\n");
					chx.divsel = SPEED_15_32;
					break;
					
				case 11:
					printf("set speed 11\n");
					chx.divsel = SPEED_13_85;
					break;
				
				case 12:
					printf("set speed 12\n");
					chx.divsel = SPEED_12_85;
					break;
					
				case 13:
					printf("set speed 13\n");
					chx.divsel = SPEED_11_62;
					break;
				
				case 14:
					printf("set speed 14\n");
					chx.divsel = SPEED_10_91;
					break;
					
				case 15:
					printf("set speed 15\n");
					chx.divsel = SPEED_10_00;
					break;
				
				case 16:
					printf("set speed 16\n");
					chx.divsel = SPEED_09_47;
					break;

				case 17:
					printf("set speed 17\n");
					chx.divsel = SPEED_09_00;
					break;
#if 0				
				case 18:
					printf("set speed 18\n");
					chx.divsel = SPEED_08_37;
					break;
					
				case 19:
					printf("set speed 19\n");
					chx.divsel = SPEED_08_00;
					break;
				
				case 20:
					printf("set speed 20\n");
					chx.divsel = SPEED_07_50;
					break;
					
				case 21:
					printf("set speed 21\n");
					chx.divsel = SPEED_07_20;
					break;
				
				case 22:
					printf("set speed 22\n");
					chx.divsel = SPEED_06_92;
					break;
					
				case 23:
					printf("set speed 23\n");
					chx.divsel = SPEED_06_60;
					break;
				
				case 24:
					printf("set speed 24\n");
					chx.divsel = SPEED_06_26;
					break;
				
				case 25:
					printf("set speed 25\n");
					chx.divsel = SPEED_06_00;
					break;
				
				case 26:
					printf("set speed 26\n");
					chx.divsel = SPEED_05_81;
					break;
					
				case 27:
					printf("set speed 27\n");
					chx.divsel = SPEED_05_62;
					break;
				
				case 28:
					printf("set speed 28\n");
					chx.divsel = SPEED_05_45;
					break;
					
				case 29:
					printf("set speed 29\n");
					chx.divsel = SPEED_05_29;
					break;
				
				case 30:
					printf("set speed 30\n");
					chx.divsel = SPEED_05_07;
					break;
					
				case 31:
					printf("set speed 31\n");
					chx.divsel = SPEED_04_93;
					break;
				
				case 32:
					printf("set speed 32\n");
					chx.divsel = SPEED_04_77;
					break;
#endif
				default:
					printf("set NOT_DIV\n");
					chx.divsel = NOT_DIV;
			}
		}
		else
		{
			printf("Usge: ./spi_motor_test start -ch 1 f -speed x -angle 100\n");
			return -1;
		}


		if(!strcmp("-angle", argv[9]))
		{
			angle = atoi(argv[10]);
			chx.cycles = (angle*512/360);
			//chx.cycles = 512;//512步为一圈
		}
		else
		{
			printf("Usge: ./spi_motor_test start -ch 1 f -subdiv 128 -speed x -angle 100\n");
			return -1;
		}


		
		//chx.chx = CH34;
		//chx.subdivide = SUBDIVIDE64;
		//chx.direct = FORWARD;
		//chx.cycles = atoi(argv[3]);
		chx.phase  = 0;

		printf("first time start motor\n");
		print_time_jk();

	    ret = ioctl(fd, CHAN_START, &chx);//启动第一次参数
		if(ret < 0)
		{
			printf("ioctl fail ret = %d\n", ret);
			return -1;
		}

		if(argc < 12)
			return 0;

		//printf("Usge: ./spi_motor_test start -ch 1 f -speed x -angle 100 -test 50\n");
		if(!strcmp("-test", argv[11]))
		{
			again = atoi(argv[12]) - 1;
		}
		else
		{
			wait_motor_idle(fd, &chx);
			print_time_jk();
		}

		while(again)
		{
			wait_motor_idle(fd, &chx);
			again--;
			print_time_jk();
			
			if(chx.direct == FORWARD)
			{
				chx.direct = BACK;
			}
			else
			{
				chx.direct = FORWARD;
			}

			ret = ioctl(fd, CHAN_START, &chx);//启动后续的参数
			if(ret < 0)
			{
				printf("ioctl fail ret = %d, again = %d\n", ret, again);
				return -1;
			}
		}
		
	}
	else if(!strcmp("show", argv[1]))
	{
		int i = 3;
		while(i > 0)
		{
			print_cycles(fd);
			i--;
		}
	}
	else if(!strcmp("stop", argv[1]))
	{
		if(!strcmp("-ch", argv[2]))
		{
			if(1 == atoi(argv[3]))
			{
				chx.chx = CH12;
			}
			else if(2 == atoi(argv[3]))
			{
				chx.chx = CH34;
			}
			else
			{
				printf("ch select only 1 or 2\n");
				return -1;
			}
		}
		else
		{
			printf("Usge: ./spi_motor_test stop -ch 1\n");
		}
	
		//printf("stop motor\n");
		//chx.chx = CH34;
		chx.direct = STOP;
		chx.cycles = 0;
		chx.phase  = 0;
		ioctl(fd, CHAN_STOP, &chx);
	}
	else if(!strcmp("into_stanby", argv[1]))
	{
		printf("into stanby\n");
		ioctl(fd, ENTER_STANBY);
	}
	else if(!strcmp("out_stanby", argv[1]))
	{
		printf("exit stanby\n");
		ioctl(fd, EXIT_STANBY);
	}
	else if(!strcmp("disable_ch34", argv[1]))
	{
		printf("disable ch34\n");
		ioctl(fd, DISABLE_CH34);
	}
	else if(!strcmp("close_chip", argv[1]))
	{
		printf("close chip\n");
		ioctl(fd, CLOSE_CHIP);
	}
	else if(!strcmp("get_status", argv[1]))
	{
		ioctl(fd, GET_STATUS, &status);

		printf("temp = %d\n", status.temp);
		printf("motor status = %d\n", status.work_status);
	}
	else if(!strcmp("clr_cnt", argv[1]))
	{
		struct chx_enable chx;
		chx.chx = CH34;

		ioctl(fd, CLR_CYCLE_CNT, &chx.chx);
	}
	else if(!strcmp("test_clr_cnt", argv[1]))
	{
		struct chx_enable chx;
		chx.chx = CH34;
		
		print_cycles(fd);
		ioctl(fd, CLR_CYCLE_CNT, &chx.chx);
		print_cycles(fd);
	}
	else
	{
		printf("Usge: ./spi_motor_test start | stop | into_stanby | out_stanby | disable_ch34 | close_chip | set_pwd | set_div\n");
	}




    return 0;
}

