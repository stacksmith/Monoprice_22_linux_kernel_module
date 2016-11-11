This is an experimental driver for Monoprice 22

Current status: stable as of 11 Nov 2016.  Please open issues for any bugs you find.

  - BTN_TOOL_PEN and BUTTON_TOOL_RUBBER are signaled as soon as they are in hover range;
  - BTN_TOUCH is set when stylus or rubber touch the screen;
  - BTN_STYLUS2 is set while button pressed;
  - GIMP and KRITA work, but eraser is not recognized, as in all drivers I've seen

Installation
============

To install:
- verify that your computer sees your Monoprice.  `lsusb` should show 'ID 0b57:9016 Beijing HanwangTechnology Co., Ltd';
- `make` to build the driver;
- `sudo make install` to install the driver.

At this point you should be able to use the stylus as the mouse and with GIMP or Krita, etc.

To uninstall, of course
```
sudo make uninstall
```

Troubleshooting
===============

If your stylus is not responsive after hybernation, type `sudo /usr/local/bin/load_mono_22.sh`.  This is a known issue.

You can try checking
- `lsusb` for 0b57:9016;
- `lsmod | grep mono` for mono_22
- `cat /proc/bus/input/devices` to see where your device is attached

With xinput, you can look try
- `xinput` and look for 'Monoprice 22'
- `xinput list --long "Monoprice 22"`
- `while :; do xinput --query-state "Monoprice 22"; sleep 1; done`
- `xinput test "Monoprice 22"`
also,
- `evtest` 

Hacking
=======
Obviously, sudo make uninstall and make clean as needed...
See aidyw's repo for more information about debugging the driver.

References
==========
based on original drivers by  <weixing@hanwang.com.cn>
- http://linux.fjfi.cvut.cz/~taxman/hw/hanvon/
- https://github.com/exaroth/pentagram_virtuoso_drivers/blob/master/hanvon.c
- further modified and improved by aidyw https://github.com/aidyw/bosto-2g-linux-kernel-module

The driver was built to these guidelines:
- https://www.kernel.org/doc/Documentation/input/event-codes.txt
- http://linuxwacom.sourceforge.net/wiki/index.php/Kernel_Input_Event_Overview


