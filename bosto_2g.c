/*
 *  USB bosto_2g tablet support
 *
 *  Copyright (c) 2010 Xing Wei <weixing@hanwang.com.cn>
 *
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
#define DRIVER_AUTHOR   "Aidan Walton <aidan@wires3.net>"
#define DRIVER_DESC     "USB Bosto(2nd Gen) tablet driver"
#define DRIVER_LICENSE  "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_HANWANG		0x0b57
#define USB_PRODUCT_BOSTO22HD		0x9016

#define BOSTO_TABLET_INT_CLASS	0x0003
#define BOSTO_TABLET_INT_SUB_CLASS	0x0001
#define BOSTO_TABLET_INT_PROTOCOL	0x0002

#define PKGLEN_MAX	10

/* match vendor and interface info */

#define BOSTO_TABLET_DEVICE(vend, prod, cl, sc, pr) \
    .match_flags = USB_DEVICE_ID_MATCH_INT_INFO \
            | USB_DEVICE_ID_MATCH_DEVICE, \
        .idVendor = (vend), \
        .idProduct = (prod), \
        .bInterfaceClass = (cl), \
        .bInterfaceSubClass = (sc), \
        .bInterfaceProtocol = (pr)


struct bosto_2g {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	char name[64];
	char phys[32];
};

#define PRODUCT_NAME "Bosto Kingtee 22HD"

static const int hw_eventtypes[]= {EV_KEY, EV_ABS};
static const int hw_absevents[] = {ABS_X, ABS_Y, ABS_PRESSURE};
static const int hw_mscevents[] = {};
static const int hw_btnevents[] = { BTN_TOUCH, BTN_TOOL_PEN,
				    BTN_TOOL_RUBBER, BTN_STYLUS2};

static void bosto_2g_parse_packet(struct bosto_2g *bosto_2g ){
  unsigned char *data = bosto_2g->data;
  struct input_dev *input_dev = bosto_2g->dev;
//   struct usb_device *dev = bosto_2g->usbdev;
//   dev_dbg(&dev->dev, "Bosto_packet:  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x Time:%li\n",	  data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9], jiffies);
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

static void bosto_2g_irq(struct urb *urb)
{
  struct bosto_2g *bosto_2g = urb->context;
  //  struct usb_device *dev = bosto_2g->usbdev;
  int retval;
  
  switch (urb->status) {
  case 0:
    bosto_2g_parse_packet(bosto_2g);
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

static int bosto_2g_open(struct input_dev *dev)
{
  struct bosto_2g *bosto_2g = input_get_drvdata(dev);
  
  bosto_2g->irq->dev = bosto_2g->usbdev;
  if (usb_submit_urb(bosto_2g->irq, GFP_KERNEL))
    return -EIO;
  return 0;
}

static void bosto_2g_close(struct input_dev *dev)
{
  struct bosto_2g *bosto_2g = input_get_drvdata(dev);
  usb_kill_urb(bosto_2g->irq);
}


static int bosto_2g_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
  struct usb_device *dev = interface_to_usbdev(intf);
  struct usb_endpoint_descriptor *endpoint;
  struct bosto_2g *bosto_2g;
  struct input_dev *input_dev;
  int error;
  int i;
  
  bosto_2g = kzalloc(sizeof(struct bosto_2g), GFP_KERNEL);
  input_dev = input_allocate_device();
  if (!bosto_2g || !input_dev) {
    error = -ENOMEM;
    goto fail1;
  }
  
  if (!(bosto_2g->data = usb_alloc_coherent(dev,PKGLEN_MAX, GFP_KERNEL, &bosto_2g->data_dma))){
    error = -ENOMEM;
    goto fail1;
  }
  
  if (!(bosto_2g->irq = usb_alloc_urb(0, GFP_KERNEL))){
      error = -ENOMEM;
    goto fail2;
  }
  
  bosto_2g->usbdev = dev;
  bosto_2g->dev = input_dev;
  
  usb_make_path(dev, bosto_2g->phys, sizeof(bosto_2g->phys));
  strlcat(bosto_2g->phys, "/input0", sizeof(bosto_2g->phys));
  
  strlcpy(bosto_2g->name, PRODUCT_NAME, sizeof(PRODUCT_NAME));
  input_dev->name = bosto_2g->name;
  input_dev->phys = bosto_2g->phys;
  usb_to_input_id(dev, &input_dev->id);
  input_dev->dev.parent = &intf->dev;
  
  input_set_drvdata(input_dev, bosto_2g);
  
  input_dev->open = bosto_2g_open;
  input_dev->close = bosto_2g_close;
  
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
  usb_fill_int_urb(bosto_2g->irq, dev,
		   usb_rcvintpipe(dev, endpoint->bEndpointAddress),
		   bosto_2g->data, PKGLEN_MAX,
		   bosto_2g_irq, bosto_2g, endpoint->bInterval);
  bosto_2g->irq->transfer_dma = bosto_2g->data_dma;
  bosto_2g->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
  
  if (input_register_device(bosto_2g->dev))
    goto fail3;
  
  usb_set_intfdata(intf, bosto_2g);
  
  return 0;
  
 fail3:	usb_free_urb(bosto_2g->irq);
 fail2:	usb_free_coherent(dev, PKGLEN_MAX,
			  bosto_2g->data, bosto_2g->data_dma);
 fail1:	input_free_device(input_dev);
  kfree(bosto_2g);
  printk (KERN_INFO "Requesting kernel to free Bosto urb.\n");
  return error;
  
}

static void bosto_2g_disconnect(struct usb_interface *intf)
{
  struct bosto_2g *bosto_2g = usb_get_intfdata(intf);
  
  printk (KERN_INFO "bosto_2g: USB interface disconnected.\n");
  input_unregister_device(bosto_2g->dev);
  usb_free_urb(bosto_2g->irq);
  usb_free_coherent(interface_to_usbdev(intf),
		    PKGLEN_MAX, bosto_2g->data,
		    bosto_2g->data_dma);
  kfree(bosto_2g);
  usb_set_intfdata(intf, NULL);
}

static const struct usb_device_id bosto_2g_ids[] = {
  {USB_DEVICE (USB_VENDOR_ID_HANWANG, USB_PRODUCT_BOSTO22HD)},
  {}
};

MODULE_DEVICE_TABLE(usb, bosto_2g_ids);

static struct usb_driver bosto_2g_driver = {
  .name		= "bosto_2g",
  .probe	= bosto_2g_probe,
  .disconnect	= bosto_2g_disconnect,
  .id_table	= bosto_2g_ids,
};

static int __init bosto_2g_init(void)
{
  printk(KERN_INFO "Bosto 2nd Generation USB Driver module being initialised.\n" );
  return usb_register(&bosto_2g_driver);
}

static void __exit bosto_2g_exit(void)
{
  usb_deregister(&bosto_2g_driver);
}

module_init(bosto_2g_init);
module_exit(bosto_2g_exit);
