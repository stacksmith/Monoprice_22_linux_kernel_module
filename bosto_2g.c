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
#define USB_PRODUCT_BOSTO14WA		0x9018
#define USB_PRODUCT_ART_MASTER_III	0x852a
#define USB_PRODUCT_ART_MASTER_HD	0x8401

#define BOSTO_TABLET_INT_CLASS	0x0003
#define BOSTO_TABLET_INT_SUB_CLASS	0x0001
#define BOSTO_TABLET_INT_PROTOCOL	0x0002

#define PKGLEN_MAX	10

/* device IDs */
//#define STYLUS_DEVICE_ID	0x02
//#define TOUCH_DEVICE_ID		0x03
//#define CURSOR_DEVICE_ID	0x06
//#define ERASER_DEVICE_ID	0x0A
//#define PAD_DEVICE_ID		0x0F

/* match vendor and interface info */

#define BOSTO_TABLET_DEVICE(vend, prod, cl, sc, pr) \
    .match_flags = USB_DEVICE_ID_MATCH_INT_INFO \
            | USB_DEVICE_ID_MATCH_DEVICE, \
        .idVendor = (vend), \
        .idProduct = (prod), \
        .bInterfaceClass = (cl), \
        .bInterfaceSubClass = (sc), \
        .bInterfaceProtocol = (pr)


enum bosto_2g_tablet_type {
	HANWANG_BOSTO_22HD,
	HANWANG_BOSTO_14WA,
	HANWANG_ART_MASTER_III,
	HANWANG_ART_MASTER_HD,
};


struct bosto_2g {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	const struct bosto_2g_features *features;
  //	unsigned int current_tool;
  //	bool stylus_btn_state;
  //	bool stylus_prox;
	char name[64];
	char phys[32];
};

struct bosto_2g_features {
	unsigned short pid;
	char *name;
	enum bosto_2g_tablet_type type;
	int pkg_len;
	int max_x;
	int res_x;
	int max_y;
	int res_y;
	int max_pressure;
};

//PKGLEN_MAX, (0x27de*2), 0x15, (0x1cfe*2), 0x1B, 0x07FF },
static const struct bosto_2g_features features_array[] = {
	{ USB_PRODUCT_BOSTO22HD, "Bosto Kingtee 22HD", HANWANG_BOSTO_22HD,
		PKGLEN_MAX, 0xdbe8, 0x56, 0x7BB3, 0x6e, 0x07FF },					// Max values scale up by 4 to get resultion accurate
	{ USB_PRODUCT_BOSTO14WA, "Bosto Kingtee 14WA", HANWANG_BOSTO_14WA,
		PKGLEN_MAX, 0x27de, 0x1cfe, 0x07FF },
};

static const int hw_eventtypes[] = {
	EV_KEY, EV_ABS, EV_MSC,
};

static const int hw_absevents[] = {
	ABS_X, ABS_Y, ABS_PRESSURE, ABS_MISC,
};



static const int hw_mscevents[] = {
	MSC_SERIAL,
};


static const int hw_btnevents[] = {
  BTN_TOUCH, BTN_TOOL_PEN,  BTN_STYLUS //, BTN_STYLUS2
};


static void bosto_2g_parse_packet(struct bosto_2g *bosto_2g ){
  unsigned char *data = bosto_2g->data;
  struct input_dev *input_dev = bosto_2g->dev;
  
   struct usb_device *dev = bosto_2g->usbdev;
   dev_dbg(&dev->dev, "Bosto_packet:  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x Time:%li\n",	  data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9], jiffies);
   
  if(0x80==data[1]){ //idle on withdraw, pre tool-change
    //input_report_key(input_dev, BTN_LEFT, 0);
    input_report_key(input_dev, BTN_TOUCH, 0);
    input_report_key(input_dev, BTN_TOOL_PEN, 0);
    input_report_key(input_dev, BTN_STYLUS, 0);
  } else
    if (0xC2 == data[1]) { //tool change
      //      input_report_key(input_dev, BTN_TOOL_PEN,    (0x20==(data[3]&0xF0)) ? 1 : 0);
      //  input_report_key(input_dev, BTN_TOOL_RUBBER, (0xA0==(data[3]&0xF0)) ? 1 : 0);
    } else { //A0=prox,E0=touch
      input_report_key(input_dev, BTN_TOUCH,   (0xE0==(data[1]&0xE0)));
      input_report_key(input_dev, BTN_TOOL_PEN,(0xA0==(data[1]&0xA0))); //proximity
      input_report_key(input_dev, BTN_STYLUS, data[1]&0x02);
      input_report_abs(input_dev,ABS_X,get_unaligned_be16(&data[2]));
      input_report_abs(input_dev,ABS_Y,get_unaligned_be16(&data[4]));
      input_report_abs(input_dev,ABS_PRESSURE,(get_unaligned_be16(&data[6]) >> 6) << 1);
    }
  input_event(input_dev, EV_MSC, MSC_SERIAL, bosto_2g->features->pid);
  input_sync(input_dev);

}


/*
static void xbosto_2g_parse_packet(struct bosto_2g *bosto_2g ){
  bool update = false;
  bool xyp_update = false;
  
  unsigned char *data = bosto_2g->data;
  struct input_dev *input_dev = bosto_2g->dev;
  struct usb_device *dev = bosto_2g->usbdev;
  static u16 x = 0;
  static u16 y = 0;
  static u16 p = 0;
  unsigned int pkt_type = 0x00;		// Default undefined
  if(data[1] == 0x80) pkt_type = 1;	// Idle or tool status update in next few packets
  if(data[1] == 0xC2) pkt_type = 2;	// tool status update packet
  if(((data[1] & 0xF0) == 0xA0) | ((data[1] & 0xF0) == 0xE0)) pkt_type = 3;	// In proximity float 0xA0  or touch 0xE0
  
  
  dev_dbg(&dev->dev, "Bosto_packet:  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x Time:%li\n",	  data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9], jiffies);
  
  switch (pkt_type) {
  case 1: //0x80 - idle, toolchange follows
    bosto_2g->stylus_btn_state = false;
    if( bosto_2g->stylus_prox) { //if we were in proximity, withdraw
      // Release all the buttons on tool out
      input_report_key(input_dev, BTN_STYLUS, 0); dev_dbg(&dev->dev, "Bosto BUTTON: BTN_STYLUS released");
      input_report_key(input_dev, BTN_TOUCH, 0); dev_dbg(&dev->dev, "Bosto BUTTON: BTN_TOUCH released");
      input_report_key(input_dev, BTN_TOOL_PEN, 0); dev_dbg(&dev->dev, "Bosto BUTTON: BTN_TOOL_PEN released");
      input_report_key(input_dev, BTN_TOOL_RUBBER, 0); dev_dbg(&dev->dev, "Bosto BUTTON: BTN_TOOL_RUBBER released");
      //input_report_abs(input_dev, ABS_MISC, bosto_2g->current_id);
      //input_event(input_dev, EV_MSC, MSC_SERIAL, bosto_2g->features->pid);
      //input_sync(input_dev);
      bosto_2g->current_tool = 0;
      //bosto_2g->current_tool = 0;
      bosto_2g->stylus_prox = false;
      update = true;
      dev_dbg(&dev->dev, "Bosto TOOL OUT");
    }
    break;
  
  case 2: //0xC2 - tool change
    bosto_2g->stylus_prox = true;
    dev_dbg(&dev->dev, "Bosto TOOL UPDATE");
    {
      unsigned int new_tool = 0;
      switch (data[3] & 0xF0) {
      case 0x20: new_tool = BTN_TOOL_PEN; break;
      case 0xA0: new_tool = BTN_TOOL_RUBBER; break;
      default:
	dev_dbg(&dev->dev, "Unknown tablet tool %02x ", new_tool);
	new_tool = 0; //error condition
      }
      if (new_tool != bosto_2g->current_tool){
	input_report_key(input_dev, bosto_2g->current_tool, 0); //remove old tool
	input_report_key(input_dev, new_tool, 1); //add new tool
        bosto_2g->current_tool = new_tool;
      }
    }
    update = true;
    break;
    
  case 3: //proximity or touch
    bosto_2g->stylus_prox = true;
    x = get_unaligned_be16(&data[2]); //because data2 is high and data3 is low!
    y = get_unaligned_be16(&data[4]);
    p = (get_unaligned_be16(&data[6]) >> 6) << 1; //0 pressure on no touch is consistent
    input_report_key(input_dev, BTN_TOUCH,(data[1] & 0xF0) == 0xE0 ? 1 : 0);
    {
      bool new_state = (data[1] & 0x2);
      if (new_state != bosto_2g->stylus_btn_state){
	input_report_key(input_dev, BTN_STYLUS, new_state); //make press
	bosto_2g->stylus_btn_state = true; //and record for posterity.
	dev_dbg(&dev->dev, "Bosto BUTTON: BTN_STYLUS");
      }
    }	
    xyp_update = true;
    update = true;
    break;
   
  default:
    dev_dbg(&dev->dev, "Error packet. Packet data[1]:  %02x ", data[1]);
  }
  
  if(xyp_update) {
    input_report_abs(input_dev, ABS_X, x);
    input_report_abs(input_dev, ABS_Y, y);
    input_report_abs(input_dev, ABS_PRESSURE, p);
    
    dev_dbg(&dev->dev, "Bosto ABS_X:  %02x ", x);
    dev_dbg(&dev->dev, "Bosto ABS_Y:  %02x ", y);
    dev_dbg(&dev->dev, "Bosto ABS_PRESSURE:  %02x ", p);
  }
  if(update){
    input_report_abs(input_dev, ABS_MISC, bosto_2g->current_tool); //a little bogus - tools not enum'd
    input_event(input_dev, EV_MSC, MSC_SERIAL, bosto_2g->features->pid);
    input_sync(input_dev);
    dev_dbg(&dev->dev, "Bosto ABS_MISC:  %02x ", bosto_2g->current_tool);
    dev_dbg(&dev->dev, "Bosto MSC_SERIAL:  %02x ", bosto_2g->features->pid);
    dev_dbg(&dev->dev, "Bosto EV_SYNC.");
  }
}
*/
static void bosto_2g_irq(struct urb *urb)
{
  struct bosto_2g *bosto_2g = urb->context;
  struct usb_device *dev = bosto_2g->usbdev;
  int retval;
  
  switch (urb->status) {
  case 0:
    /* success */;
    bosto_2g_parse_packet(bosto_2g);
    break;
  case -ECONNRESET:
  case -ENOENT:
  case -EINPROGRESS:
    //dev_err(&dev->dev, "%s - urb in progress status: %d",
    //			__func__, urb->status);
    return;
  case -ESHUTDOWN:
    /* this urb is terminated, clean up */
    //dev_err(&dev->dev, "%s - urb shutting down with status: %d",
    //	__func__, urb->status);
    return;
  case -ENODEV:
    //dev_err(&dev->dev, "%s - Device removed. urb status: %d",
    //__func__, urb->status);
  default:
    //dev_err(&dev->dev, "%s - nonzero urb status received: %d",
    //__func__, urb->status);
    break;
  }
  
  retval = usb_submit_urb(urb, GFP_ATOMIC);
  if (retval)
    dev_err(&dev->dev, "%s - usb_submit_urb failed with result %d",
	    __func__, retval);
}

static int bosto_2g_open(struct input_dev *dev)
{
  struct bosto_2g *bosto_2g = input_get_drvdata(dev);
  
  bosto_2g->irq->dev = bosto_2g->usbdev;
  if (usb_submit_urb(bosto_2g->irq, GFP_KERNEL))
    return -EIO;
  
  //dev_err(&dev->dev, "%s - Opening Bosto urb.",
  //__func__);
  return 0;
}

static void bosto_2g_close(struct input_dev *dev)
{
  struct bosto_2g *bosto_2g = input_get_drvdata(dev);
  
  dev_err(&dev->dev, "%s - Closing Bosto urb.",
	  __func__);
  usb_kill_urb(bosto_2g->irq);
}

static bool get_features(struct usb_device *dev, struct bosto_2g *bosto_2g)
{
  int i;
  
  for (i = 0; i < ARRAY_SIZE(features_array); i++) {
    if (le16_to_cpu(dev->descriptor.idProduct) == features_array[i].pid) {
      printk (KERN_INFO "Found Bosto Product Type: %s.\n", features_array[i].name);
      printk (KERN_INFO "Found Bosto MAX_X: %i.\n", features_array[i].max_x);
      printk (KERN_INFO "Found Bosto MAX_Y: %i.\n", features_array[i].max_y);
      printk (KERN_INFO "Found Bosto MAX_PRESSURE: %i.\n", features_array[i].max_pressure);
      bosto_2g->features = &features_array[i];
      return true;
    }
  }
  
  return false;
}


static int bosto_2g_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
  struct usb_device *dev = interface_to_usbdev(intf);
  struct usb_endpoint_descriptor *endpoint;
  struct bosto_2g *bosto_2g;
  struct input_dev *input_dev;
  int error;
  int i;
  
  
  printk (KERN_INFO "Bosto_Probe checking Tablet.\n");
  bosto_2g = kzalloc(sizeof(struct bosto_2g), GFP_KERNEL);
  input_dev = input_allocate_device();
  if (!bosto_2g || !input_dev) {
    error = -ENOMEM;
    goto fail1;
  }
  
  if (!get_features(dev, bosto_2g)) {
    error = -ENXIO;
    goto fail1;
  }
  
  bosto_2g->data = usb_alloc_coherent(dev, bosto_2g->features->pkg_len,
				      GFP_KERNEL, &bosto_2g->data_dma);
  if (!bosto_2g->data) {
    error = -ENOMEM;
    goto fail1;
  }
  
  bosto_2g->irq = usb_alloc_urb(0, GFP_KERNEL);
  if (!bosto_2g->irq) {
    error = -ENOMEM;
    goto fail2;
  }
  
  bosto_2g->usbdev = dev;
  bosto_2g->dev = input_dev;
  
  usb_make_path(dev, bosto_2g->phys, sizeof(bosto_2g->phys));
  strlcat(bosto_2g->phys, "/input0", sizeof(bosto_2g->phys));
  
  strlcpy(bosto_2g->name, bosto_2g->features->name, sizeof(bosto_2g->name));
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
  
  //for (i = 0; i < ARRAY_SIZE(hw_btnevents); ++i)
  //  __set_bit(hw_btnevents[i], input_dev->keybit);
  input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_PEN) | BIT_MASK (BTN_TOUCH) | BIT_MASK (BTN_TOOL_RUBBER) | BIT_MASK (BTN_STYLUS);
  for(i=0;i<sizeof(hw_btnevents)/sizeof(hw_btnevents[0]);i++)
	  __set_bit(hw_btnevents[i], input_dev->keybit);
  
  for (i = 0; i < ARRAY_SIZE(hw_mscevents); ++i)
    __set_bit(hw_mscevents[i], input_dev->mscbit);
  
  input_set_abs_params(input_dev, ABS_X,
		       0, bosto_2g->features->max_x, 0, 0);
  input_abs_set_res(input_dev, ABS_X, bosto_2g->features->res_x);	
  input_set_abs_params(input_dev, ABS_Y,
		       0, bosto_2g->features->max_y, 0, 0);
  input_abs_set_res(input_dev, ABS_Y, bosto_2g->features->res_y);
  input_set_abs_params(input_dev, ABS_PRESSURE,
		       0, bosto_2g->features->max_pressure, 0, 0);		 
  
  endpoint = &intf->cur_altsetting->endpoint[0].desc;
  usb_fill_int_urb(bosto_2g->irq, dev,
		   usb_rcvintpipe(dev, endpoint->bEndpointAddress),
		   bosto_2g->data, bosto_2g->features->pkg_len,
		   bosto_2g_irq, bosto_2g, endpoint->bInterval);
  bosto_2g->irq->transfer_dma = bosto_2g->data_dma;
  bosto_2g->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
  
  error = input_register_device(bosto_2g->dev);
  if (error)
    goto fail3;
  
  usb_set_intfdata(intf, bosto_2g);
  
  return 0;
  
 fail3:	usb_free_urb(bosto_2g->irq);
 fail2:	usb_free_coherent(dev, bosto_2g->features->pkg_len,
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
		    bosto_2g->features->pkg_len, bosto_2g->data,
		    bosto_2g->data_dma);
  kfree(bosto_2g);
  usb_set_intfdata(intf, NULL);
}

static const struct usb_device_id bosto_2g_ids[] = {
  { BOSTO_TABLET_DEVICE(USB_VENDOR_ID_HANWANG, USB_PRODUCT_BOSTO22HD, BOSTO_TABLET_INT_CLASS, BOSTO_TABLET_INT_SUB_CLASS, BOSTO_TABLET_INT_PROTOCOL) },
  {}
};



MODULE_DEVICE_TABLE(usb, bosto_2g_ids);

static struct usb_driver bosto_2g_driver = {
  .name		= "bosto_2g",
  .probe		= bosto_2g_probe,
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
