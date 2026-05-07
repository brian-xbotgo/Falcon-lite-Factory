SDK_DIR      ?= /home/gdh/RV1126B_LINUX6.1
CROSS_COMPILE = $(SDK_DIR)/buildroot/output/rockchip_rv1126b_ipc_64_evb1_v10/rockchip_rv1126b_ipc/host/bin/aarch64-buildroot-linux-gnu-

CC       = $(CROSS_COMPILE)gcc
CXX      = $(CROSS_COMPILE)g++
TARGET   = ble_factory_advertise
CXXFLAGS = -Wall -O2 -s -std=c++17
CFLAGS   = -Wall -O2 -s

SDK_SYSROOT = $(SDK_DIR)/buildroot/output/rockchip_rv1126b_ipc_64_evb1_v10/rockchip_rv1126b_ipc/host/aarch64-buildroot-linux-gnu/sysroot

CXXFLAGS += --sysroot=$(SDK_SYSROOT)
CFLAGS   += --sysroot=$(SDK_SYSROOT)

INCLUDE = -I../../include \
          -I../../third_party/mosquitto/out/include \
          -I$(SDK_SYSROOT)/usr/include/glib-2.0 \
          -I$(SDK_SYSROOT)/usr/lib/glib-2.0/include

LIB_DIR = -L../../third_party/mosquitto/out/lib
LIBS    = -lmosquitto -lglib-2.0 -lgio-2.0 -lgobject-2.0 -lpthread

OBJS = main.o ../hal/BleAdvertiser.o

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(LIB_DIR) $(LIBS) -o $(TARGET)

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c main.cpp $(INCLUDE) -o main.o

../hal/BleAdvertiser.o: ../hal/BleAdvertiser.cpp
	$(CXX) $(CXXFLAGS) -c ../hal/BleAdvertiser.cpp $(INCLUDE) -o ../hal/BleAdvertiser.o

clean:
	rm -f $(TARGET) main.o ../hal/BleAdvertiser.o
