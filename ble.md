bleconfigwifi 运行机制
bleconfigwifi 实际上是 bleConfigureWifi/ 目录下的 ble_wifi_config 程序，是一个 BLE 外设 (GATT Server)，通过 BlueZ D-Bus API 实现，用于让手机 App 通过 BLE 配网。

核心架构

main() → 初始化日志 → 连接本地MQTT → 注册BLE广播 → 注册GATT服务 → GLib事件循环
BLE 广播 (ble_advertise.c): 每秒更新广播包，包含 STA 数量、IP 地址、固件版本、设备颜色、直播状态等
GATT 服务 (ble_gatt.c): 注册 WiFi 配网服务 (UUID 0000ff00-...)，包含 8 个特征值 (FF01-FF08)，支持手机写入 WiFi 凭证、读取扫描列表、获取连接状态等
WiFi 操作 (ble_wifi_config.c): 通过 iw/hostapd_cli/RK_wifi_* API 实现扫描、连接、断开、获取 STA 数量/IP 等
MQTT 通信 (ble_mqtt.c): 与系统其他模块通过本地 MQTT broker 通信，订阅直播状态、温度告警、手机连接状态等主题

产测模式干了什么
产测模式通过编译宏 -DFACTORY_MODE 和运行时检查 /userdata/factory_mode 文件是否存过来激活。

启动时阻塞等待 SN：程序启动后一直 sleep(1) 循环，直到通过 MQTT 收到有效的 SN 码（14 字节）
MQTT 订阅产测主题：订阅 "10R" 和 "31R" 主题，上位机通过这些主题发送设备 SN
BLE 广播改为广播 SN：不再广播正常的 WiFi/IP/状态数据，改为在 BLE 广播包中直接广播设备的 14 字节 SN 码，产测设备扫描 BLE 即可读取 SN
广播完一次后重置：SN 广播一次后 is_valid_sn_pcba 置为 false，等待下一个 SN
简单说：产测模式下，上位机通过 MQTT 下发 SN → ble_wifi_config 收到后通过 BLE 广播出去 → 产测扫描设备读取 SN 完成设备识别。

是否依赖 ble_central
不依赖。 两者完全独立：

bleConfigureWifi	ble_central
角色	BLE 外设 (Peripheral)	BLE 中心设备 (Central)
功能	广播 + 提供 GATT 服务供手机连接配网	扫描并连接远程 BLE 设备（如遥控器）
目录	bleConfigureWifi/	ble_central/
构建产物	ble_wifi_config	out/ble_central
两者没有共享任何源码或头文件，各自独立编译，只是都使用了 commons/ 下的公共库（mosquitto、glib、cJSON 等）和 MQTT 进行系统通信。


产测上位机

在蓝牙测试中是扫描Xbt-F-前缀的SSID，然后再读每个蓝牙的广播数据，数据为SN码

