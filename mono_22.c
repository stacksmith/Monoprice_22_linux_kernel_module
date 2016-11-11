/*
 *  USB monoprice 22tablet support
 *
 *  (c) 2016 stacksmith
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/* This is an attempt to implement simple/generic touchpad protocol
 http://linuxwacom.sourceforge.net/wiki/index.php/Kernel_Input_Event_Overview
 https://www.kernel.org/doc/Documentation/input/event-codes.txt

based on original drivers by  <weixing@hanwang.com.cn>
http://linux.fjfi.cvut.cz/~taxman/hw/hanvon/
https://github.com/exaroth/pentagram_virtuoso_drivers/blob/master/hanvon.c
further modified and improved by aidyw
https://github.com/aidyw/bosto-2g-linux-kernel-module
*/
#include <linux/jiffies.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <asm/unaligned.h>
#define DRIVER_AUTHOR   "stacksmith <fpgasm@apple2.x10.mx>"
#define DRIVER_DESC     "USB Monoprice 22 tablet driver"
#define DRIVER_LICENSE  "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_HANWANG		0x0b57
#define USB_PRODUCT_MONOPRICE22		0x9016
#define PKT_LEN_MAX	10

/* match vendor and interface info */

struct smono {
  unsigned char *data;
  dma_addr_t data_dma;
  struct input_dev *dev;
  struct usb_device *usbdev;
  struct urb *irq;
  char name[64];
  char phys[32];
};

#define PRODUCT_NAME "Monoprice 22"

static const int hw_eventtypes[]= {EV_KEY, EV_ABS};
static const int hw_absevents[] = {ABS_X, ABS_Y, ABS_PRESSURE};
static const int hw_mscevents[] = {};
static const int hw_btnevents[] = { BTN_TOUCH, BTN_TOOL_PEN,
				    BTN_TOOL_RUBBER, BTN_STYLUS2};

static void mono_22_parse_packet(struct smono *buffer ){
  unsigned char *data = buffer->data;
  struct input_dev *input_dev = buffer->dev;
//   struct usb_device *dev = buffer->usbdev;
//   dev_dbg(&dev->dev, "mono_22_packet:  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x Time:%li\n",	  data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9], jiffies);
  unsigned char data1 = data[1];   
  if(0x80==data1){ //idle on withdraw, pre tool-change
    input_report_key(input_dev, BTN_TOUCH, 0);
    input_report_key(input_dev, BTN_TOOL_PEN, 0);
    input_report_key(input_dev, BTN_TOOL_RUBBER, 0);
    input_report_key(input_dev, BTN_STYLUS2, 0);
  } else
    if (0xC2 == data1) { //tool change
	unsigned char data_tool = data[3]&0xF0;
        input_report_key(input_dev, BTN_TOOL_PEN,    (0x20==data_tool));
        input_report_key(input_dev, BTN_TOOL_RUBBER, (0xA0==data_tool));
    } else { //A0=prox,E0=touch
      input_report_key(input_dev, BTN_TOUCH,   (0xE0==(data1&0xE0)));
      input_report_key(input_dev, BTN_STYLUS2, data1&0x02);
      input_report_abs(input_dev,ABS_X,get_unaligned_be16(&data[2]));
      input_report_abs(input_dev,ABS_Y,get_unaligned_be16(&data[4]));
      input_report_abs(input_dev,ABS_PRESSURE,(get_unaligned_be16(&data[6]) >> 6) << 1);
    }
  input_sync(input_dev);
}

static void mono_22_irq(struct urb *urb)
{
  struct smono *buffer = urb->context;
  //  struct usb_device *dev = buffer->usbdev;
  int retval;
  
  switch (urb->status) {
  case 0:
    mono_22_parse_packet(buffer);
    break;
  case -ECONNRESET:
  case -ENOENT:
  case -EINPROGRESS:
  case -ESHUTDOWN:
  case -ENODEV:
  default:
    printk("%s - nonzero urb status received: %d", __func__, urb->status);
    break;
  }
  
  retval = usb_submit_urb(urb, GFP_ATOMIC);
  if (retval)
    printk("%s - usb_submit_urb failed with result %d", __func__, retval);
}

static int mono_22_open(struct input_dev *dev)
{
  struct smono *buffer = input_get_drvdata(dev);
  
  buffer->irq->dev = buffer->usbdev;
  if (usb_submit_urb(buffer->irq, GFP_KERNEL))
    return -EIO;
  return 0;
}

static void mono_22_close(struct input_dev *dev)
{
  struct smono *buffer = input_get_drvdata(dev);
  usb_kill_urb(buffer->irq);
}


static int mono_22_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
  struct usb_device *dev = interface_to_usbdev(intf);
  struct usb_endpoint_descriptor *endpoint;
  struct smono *buffer;
  struct input_dev *input_dev;
  int error;
  int i;
  
  buffer = kzalloc(sizeof(struct smono), GFP_KERNEL);
  input_dev = input_allocate_device();
  if (!buffer || !input_dev) {
    error = -ENOMEM;
    goto fail1;
  }
  
  if (!(buffer->data = usb_alloc_coherent(dev,PKT_LEN_MAX, GFP_KERNEL, &buffer->data_dma))){
    error = -ENOMEM;
    goto fail1;
  }
  
  if (!(buffer->irq = usb_alloc_urb(0, GFP_KERNEL))){
      error = -ENOMEM;
    goto fail2;
  }
  
  buffer->usbdev = dev;
  buffer->dev = input_dev;
  
  usb_make_path(dev, buffer->phys, sizeof(buffer->phys));
  strlcat(buffer->phys, "/input0", sizeof(buffer->phys));
  
  strlcpy(buffer->name, PRODUCT_NAME, sizeof(PRODUCT_NAME));
  input_dev->name = buffer->name;
  input_dev->phys = buffer->phys;
  usb_to_input_id(dev, &input_dev->id);
  input_dev->dev.parent = &intf->dev;
  
  input_set_drvdata(input_dev, buffer);
  
  input_dev->open = mono_22_open;
  input_dev->close = mono_22_close;
  
  for (i = 0; i < ARRAY_SIZE(hw_eventtypes); ++i)
    __set_bit(hw_eventtypes[i], input_dev->evbit);
  
  for (i = 0; i < ARRAY_SIZE(hw_absevents); ++i)
    __set_bit(hw_absevents[i], input_dev->absbit);
  
  for (i = 0; i < ARRAY_SIZE(hw_btnevents); ++i)
    __set_bit(hw_btnevents[i], input_dev->keybit);
  
  for (i = 0; i < ARRAY_SIZE(hw_mscevents); ++i)
    __set_bit(hw_mscevents[i], input_dev->mscbit);
  
  input_set_abs_params(input_dev, ABS_X,0, 0xDBE8, 0, 0);
  input_abs_set_res(input_dev, ABS_X,0x56);	
  input_set_abs_params(input_dev, ABS_Y, 0,0x7BB4, 0, 0);
  input_abs_set_res(input_dev, ABS_Y, 0x6E);
  input_set_abs_params(input_dev, ABS_PRESSURE, 0, 0x7FF, 0, 0);
  
  __set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
  
  endpoint = &intf->cur_altsetting->endpoint[0].desc;
  usb_fill_int_urb(buffer->irq, dev,
		   usb_rcvintpipe(dev, endpoint->bEndpointAddress),
		   buffer->data, PKT_LEN_MAX,
		   mono_22_irq, buffer, endpoint->bInterval);
  buffer->irq->transfer_dma = buffer->data_dma;
  buffer->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
  
  if (input_register_device(buffer->dev))
    goto fail3;
  
  usb_set_intfdata(intf, buffer);
  
  return 0;
  
 fail3:	usb_free_urb(buffer->irq);
 fail2:	usb_free_coherent(dev, PKT_LEN_MAX,
			  buffer->data, buffer->data_dma);
 fail1:	input_free_device(input_dev);
  kfree(buffer);
  return error;
  
}

static void mono_22_disconnect(struct usb_interface *intf)
{
  struct smono *buffer = usb_get_intfdata(intf);
  
  printk (KERN_INFO "mono_22: USB interface disconnected.\n");
  input_unregister_device(buffer->dev);
  usb_free_urb(buffer->irq);
  usb_free_coherent(interface_to_usbdev(intf),
		    PKT_LEN_MAX, buffer->data,
		    buffer->data_dma);
  kfree(buffer);
  usb_set_intfdata(intf, NULL);
}

static const struct usb_device_id mono_22_ids[] = {
  {USB_DEVICE (USB_VENDOR_ID_HANWANG, USB_PRODUCT_MONOPRICE22)},
  {}
};

MODULE_DEVICE_TABLE(usb, mono_22_ids);

static struct usb_driver mono_22_driver = {
  .name		= "mono_22",
  .probe	= mono_22_probe,
  .disconnect	= mono_22_disconnect,
  .id_table	= mono_22_ids,
};

static int __init mono_22_init(void)
{
  printk(KERN_INFO "Monoprice 22 USB Driver module being initialised.\n" );
  return usb_register(&mono_22_driver);
}

static void __exit mono_22_exit(void)
{
  usb_deregister(&mono_22_driver);
}

module_init(mono_22_init);
module_exit(mono_22_exit);
