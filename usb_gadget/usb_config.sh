#!/bin/sh

ADB_EN=on
DFU_EN=off
RNDIS_EN=off
if ( echo $2 |grep -q "off" ); then
ADB_EN=off
fi
if ( echo $1 |grep -q "rndis" ); then
RNDIS_EN=on
fi
USB_FUNCTIONS_DIR=/sys/kernel/config/usb_gadget/rockchip/functions
USB_CONFIGS_DIR=/sys/kernel/config/usb_gadget/rockchip/configs/b.1

# Determine idProduct based on enabled functions (matches SDK usb_pid())
# Reference: Rockchip_Developer_Guide_USB_CN V2.0.0
usb_pid()
{
  if [ "$ADB_EN" = on ] && [ "$RNDIS_EN" = on ]; then
    echo 0x0006  # adb-rndis (use adb PID for rktool compatibility; RNDIS is still present via interface descriptors)
  elif [ "$ADB_EN" = on ]; then
    echo 0x0006  # adb only
  elif [ "$RNDIS_EN" = on ]; then
    echo 0x0003  # rndis only
  else
    echo 0x00ff  # undefined
  fi
}

pre_run_rndis()
{
  RNDIS_STR="rndis"
  if ( echo $1 |grep -q "rndis" ); then
   #sleep 1
   IP_FILE=/data/uvc_xu_ip_save
   echo "config usb0 IP..."
   if [ -f $IP_FILE ]; then
      for line in `cat $IP_FILE`
      do
        echo "save ip is: $line"
        ifconfig usb0 $line
      done
   else
    ifconfig usb0 172.16.110.6
   fi
   ifconfig usb0 up
  fi
}
pre_run_adb()
{
  if [ $ADB_EN = on ];then
    umount /dev/usb-ffs/adb
    mkdir -p /dev/usb-ffs/adb -m 0770
    mount -o uid=2000,gid=2000 -t functionfs adb /dev/usb-ffs/adb
    start-stop-daemon --start --quiet --background --exec /usr/bin/adbd
  fi
}

##main
#init usb config
ifconfig lo up   # for adb ok
#/etc/init.d/S10udev stop
umount /sys/kernel/config
mkdir /dev/usb-ffs
mount -t configfs none /sys/kernel/config
mkdir -p /sys/kernel/config/usb_gadget/rockchip
mkdir -p /sys/kernel/config/usb_gadget/rockchip/strings/0x409
mkdir -p ${USB_CONFIGS_DIR}/strings/0x409
echo 0x2207 > /sys/kernel/config/usb_gadget/rockchip/idVendor
echo 0x0310 > /sys/kernel/config/usb_gadget/rockchip/bcdDevice
echo 0x0200 > /sys/kernel/config/usb_gadget/rockchip/bcdUSB
echo 0 > /sys/kernel/config/usb_gadget/rockchip/bDeviceClass
# bDeviceSubClass / bDeviceProtocol are ignored when bDeviceClass=0 (per-interface)
SERIAL_NUM=`cat /proc/cpuinfo |grep Serial | awk -F ":" '{print $2}'`
echo "serialnumber is $SERIAL_NUM"
echo $SERIAL_NUM > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/serialnumber
echo "rockchip" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/manufacturer
echo "FalconAir" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/product
echo 0x1 > /sys/kernel/config/usb_gadget/rockchip/os_desc/b_vendor_code
echo "MSFT100" > /sys/kernel/config/usb_gadget/rockchip/os_desc/qw_sign
echo 500 > /sys/kernel/config/usb_gadget/rockchip/configs/b.1/MaxPower
#ln -s /sys/kernel/config/usb_gadget/rockchip/configs/b.1 /sys/kernel/config/usb_gadget/rockchip/os_desc/b.1

# Reset config (remove any leftover function links)
if [ -e ${USB_CONFIGS_DIR}/ffs.adb ]; then
   rm -f ${USB_CONFIGS_DIR}/ffs.adb
else
   ls ${USB_CONFIGS_DIR} | grep f[0-9] | xargs -I {} rm ${USB_CONFIGS_DIR}/{}
fi

USB_CNT=0

# ADB first (f1) so Rockchip tools can find it
if [ $ADB_EN = on ];then
  mkdir ${USB_FUNCTIONS_DIR}/ffs.adb
  echo "adb" > ${USB_CONFIGS_DIR}/strings/0x409/configuration
  USB_CNT=$((USB_CNT+1))
  echo "adb on++++++ ${USB_CNT}"
  ln -s ${USB_FUNCTIONS_DIR}/ffs.adb ${USB_CONFIGS_DIR}/f${USB_CNT}
  pre_run_adb
  sleep .5
fi

# RNDIS after ADB (f2)
case "$1" in
rndis)
   mkdir /sys/kernel/config/usb_gadget/rockchip/functions/rndis.gs0
   CONFIG_STR=`cat /sys/kernel/config/usb_gadget/rockchip/configs/b.1/strings/0x409/configuration`
   STR=${CONFIG_STR}_rndis
   echo $STR > ${USB_CONFIGS_DIR}/strings/0x409/configuration
   USB_CNT=$((USB_CNT+1))
   echo "rndis on++++++ ${USB_CNT}"
   ln -s ${USB_FUNCTIONS_DIR}/rndis.gs0 ${USB_CONFIGS_DIR}/f${USB_CNT}
   echo "config rndis..."
   ;;
*)
   echo "config adb only ..."
esac

if [ $DFU_EN = on ];then
  mkdir /sys/kernel/config/usb_gadget/rockchip/functions/dfu.gs0
  CONFIG_STR=`cat /sys/kernel/config/usb_gadget/rockchip/configs/b.1/strings/0x409/configuration`
  STR=${CONFIG_STR}_dfu
  echo $STR > ${USB_CONFIGS_DIR}/strings/0x409/configuration
  USB_CNT=$((USB_CNT+1))
  echo "dfu on++++++ ${USB_CNT}"
  ln -s ${USB_FUNCTIONS_DIR}/dfu.gs0 ${USB_CONFIGS_DIR}/f${USB_CNT}
  ADB_EN=off
  sleep .5
fi

# Set idProduct AFTER determining which functions are enabled
USB_PID=$(usb_pid)
echo "usb_pid: $USB_PID (adb=$ADB_EN rndis=$RNDIS_EN)"
echo $USB_PID > /sys/kernel/config/usb_gadget/rockchip/idProduct

UDC=`ls /sys/class/udc/| awk '{print $1}'`
echo $UDC > /sys/kernel/config/usb_gadget/rockchip/UDC

if [ "$1" ]; then
  pre_run_rndis $1
fi
