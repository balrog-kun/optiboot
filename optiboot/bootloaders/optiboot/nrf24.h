/*
 * nRF24L01+ only (note the plus).
 *
 * Licensed under AGPLv3.
 */
#include "nRF24L01.h"

static inline void nrf24_csn(uint8_t level) {
	if (level)
		CSN_PORT |= CSN_PIN;
	else
		CSN_PORT &= ~CSN_PIN;
}

static void delay8(uint16_t count) {
	while (count --)
		__asm__ __volatile__ (
			"\tnop\n"
			"\tnop\n"
			"\tnop\n"
			"\tnop\n"
			"\tnop\n"
			"\tnop\n"
			"\tnop\n"
			"\twdr\n"
		);
}
#ifndef TIMER
#define my_delay(msec) delay8((int) (F_CPU / 8000L * (msec)))
#endif

static inline void nrf24_ce(uint8_t level) {
	/*
	 * Make sure the minimum time period has passed since the previous
	 * CE edge for the new edge to be detected.  The spec doesn't say
	 * what's the actual CE sampling frequency, but the minimum period
	 * for a rising edge on Tx start to be detected is specified to be
	 * 10us.  However this doesn't seem to work in our configuration, a
	 * minimum of ~150-200us is required so we use 200us for a rising
	 * edge and 10us for a falling edge which seems to be enough.
	 *
	 * Falling edges are important because there's no direct transition
	 * between the Tx and Rx states in the nRF24L01+ state diagram, the
	 * Standby-I state is always necessary.  If we attempt to go from
	 * Tx (or Rx) to Standby-I and then immediately go to Rx (or Tx)
	 * the low CE period could be too short.
	 */
#ifdef TIMER
	static uint32_t prev_ce_edge;

	if (level)
		while (timer_read() - prev_ce_edge <= F_CPU / 100000);
	else
		while (timer_read() - prev_ce_edge <= F_CPU / 5000);
#else
	/* This should take at least 10us (rising) or 200us (falling) */
	if (level)
		my_delay(0.01);
	else
		my_delay(0.2);
#endif

	if (level)
		CE_PORT |= CE_PIN;
	else
		CE_PORT &= ~CE_PIN;

#ifdef TIMER
	prev_ce_edge = timer_read();
#endif
}

static uint8_t nrf24_read_reg(uint8_t addr) {
	uint8_t ret;

	nrf24_csn(0);

	spi_transfer(addr | R_REGISTER);
	ret = spi_transfer(0);

	nrf24_csn(1);

	return ret;
}

static void nrf24_write_reg(uint8_t addr, uint8_t value) {
	nrf24_csn(0);

	spi_transfer(addr | W_REGISTER);
	spi_transfer(value);

	nrf24_csn(1);
}

static uint8_t nrf24_read_status(void) {
	uint8_t ret;

	nrf24_csn(0);

	ret = spi_transfer(NOP);

	nrf24_csn(1);

	return ret;
}

static void nrf24_write_addr_reg(uint8_t addr, uint8_t value[3]) {
	nrf24_csn(0);

	spi_transfer(addr | W_REGISTER);
	spi_transfer(value[0]);
	spi_transfer(value[1]);
	spi_transfer(value[2]);

	nrf24_csn(1);
}

static uint8_t nrf24_tx_flush(void) {
	uint8_t ret;

	nrf24_csn(0);

	ret = spi_transfer(FLUSH_TX);

	nrf24_csn(1);

	return ret;
}

static void nrf24_delay(void) {
	my_delay(5);
}

/* Enable 16-bit CRC */
#define CONFIG_VAL ((1 << MASK_RX_DR) | (1 << MASK_TX_DS) | \
		(1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC))

static int nrf24_init(void) {
	/* CE and CSN are outputs */
	CE_DDR |= CE_PIN;
	CSN_DDR |= CSN_PIN;

	nrf24_ce(0);
	nrf24_csn(1);
	nrf24_delay();

	/* 2ms interval, 15 retries (16 total) */
	nrf24_write_reg(SETUP_RETR, 0x7f);
	if (nrf24_read_reg(SETUP_RETR) != 0x7f)
		return 1; /* There may be no nRF24 connected */

	/* Maximum Tx power, 250kbps data rate */
	nrf24_write_reg(RF_SETUP, (1 << RF_PWR_LOW) | (1 << RF_PWR_HIGH) |
			(1 << RF_DR_LOW));
	/* Dynamic payload length for TX & RX (pipes 0 and 1) */
	nrf24_write_reg(DYNPD, 0x03);
	nrf24_write_reg(FEATURE, 1 << EN_DPL);
	/* Reset status bits */
	nrf24_write_reg(STATUS, (1 << RX_DR) | (1 << TX_DS) | (1 << MAX_RT));
	/* Set some RF channel number */
	nrf24_write_reg(RF_CH, 42);
	/* 3-byte addresses */
	nrf24_write_reg(SETUP_AW, 0x01);
	/* Enable ACKing on both pipe 0 & 1 for TX & RX ACK support */
	nrf24_write_reg(EN_AA, 0x03);

	return 0;
}

static void nrf24_set_rx_addr(uint8_t addr[3]) {
	nrf24_write_addr_reg(RX_ADDR_P1, addr);
}

static void nrf24_set_tx_addr(uint8_t addr[3]) {
	nrf24_write_addr_reg(TX_ADDR, addr);
	/* The pipe 0 address is the address we listen on for ACKs */
	nrf24_write_addr_reg(RX_ADDR_P0, addr);
}

static uint8_t nrf24_in_rx = 0;

static void nrf24_rx_mode(void) {
	if (nrf24_in_rx)
		return;

	/* Rx mode */
	nrf24_write_reg(CONFIG, CONFIG_VAL | (1 << PWR_UP) | (1 << PRIM_RX));
	/* Only use data pipe 1 for receiving, pipe 0 is for TX ACKs */
	nrf24_write_reg(EN_RXADDR, 0x02);

	nrf24_ce(1);

	nrf24_in_rx = 1;
}

/*
 * This switches out of Rx mode and leaves the chip in Standby if desired.
 * Otherwise the chip is powered off.  In Standby a new operation will
 * start faster but more current is consumed while waiting.
 */
static void nrf24_idle_mode(uint8_t standby) {
	if (nrf24_in_rx) {
		nrf24_ce(0);

		if (!standby)
			nrf24_write_reg(CONFIG, CONFIG_VAL);
	} else {
		if (standby)
			nrf24_write_reg(CONFIG, CONFIG_VAL | (1 << PWR_UP));
		else
			nrf24_write_reg(CONFIG, CONFIG_VAL);
	}

	nrf24_in_rx = 0;
}

static uint8_t nrf24_rx_new_data(void) {
	return (nrf24_read_status() >> RX_DR) & 1;
}

static uint8_t nrf24_rx_fifo_data(void) {
	return !(nrf24_read_reg(FIFO_STATUS) & (1 << RX_EMPTY));
}

static uint8_t nrf24_rx_data_avail(void) {
	uint8_t ret;

	nrf24_csn(0);

	spi_transfer(R_RX_PL_WID);
	ret = spi_transfer(0);

	nrf24_csn(1);

	return ret;
}

static void nrf24_rx_read(uint8_t *buf, uint8_t *pkt_len) {
	uint8_t len;

	nrf24_write_reg(STATUS, 1 << RX_DR);

	len = nrf24_rx_data_avail();
	*pkt_len = len;

	nrf24_csn(0);

	spi_transfer(R_RX_PAYLOAD);
	while (len --)
		*buf ++ = spi_transfer(0);

	nrf24_csn(1);
}

static void nrf24_tx(uint8_t *buf, uint8_t len) {
	/*
	 * The user may have put the chip out of Rx mode to perform a
	 * few Tx operations in a row, or they may have left the chip
	 * in Rx which we'll switch back on when this Tx is done.
	 */
	if (nrf24_in_rx) {
		nrf24_idle_mode(1);

		nrf24_in_rx = 1;
	}

	/* Tx mode */
	nrf24_write_reg(CONFIG, CONFIG_VAL | (1 << PWR_UP));
	/* Use pipe 0 for receiving ACK packets */
	nrf24_write_reg(EN_RXADDR, 0x01);

	/*
	 * The TX_FULL bit is automatically reset on a successful Tx, but
	 * the FIFO is apparently not actually cleaned so bad things happen
	 * if we don't flush it manually even though the datasheet says
	 * a W_TX_PAYLOAD resets the FIFO contents same as a FLUSH_TX.
	 * This may be connected with the fact of using automatic ACKing.
	 * But if don't we flush the FIFO here it looks like each payload
	 * gets retransmitted about 3 times (sometimes 2, sometimes 4)
	 * instead of the chip picking up what we've written.  After that
	 * it picks up whatever the new payload is and again stops accepting
	 * new payloads for another while.
	 */
	nrf24_tx_flush();

	nrf24_csn(0);

	spi_transfer(W_TX_PAYLOAD);
	while (len --)
		spi_transfer(*buf ++);

	nrf24_csn(1);

	/*
	 * Set CE high for at least 10us - that's 160 cycles at 16MHz.
	 * But we can also leave it that way until tx_result_wait().
	 */
	nrf24_ce(1);
}

static int nrf24_tx_result_wait(void) {
	uint8_t status;
	uint16_t count = 10000; /* ~100ms timeout */

	status = nrf24_read_status();

	/* Reset CE early so that a new Tx or Rx op can start sooner. */
	nrf24_ce(0);

	while ((!(status & (1 << TX_DS)) || (status & (1 << TX_FULL))) &&
			!(status & (1 << MAX_RT)) && --count) {
		delay8((int) (F_CPU / 8000L * 0.01));
		status = nrf24_read_status();
	}

	/* Reset status bits */
	nrf24_write_reg(STATUS, (1 << MAX_RT) | (1 << TX_DS));

	if (nrf24_in_rx) {
		nrf24_in_rx = 0;

		nrf24_rx_mode();
	}

	return (status & (1 << TX_DS)) ? 0 : -1;
}
