cross_compile = ${SDK_DIR}/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-

KERNEL_DIR := $(SDK_DIR)/kernel-6.1

MODULE_NAME := battery

obj-m := $(MODULE_NAME).o

$(MODULE_NAME)-objs := om70x0x_battery.o

ARCH := arm64

EXTRA_CFLAGS += -DMODULE

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(cross_compile) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
