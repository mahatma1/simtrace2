//#define TRACE_LEVEL 6

#include "board.h"
#include "simtrace.h"
#include "ringbuffer.h"
#include "card_emu.h"
#include "iso7816_fidi.h"
#include "utils.h"
#include "osmocom/core/linuxlist.h"
#include "llist_irqsafe.h"
#include "req_ctx.h"
#include "cardemu_prot.h"

#define TRACE_ENTRY()	TRACE_DEBUG("%s entering\r\n", __func__)

static const Pin pins_cardsim[] = PINS_CARDSIM;

/* UART pins */
static const Pin pins_usim1[]	= {PINS_USIM1};
static const Pin pin_usim1_rst	= PIN_USIM1_nRST;
static const Pin pin_usim1_vcc	= PIN_USIM1_VCC;

#ifdef CARDEMU_SECOND_UART
static const Pin pins_usim2[]    = {PINS_USIM2};
static const Pin pin_usim2_rst	= PIN_USIM2_nRST;
static const Pin pin_usim2_vcc	= PIN_USIM2_VCC;
#endif

struct cardem_inst {
	uint32_t num;
	struct card_handle *ch;
	struct llist_head usb_out_queue;
	struct ringbuf rb;
	struct Usart_info usart_info;
	int usb_pending_old;
	uint8_t ep_out;
	uint8_t ep_in;
	uint8_t ep_int;
	const Pin pin_insert;
	uint32_t vcc_uv;
	uint32_t vcc_uv_last;
};

static struct cardem_inst cardem_inst[] = {
	{
		.num = 0,
		.usart_info = 	{
			.base = USART1,
			.id = ID_USART1,
			.state = USART_RCV
		},
		.ep_out = PHONE_DATAOUT,
		.ep_in = PHONE_DATAIN,
		.ep_int = PHONE_INT,
		.pin_insert = PIN_SET_USIM1_PRES,
	},
#ifdef CARDEMU_SECOND_UART
	{
		.num = 1,
		.usart_info = {
			.base = USART0,
			.id = ID_USART0,
			.state = USART_RCV
		},
		.ep_out = CARDEM_USIM2_DATAOUT,
		.ep_in = CARDEM_USIM2_DATAIN,
		.ep_int = CARDEM_USIM2_INT,
		.pin_insert = PIN_SET_USIM2_PRES,
	},
#endif
};

static Usart *get_usart_by_chan(uint8_t uart_chan)
{
	switch (uart_chan) {
	case 0:
		return USART1;
#ifdef CARDEMU_SECOND_UART
	case 1:
		return USART0;
#endif
	}
	return NULL;
}

/***********************************************************************
 * Call-Backs from card_emu.c
 ***********************************************************************/

static void wait_tx_idle(Usart *usart)
{
	int i = 1;

	/* wait until last char has been fully transmitted */
	while ((usart->US_CSR & (US_CSR_TXEMPTY)) == 0) {
		if (!(i%1000000)) {
			TRACE_ERROR("s: %x \r\n", usart->US_CSR);
		}
		i++;
	}
}

void card_emu_uart_wait_tx_idle(uint8_t uart_chan)
{
	Usart *usart = get_usart_by_chan(uart_chan);
	wait_tx_idle(usart);
}

/* call-back from card_emu.c to enable/disable transmit and/or receive */
void card_emu_uart_enable(uint8_t uart_chan, uint8_t rxtx)
{
	Usart *usart = get_usart_by_chan(uart_chan);
	switch (rxtx) {
	case ENABLE_TX:
		USART_DisableIt(usart, ~US_IER_TXRDY);
		/* as irritating as it is, we actually want to keep the
		 * receiver enabled during transmit */
		USART_SetReceiverEnabled(usart, 1);
		usart->US_CR = US_CR_RSTSTA | US_CR_RSTIT | US_CR_RSTNACK;
		USART_EnableIt(usart, US_IER_TXRDY);
		USART_SetTransmitterEnabled(usart, 1);
		break;
	case ENABLE_RX:
		USART_DisableIt(usart, ~US_IER_RXRDY);
		/* as irritating as it is, we actually want to keep the
		 * transmitter enabled during receive */
		USART_SetTransmitterEnabled(usart, 1);
		wait_tx_idle(usart);
		usart->US_CR = US_CR_RSTSTA | US_CR_RSTIT | US_CR_RSTNACK;
		USART_EnableIt(usart, US_IER_RXRDY);
		USART_SetReceiverEnabled(usart, 1);
		break;
	case 0:
	default:
		USART_SetTransmitterEnabled(usart, 0);
		USART_SetReceiverEnabled(usart, 0);
		USART_DisableIt(usart, 0xFFFFFFFF);
		usart->US_CR = US_CR_RSTSTA | US_CR_RSTIT | US_CR_RSTNACK;
		break;
	}
}

/* call-back from card_emu.c to transmit a byte */
int card_emu_uart_tx(uint8_t uart_chan, uint8_t byte)
{
	Usart *usart = get_usart_by_chan(uart_chan);
#if 0
	Usart_info *ui = &usart_info[uart_chan];
	ISO7816_SendChar(byte, ui);
#else
	int i = 1;
	while ((usart->US_CSR & (US_CSR_TXRDY)) == 0) {
		if (!(i%1000000)) {
			TRACE_ERROR("%u: s: %x %02X\r\n",
				uart_chan, usart->US_CSR,
				usart->US_RHR & 0xFF);
			usart->US_CR = US_CR_RSTTX;
			usart->US_CR = US_CR_RSTRX;
		}
		i++;
	}
	usart->US_THR = byte;
	//TRACE_ERROR("Sx%02x\r\n", byte);
#endif
	return 1;
}


/* FIXME: integrate this with actual irq handler */
static void usart_irq_rx(uint8_t inst_num)
{
	Usart *usart = get_usart_by_chan(inst_num);
	struct cardem_inst *ci = &cardem_inst[inst_num];
	uint32_t csr;
	uint8_t byte = 0;

	csr = usart->US_CSR & usart->US_IMR;

	if (csr & US_CSR_RXRDY) {
        	byte = (usart->US_RHR) & 0xFF;
		rbuf_write(&ci->rb, byte);
	}

	if (csr & US_CSR_TXRDY) {
		if (card_emu_tx_byte(ci->ch) == 0)
			USART_DisableIt(usart, US_IER_TXRDY);
	}

	if (csr & (US_CSR_OVRE|US_CSR_FRAME|US_CSR_PARE|
		   US_CSR_TIMEOUT|US_CSR_NACK|(1<<10))) {
		usart->US_CR = US_CR_RSTSTA | US_CR_RSTIT | US_CR_RSTNACK;
		TRACE_ERROR("%u e 0x%x st: 0x%x\n", ci->num, byte, csr);
	}
}

void mode_cardemu_usart0_irq(void)
{
	/* USART0 == Instance 1 == USIM 2 */
	usart_irq_rx(1);
}

void mode_cardemu_usart1_irq(void)
{
	/* USART1 == Instance 0 == USIM 1 */
	usart_irq_rx(0);
}

/* call-back from card_emu.c to change UART baud rate */
int card_emu_uart_update_fidi(uint8_t uart_chan, unsigned int fidi)
{
	int rc;
	Usart *usart = get_usart_by_chan(uart_chan);

	usart->US_CR |= US_CR_RXDIS | US_CR_RSTRX;
	usart->US_FIDI = fidi & 0x3ff;
	usart->US_CR |= US_CR_RXEN | US_CR_STTTO;
	return 0;
}

/***********************************************************************
 * ADC for VCC voltage detection
 ***********************************************************************/

#ifdef DETECT_VCC_BY_ADC

static int adc_triggered = 0;
static int adc_sam3s_reva_errata = 0;

static int card_vcc_adc_init(void)
{
	uint32_t chip_arch = CHIPID->CHIPID_CIDR & CHIPID_CIDR_ARCH_Msk;
	uint32_t chip_ver = CHIPID->CHIPID_CIDR & CHIPID_CIDR_VERSION_Msk;

	PMC_EnablePeripheral(ID_ADC);

	ADC->ADC_CR |= ADC_CR_SWRST;
	if (chip_ver == 0 &&
	    (chip_arch == CHIPID_CIDR_ARCH_SAM3SxA ||
	     chip_arch == CHIPID_CIDR_ARCH_SAM3SxB ||
	     chip_arch == CHIPID_CIDR_ARCH_SAM3SxC)) {
		TRACE_INFO("Enabling Rev.A ADC Errata work-around\r\n");
		adc_sam3s_reva_errata = 1;
	}

	if (adc_sam3s_reva_errata) {
		/* Errata Work-Around to clear EOCx flags */
		volatile uint32_t foo;
		int i;
		for (i = 0; i < 16; i++)
			foo = ADC->ADC_CDR[i];
	}

	/* Initialize ADC for AD7 / AD6, fADC=48/24=2MHz */
	ADC->ADC_MR = ADC_MR_TRGEN_DIS | ADC_MR_LOWRES_BITS_12 |
		      ADC_MR_SLEEP_NORMAL | ADC_MR_FWUP_OFF |
		      ADC_MR_FREERUN_OFF | ADC_MR_PRESCAL(23) |
		      ADC_MR_STARTUP_SUT8 | ADC_MR_SETTLING(3) |
		      ADC_MR_ANACH_NONE | ADC_MR_TRACKTIM(4) |
		      ADC_MR_TRANSFER(1) | ADC_MR_USEQ_NUM_ORDER;
	/* enable AD6 + AD7 channels */
	ADC->ADC_CHER = ADC_CHER_CH7;
	ADC->ADC_IER = ADC_IER_EOC7;
#ifdef CARDEMU_SECOND_UART
	ADC->ADC_CHER |= ADC_CHER_CH6;
	ADC->ADC_IER |= ADC_IER_EOC6;
#endif
	NVIC_EnableIRQ(ADC_IRQn);
	ADC->ADC_CR |= ADC_CR_START;

	return 0;
}

#define UV_PER_LSB	((3300 * 1000) / 4096)
#define VCC_UV_THRESH_1V8	1500000
#define VCC_UV_THRESH_3V	2800000

static void process_vcc_adc(struct cardem_inst *ci)
{
	if (ci->vcc_uv >= VCC_UV_THRESH_3V &&
	    ci->vcc_uv_last < VCC_UV_THRESH_3V) {
		card_emu_io_statechg(ci->ch, CARD_IO_VCC, 1);
		/* FIXME do this for real */
		card_emu_io_statechg(ci->ch, CARD_IO_CLK, 1);
	} else if (ci->vcc_uv < VCC_UV_THRESH_3V &&
		 ci->vcc_uv_last >= VCC_UV_THRESH_3V) {
		/* FIXME do this for real */
		card_emu_io_statechg(ci->ch, CARD_IO_CLK, 0);
		card_emu_io_statechg(ci->ch, CARD_IO_VCC, 0);
	}
	ci->vcc_uv_last = ci->vcc_uv;
}

static uint32_t adc2uv(uint16_t adc)
{
	uint32_t uv = (uint32_t) adc * UV_PER_LSB;
	return uv;
}

void ADC_IrqHandler(void)
{
#ifdef CARDEMU_SECOND_UART
	if (ADC->ADC_ISR & ADC_ISR_EOC6) {
		uint16_t val = ADC->ADC_CDR[6] & 0xFFF;
		cardem_inst[1].vcc_uv = adc2uv(val);
		process_vcc_adc(&cardem_inst[1]);
		if (adc_sam3s_reva_errata) {
			/* Errata: START doesn't start a conversion
			 * sequence, but only a single conversion */
			ADC->ADC_CR |= ADC_CR_START;
		}
	}
#endif

	if (ADC->ADC_ISR & ADC_ISR_EOC7) {
		uint16_t val = ADC->ADC_CDR[7] & 0xFFF;
		cardem_inst[0].vcc_uv = adc2uv(val);
		process_vcc_adc(&cardem_inst[0]);
		ADC->ADC_CR |= ADC_CR_START;
	}
}
#endif /* DETECT_VCC_BY_ADC */

/***********************************************************************
 * Core USB  / mainloop integration
 ***********************************************************************/

static void usim1_rst_irqhandler(const Pin *pPin)
{
	int active = PIO_Get(&pin_usim1_rst) ? 0 : 1;
	card_emu_io_statechg(cardem_inst[0].ch, CARD_IO_RST, active);
}

#ifndef DETECT_VCC_BY_ADC
static void usim1_vcc_irqhandler(const Pin *pPin)
{
	int active = PIO_Get(&pin_usim1_vcc) ? 1 : 0;
	card_emu_io_statechg(cardem_inst[0].ch, CARD_IO_VCC, active);
	/* FIXME do this for real */
	card_emu_io_statechg(cardem_inst[0].ch, CARD_IO_CLK, active);
}
#endif /* !DETECT_VCC_BY_ADC */

#ifdef CARDEMU_SECOND_UART
static void usim2_rst_irqhandler(const Pin *pPin)
{
	int active = PIO_Get(&pin_usim2_rst) ? 0 : 1;
	card_emu_io_statechg(cardem_inst[1].ch, CARD_IO_RST, active);
}

#ifndef DETECT_VCC_BY_ADC
static void usim2_vcc_irqhandler(const Pin *pPin)
{
	int active = PIO_Get(&pin_usim2_vcc) ? 1 : 0;
	card_emu_io_statechg(cardem_inst[1].ch, CARD_IO_VCC, active);
	/* FIXME do this for real */
	card_emu_io_statechg(cardem_inst[1].ch, CARD_IO_CLK, active);
}
#endif /* !DETECT_VCC_BY_ADC */
#endif /* CARDEMU_SECOND_UART */

/* executed once at system boot for each config */
void mode_cardemu_configure(void)
{
	TRACE_ENTRY();
}

/* called if config is activated */
void mode_cardemu_init(void)
{
	int i;

	TRACE_ENTRY();

	PIO_Configure(pins_cardsim, PIO_LISTSIZE(pins_cardsim));
#ifdef DETECT_VCC_BY_ADC
	card_vcc_adc_init();
#endif /* DETECT_VCC_BY_ADC */

	INIT_LLIST_HEAD(&cardem_inst[0].usb_out_queue);
	rbuf_reset(&cardem_inst[0].rb);
	PIO_Configure(pins_usim1, PIO_LISTSIZE(pins_usim1));
	ISO7816_Init(&cardem_inst[0].usart_info, CLK_SLAVE);
	NVIC_EnableIRQ(USART1_IRQn);
	PIO_ConfigureIt(&pin_usim1_rst, usim1_rst_irqhandler);
	PIO_EnableIt(&pin_usim1_rst);
#ifndef DETECT_VCC_BY_ADC
	PIO_ConfigureIt(&pin_usim1_vcc, usim1_vcc_irqhandler);
	PIO_EnableIt(&pin_usim1_vcc);
#endif /* DETECT_VCC_BY_ADC */
	cardem_inst[0].ch = card_emu_init(0, 2, 0);

#ifdef CARDEMU_SECOND_UART
	INIT_LLIST_HEAD(&cardem_inst[1].usb_out_queue);
	rbuf_reset(&cardem_inst[1].rb);
	PIO_Configure(pins_usim2, PIO_LISTSIZE(pins_usim2));
	ISO7816_Init(&cardem_inst[1].usart_info, CLK_SLAVE);
	NVIC_EnableIRQ(USART0_IRQn);
	PIO_ConfigureIt(&pin_usim2_rst, usim2_rst_irqhandler);
	PIO_EnableIt(&pin_usim2_rst);
#ifndef DETECT_VCC_BY_ADC
	PIO_ConfigureIt(&pin_usim2_vcc, usim2_vcc_irqhandler);
	PIO_EnableIt(&pin_usim2_vcc);
#endif /* DETECT_VCC_BY_ADC */
	cardem_inst[1].ch = card_emu_init(1, 0, 1);
#endif /* CARDEMU_SECOND_UART */
}

/* called if config is deactivated */
void mode_cardemu_exit(void)
{
	TRACE_ENTRY();

	/* FIXME: stop tc_fdt */
	/* FIXME: release all rctx, unlink them from any queue */

	PIO_DisableIt(&pin_usim1_rst);
	PIO_DisableIt(&pin_usim1_vcc);

	NVIC_DisableIRQ(USART1_IRQn);
	USART_SetTransmitterEnabled(USART1, 0);
	USART_SetReceiverEnabled(USART1, 0);

#ifdef CARDEMU_SECOND_UART
	PIO_DisableIt(&pin_usim2_rst);
	PIO_DisableIt(&pin_usim2_vcc);

	NVIC_DisableIRQ(USART0_IRQn);
	USART_SetTransmitterEnabled(USART0, 0);
	USART_SetReceiverEnabled(USART0, 0);
#endif
}

/* handle a single USB command as received from the USB host */
static void dispatch_usb_command(struct req_ctx *rctx, struct cardem_inst *ci)
{
	struct cardemu_usb_msg_hdr *hdr;
	struct cardemu_usb_msg_set_atr *atr;
	struct cardemu_usb_msg_cardinsert *cardins;
	struct llist_head *queue;

	hdr = (struct cardemu_usb_msg_hdr *) rctx->data;
	switch (hdr->msg_type) {
	case CEMU_USB_MSGT_DT_TX_DATA:
		queue = card_emu_get_uart_tx_queue(ci->ch);
		req_ctx_set_state(rctx, RCTX_S_UART_TX_PENDING);
		llist_add_tail(&rctx->list, queue);
		card_emu_have_new_uart_tx(ci->ch);
		break;
	case CEMU_USB_MSGT_DT_SET_ATR:
		atr = (struct cardemu_usb_msg_set_atr *) hdr;
		card_emu_set_atr(ci->ch, atr->atr, atr->atr_len);
		req_ctx_put(rctx);
		break;
	case CEMU_USB_MSGT_DT_CARDINSERT:
		cardins = (struct cardemu_usb_msg_cardinsert *) hdr;
		TRACE_INFO("%u: set card_insert to %s\r\n", ci->num,
			   cardins->card_insert ? "INSERTED" : "REMOVED");
		if (cardins->card_insert)
			PIO_Set(&ci->pin_insert);
		else
			PIO_Clear(&ci->pin_insert);
		req_ctx_put(rctx);
		break;
	case CEMU_USB_MSGT_DT_GET_STATUS:
		card_emu_report_status(ci->ch);
		break;
	case CEMU_USB_MSGT_DT_GET_STATS:
	default:
		/* FIXME */
		req_ctx_put(rctx);
		break;
	}
}

static void dispatch_received_rctx(struct req_ctx *rctx, struct cardem_inst *ci)
{
	struct req_ctx *segm;
	struct cardemu_usb_msg_hdr *mh;
	int i = 0;

	/* check if we have multiple concatenated commands in
	 * one message.  USB endpoints are streams that don't
	 * preserve the message boundaries */
	mh = (struct cardemu_usb_msg_hdr *) rctx->data;
	if (mh->msg_len == rctx->tot_len) {
		/* fast path: only one message in buffer */
		dispatch_usb_command(rctx, ci);
		return;
	}

	/* slow path: iterate over list of messages, allocating one new
	 * reqe_ctx per segment */
	for (mh = (struct cardemu_usb_msg_hdr *) rctx->data;
	     (uint8_t *)mh < rctx->data + rctx->tot_len;
	     mh = (struct cardemu_usb_msg_hdr * ) ((uint8_t *)mh + mh->msg_len)) {
		segm = req_ctx_find_get(0, RCTX_S_FREE, RCTX_S_MAIN_PROCESSING);
		if (!segm) {
			TRACE_ERROR("%u: ENOMEM during rctx segmentation\r\n",
				    ci->num);
			break;
		}
		segm->idx = 0;
		segm->tot_len = mh->msg_len;
		memcpy(segm->data, mh, segm->tot_len);
		dispatch_usb_command(segm, ci);
		i++;
	}

	/* release the master req_ctx, as all segments have been
	 * processed now */
	req_ctx_put(rctx);
}

/* iterate over the queue of incoming USB commands and dispatch/execute
 * them */
static void process_any_usb_commands(struct llist_head *main_q,
				     struct cardem_inst *ci)
{
	struct llist_head *lh;
	struct req_ctx *rctx;
	int i;

	/* limit the number of iterations to 10, to ensure we don't get
	 * stuck here without returning to main loop processing */
	for (i = 0; i < 10; i++) {
		/* de-queue the list head in an irq-safe way */
		lh = llist_head_dequeue_irqsafe(main_q);
		if (!lh)
			break;
		rctx = llist_entry(lh, struct req_ctx, list);
		dispatch_received_rctx(rctx, ci);
	}
}

/* main loop function, called repeatedly */
void mode_cardemu_run(void)
{
	struct llist_head *queue;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cardem_inst); i++) {
		struct cardem_inst *ci = &cardem_inst[i];

		/* drain the ring buffer from UART into card_emu */
		while (1) {
			__disable_irq();
			if (rbuf_is_empty(&ci->rb)) {
				__enable_irq();
				break;
			}
			uint8_t byte = rbuf_read(&ci->rb);
			__enable_irq();
			card_emu_process_rx_byte(ci->ch, byte);
			//TRACE_ERROR("%uRx%02x\r\n", i, byte);
		}

		queue = card_emu_get_usb_tx_queue(ci->ch);
		int usb_pending = llist_count(queue);
		if (usb_pending != ci->usb_pending_old) {
			TRACE_DEBUG("%u usb_pending=%d\r\n",
				    i, usb_pending);
			ci->usb_pending_old = usb_pending;
		}
		usb_refill_to_host(queue, ci->ep_in);

		/* ensure we can handle incoming USB messages from the
		 * host */
		queue = &ci->usb_out_queue;
		usb_refill_from_host(queue, ci->ep_out);
		process_any_usb_commands(queue, ci);
	}
}