
#include <usb/include/USBDescriptors.h>
#include <usb/device/dfu/dfu.h>


/* String 1 "SimTrace DFU Interface - Application Partition" */
const struct USBStringDescriptor USBDFU_string1 = {
	.hdr = {
		.bLength = sizeof(USBGenericDescriptor) + 46 * sizeof(unsigned short),
		.bDescriptorType = USBGenericDescriptor_STRING,
	},
	.wData = {	0x0053, 0x0069, 0x006d, 0x0054, 0x0072, 0x0061,
			0x0063, 0x0065, 0x0020, 0x0044, 0x0046, 0x0055,
			0x0020, 0x0049, 0x006e, 0x0074, 0x0065, 0x0072,
			0x0066, 0x0061, 0x0063, 0x0065, 0x0020, 0x002d,
			0x0020, 0x0041, 0x0070, 0x0070, 0x006c, 0x0069,
			0x0063, 0x0061, 0x0074, 0x0069, 0x006f, 0x006e,
			0x0020, 0x0050, 0x0061, 0x0072, 0x0074, 0x0069,
			0x0074, 0x0069, 0x006f, 0x006e, },
};

/* String 2 "SimTrace DFU Interface - Bootloader Partition" */
const struct USBStringDescriptor USBDFU_string2 = {
	.hdr = {
		.bLength = sizeof(USBGenericDescriptor) + 45 * sizeof(unsigned short),
		.bDescriptorType = USBGenericDescriptor_STRING,
	},
	.wData = {	0x0053, 0x0069, 0x006d, 0x0054, 0x0072, 0x0061,
			0x0063, 0x0065, 0x0020, 0x0044, 0x0046, 0x0055,
			0x0020, 0x0049, 0x006e, 0x0074, 0x0065, 0x0072,
			0x0066, 0x0061, 0x0063, 0x0065, 0x0020, 0x002d,
			0x0020, 0x0042, 0x006f, 0x006f, 0x0074, 0x006c,
			0x006f, 0x0061, 0x0064, 0x0065, 0x0072, 0x0020,
			0x0050, 0x0061, 0x0072, 0x0074, 0x0069, 0x0074,
			0x0069, 0x006f, 0x006e, },
};

/* String 3 "SimTrace DFU Interface - RAM" */
const struct USBStringDescriptor USBDFU_string3 = {
	.hdr = {
		.bLength = sizeof(USBGenericDescriptor) + 28 * sizeof(unsigned short),
		.bDescriptorType = USBGenericDescriptor_STRING,
	},
	.wData = {	0x0053, 0x0069, 0x006d, 0x0054, 0x0072, 0x0061,
			0x0063, 0x0065, 0x0020, 0x0044, 0x0046, 0x0055,
			0x0020, 0x0049, 0x006e, 0x0074, 0x0065, 0x0072,
			0x0066, 0x0061, 0x0063, 0x0065, 0x0020, 0x002d,
			0x0020, 0x0052, 0x0041, 0x004d, },
};
