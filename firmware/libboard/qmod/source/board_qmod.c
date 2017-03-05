/* Quad-modem speciic application code */
/* (C) 2016-2016 by Harald Welte <laforge@gnumonks.org> */

#include "board.h"
#include "simtrace.h"
#include "utils.h"
#include "req_ctx.h"
#include "wwan_led.h"
#include "wwan_perst.h"
#include "boardver_adc.h"
#include "osmocom/core/timer.h"

static const Pin pin_hubpwr_override = PIN_PRTPWR_OVERRIDE;
static const Pin pin_hub_rst = {PIO_PA13, PIOA, ID_PIOA, PIO_OUTPUT_0, PIO_DEFAULT};
static const Pin pin_1234_detect = {PIO_PA14, PIOA, ID_PIOA, PIO_INPUT, PIO_PULLUP};
static const Pin pin_peer_rst = {PIO_PA0, PIOA, ID_PIOA, PIO_OUTPUT_0, PIO_DEFAULT};
static const Pin pin_peer_erase = {PIO_PA11, PIOA, ID_PIOA, PIO_OUTPUT_0, PIO_DEFAULT};


static int qmod_sam3_is_12(void)
{
	if (PIO_Get(&pin_1234_detect) == 0)
		return 1;
	else
		return 0;
}

const unsigned char __eeprom_bin[256] = {
	USB_VENDOR_OPENMOKO & 0xff,
	USB_VENDOR_OPENMOKO >> 8,
	USB_PRODUCT_QMOD_HUB & 0xff,
	USB_PRODUCT_QMOD_HUB >> 8,
	                        0x00, 0x00, 0x9b, 0x20, 0x01, 0x00, 0x00, 0x00, 0x32, 0x32, 0x32, 0x32, /* 0x00 - 0x0f */
	0x32, 0x04, 0x09, 0x18, 0x0d, 0x00, 0x73, 0x00, 0x79, 0x00, 0x73, 0x00, 0x6d, 0x00, 0x6f, 0x00, /* 0x10 - 0x1f */
	0x63, 0x00, 0x6f, 0x00, 0x6d, 0x00, 0x20, 0x00, 0x2d, 0x00, 0x20, 0x00, 0x73, 0x00, 0x2e, 0x00, /* 0x20 - 0x2f */
	0x66, 0x00, 0x2e, 0x00, 0x6d, 0x00, 0x2e, 0x00, 0x63, 0x00, 0x2e, 0x00, 0x20, 0x00, 0x47, 0x00, /* 0x30 - 0x3f */
	0x6d, 0x00, 0x62, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x40 - 0x4f */
	0x00, 0x00, 0x00, 0x00, 0x71, 0x00, 0x75, 0x00, 0x61, 0x00, 0x64, 0x00, 0x20, 0x00, 0x6d, 0x00, /* 0x50 - 0x5f */
	0x6f, 0x00, 0x64, 0x00, 0x65, 0x00, 0x6d, 0x00, 0x20, 0x00, 0x76, 0x00, 0x32, 0x00, 0x00, 0x00, /* 0x60 - 0x6f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x70 - 0x7f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x80 - 0x8f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x90 - 0x9f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xa0 - 0xaf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xb0 - 0xbf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xc0 - 0xcf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xd0 - 0xdf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xe0 - 0xef */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0x56, 0x23, 0x71, 0x04, 0x00, /* 0xf0 - 0xff */
};

#include "i2c.h"
static int write_hub_eeprom(void)
{
	const unsigned int __eeprom_bin_len = 256;

	int i;

	/* wait */
	volatile int v;
	/* 440ns per cycle here */
	for (i = 0; i < 1000000; i++) {
		v = 0;
	}

	TRACE_INFO("Writing EEPROM...\n\r");
	/* write the EEPROM once */
	for (i = 0; i < 256; i++) {
		int rc = eeprom_write_byte(0x50, i, __eeprom_bin[i]);
		/* if the result was negative, repeat that write */
		if (rc < 0)
			i--;
	}

	/* then pursue re-reading it again and again */
	TRACE_INFO("Verifying EEPROM...\n\r");
	for (i = 0; i < 256; i++) {
		int byte = eeprom_read_byte(0x50, i);
		TRACE_INFO("0x%02x: %02x\n\r", i, byte);
		if (byte != __eeprom_bin[i])
			TRACE_ERROR("Byte %u is wrong, expected 0x%02x, found 0x%02x\n\r",
					i, __eeprom_bin[i], byte);
	}

	/* FIXME: Release PIN_PRTPWR_OVERRIDE after we know the hub is
	 * again powering us up */

	return 0;
}

/* returns '1' in case we should break any endless loop */
void board_exec_dbg_cmd(int ch)
{
	uint32_t addr, val;

	switch (ch) {
	case '?':
		printf("\t?\thelp\n\r");
		printf("\tE\tprogram EEPROM\n\r");
		printf("\tR\treset SAM3\n\r");
		printf("\tO\tEnable PRTPWR_OVERRIDE\n\r");
		printf("\to\tDisable PRTPWR_OVERRIDE\n\r");
		printf("\tH\tRelease HUB RESET (high)\n\r");
		printf("\th\tAssert HUB RESET (low)\n\r");
		printf("\tw\tWrite single byte in EEPROM\n\r");
		printf("\tr\tRead single byte from EEPROM\n\r");
		printf("\tX\tRelease peer SAM3 from reset\n\r");
		printf("\tx\tAssert peer SAM3 reset\n\r");
		printf("\tY\tRelease peer SAM3 ERASE signal\n\r");
		printf("\ty\tAssert peer SAM3 ERASE signal\n\r");
		printf("\tU\tProceed to USB Initialization\n\r");
		printf("\t1\tGenerate 1ms reset pulse on WWAN1\n\r");
		printf("\t2\tGenerate 1ms reset pulse on WWAN2\n\r");
		break;
	case 'E':
		write_hub_eeprom();
		break;
	case 'R':
		printf("Asking NVIC to reset us\n\r");
		USBD_Disconnect();
		NVIC_SystemReset();
		break;
	case 'O':
		printf("Setting PRTPWR_OVERRIDE\n\r");
		PIO_Set(&pin_hubpwr_override);
		break;
	case 'o':
		printf("Clearing PRTPWR_OVERRIDE\n\r");
		PIO_Clear(&pin_hubpwr_override);
		break;
	case 'H':
		printf("Clearing _HUB_RESET -> HUB_RESET high (inactive)\n\r");
		PIO_Clear(&pin_hub_rst);
		break;
	case 'h':
		/* high level drives transistor -> HUB_RESET low */
		printf("Asserting _HUB_RESET -> HUB_RESET low (active)\n\r");
		PIO_Set(&pin_hub_rst);
		break;
	case 'w':
		if (PIO_GetOutputDataStatus(&pin_hub_rst) == 0)
			printf("WARNING: attempting EEPROM access while HUB not in reset\n\r");
		printf("Please enter EEPROM offset:\n\r");
		UART_GetIntegerMinMax(&addr, 0, 255);
		printf("Please enter EEPROM value:\n\r");
		UART_GetIntegerMinMax(&val, 0, 255);
		printf("Writing value 0x%02x to EEPROM offset 0x%02x\n\r", val, addr);
		eeprom_write_byte(0x50, addr, val);
		break;
	case 'r':
		printf("Please enter EEPROM offset:\n\r");
		UART_GetIntegerMinMax(&addr, 0, 255);
		printf("EEPROM[0x%02x] = 0x%02x\n\r", addr, eeprom_read_byte(0x50, addr));
		break;
	case 'X':
		printf("Clearing _SIMTRACExx_RST -> SIMTRACExx_RST high (inactive)\n\r");
		PIO_Clear(&pin_peer_rst);
		break;
	case 'x':
		printf("Setting _SIMTRACExx_RST -> SIMTRACExx_RST low (active)\n\r");
		PIO_Set(&pin_peer_rst);
		break;
	case 'Y':
		printf("Clearing SIMTRACExx_ERASE (inactive)\n\r");
		PIO_Clear(&pin_peer_erase);
		break;
	case 'y':
		printf("Seetting SIMTRACExx_ERASE (active)\n\r");
		PIO_Set(&pin_peer_erase);
		break;
	case '1':
		printf("Resetting Modem 1 (of this SAM3)\n\r");
		wwan_perst_do_reset(1);
		break;
	case '2':
		printf("Resetting Modem 2 (of this SAM3)\n\r");
		wwan_perst_do_reset(2);
		break;
	default:
		printf("Unknown command '%c'\n\r", ch);
		break;
	}
}

void board_main_top(void)
{
	wwan_led_init();
	wwan_perst_init();

	/* set PIN_PRTPWR_OVERRIDE to output-low to avoid the internal
	 * pull-up on the input to keep SIMTRACE12 alive */
	PIO_Configure(&pin_hubpwr_override, 1);
	PIO_Configure(&pin_hub_rst, 1);
	PIO_Configure(&pin_1234_detect, 1);
	PIO_Configure(&pin_peer_rst, 1);
	PIO_Configure(&pin_peer_erase, 1);
	i2c_pin_init();

	if (qmod_sam3_is_12()) {
		TRACE_INFO("Detected Quad-Modem ST12\n\r");
	} else {
		TRACE_INFO("Detected Quad-Modem ST34\n\r");
	}

	/* Obtain the circuit board version (currently just prints voltage */
	get_board_version_adc();
}
