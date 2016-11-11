CC=gcc
INC=-I/usr/include/libusb-1.0 
LIB=-L/lib/$(arch)-linux-gnu/libusb-1.0.so.0

.PHONY: all clean archive

obj-m += mono_22.o
 
all:
	$(CC) detach_usbhid.c $(INC) $(LIB) -lusb-1.0 -o detach_usbhid
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
 
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm detach_usbhid

archive:
	tar f - --exclude=.git -C ../ -c mono_22 | gzip -c9 > ../mono_22-`date +%Y%m%d`.tgz

install:
	cp ./mono_22.ko /lib/modules/$(shell uname -r)/kernel/drivers/input/tablet/
	echo mono_22 >> /etc/modules
	depmod
	cp ./load_mono_22.sh /usr/local/bin
	cp ./detach_usbhid /usr/local/bin
	cp ./load_mono_22.rules /etc/udev/rules.d
	/sbin/udevadm control --reload
	modprobe mono_22

uninstall:
	rm /usr/local/bin/load_mono_22.sh
	rm /usr/local/bin/detach_usbhid
	rm /etc/udev/rules.d/load_mono_22.rules
	/sbin/udevadm control --reload
	rm /lib/modules/$(shell uname -r)/kernel/drivers/input/tablet/mono_22.ko
	sed -i '/mono_22/d' /etc/modules
	depmod
	rmmod mono_22
