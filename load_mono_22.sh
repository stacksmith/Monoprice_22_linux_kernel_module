#!/bin/sh

# Script called by udev to initialise mono_22 module during usb hotplug event.

logger udev rule triggered to load mono_22
#Make sure mono module is not loaded
rmmod mono_22
logger removed mono_22 driver module from kernel

#Detach the usbhid module
/usr/local/bin/detach_usbhid
logger detached usbhid driver module from mono device

#Now insert Bosto module
insmod /lib/modules/$(uname -r)/kernel/drivers/input/tablet/mono_22.ko
