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
          -I$(SDK_SYSROOT)/usr/lib/glib-2.0/include \
          -I../../third_party/cJSON

LIB_DIR = -L../../third_party/mosquitto/out/lib \
          -L$(SDK_SYSROOT)/usr/lib

LIBS = -lmosquitto -lglib-2.0 -lgio-2.0 -lgobject-2.0 -lrkwifibt -lpthread

OBJS = main.o \
       BleAdvertiser.o \
       BleGattServer.o \
       BleWifiManager.o \
       BleMqttBridge.o \
       BleDeviceInfo.o \
       BleUtility.o \
       ../../third_party/cJSON/cJSON.o \
       ../../third_party/cJSON/wifi_ip.o

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LIB_DIR) $(LIBS) -o $(TARGET)

main.o: main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c main.cpp -o main.o

BleAdvertiser.o: BleAdvertiser.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c BleAdvertiser.cpp -o BleAdvertiser.o

BleGattServer.o: BleGattServer.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c BleGattServer.cpp -o BleGattServer.o

BleWifiManager.o: BleWifiManager.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c BleWifiManager.cpp -o BleWifiManager.o

BleMqttBridge.o: BleMqttBridge.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c BleMqttBridge.cpp -o BleMqttBridge.o

BleDeviceInfo.o: BleDeviceInfo.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c BleDeviceInfo.cpp -o BleDeviceInfo.o

BleUtility.o: BleUtility.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c BleUtility.cpp -o BleUtility.o

../../third_party/cJSON/cJSON.o: ../../third_party/cJSON/cJSON.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

../../third_party/cJSON/wifi_ip.o: ../../third_party/cJSON/wifi_ip.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

clean:
	rm -f $(TARGET) *.o ../../third_party/cJSON/cJSON.o ../../third_party/cJSON/wifi_ip.o
