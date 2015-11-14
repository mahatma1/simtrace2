#pragma once

#define RCTX_SIZE_LARGE	960
#define RCTX_SIZE_SMALL	320
#define MAX_HDRSIZE	sizeof(struct openpcd_hdr)

#include <stdint.h>

#define __ramfunc

enum req_ctx_state {
	RCTX_S_FREE,
	/* USB -> UART */
	RCTX_S_USB_RX_BUSY,
	RCTX_S_MAIN_PROCESSING,
	RCTX_S_UART_TX_PENDING,
	RCTX_S_UART_TX_BUSY,
	/* UART -> USB */
	RCTX_S_UART_RX_BUSY,
	RCTX_S_USB_TX_PENDING,
	RCTX_S_USB_TX_BUSY,
	/* number of states */
	RCTX_STATE_COUNT
};

struct req_ctx {
	/* enum req_ctx_state */
	volatile uint32_t state;
#ifdef REQ_CTX_LISTS
	/* pointers for queues */
	volatile struct req_ctx *prev, *next;
#endif
	/* size of th 'data' buffer */
	uint16_t size;
	/* total number of used bytes in buffer */
	uint16_t tot_len;
	/* index into the buffer, user specific */
	uint16_t idx;
	/* actual data buffer */
	uint8_t *data;
};

extern struct req_ctx __ramfunc *req_ctx_find_get(int large, uint32_t old_state, uint32_t new_state);
extern struct req_ctx *req_ctx_find_busy(void);
extern void req_ctx_set_state(struct req_ctx *ctx, uint32_t new_state);
extern void req_ctx_put(struct req_ctx *ctx);
extern uint8_t req_ctx_num(struct req_ctx *ctx);
unsigned int req_ctx_count(uint32_t state);