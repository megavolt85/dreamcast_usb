#include <avr/io.h>
#include <util/delay.h>
#include "usbdrv.h"

//
//
// PORTC0 : Pin 1
// PORTC1 : Pin 5
//

void maple_init(void)
{
}

#define MAPLE_BUF_SIZE	640
static unsigned char maplebuf[MAPLE_BUF_SIZE];
static unsigned char buf_used;
static unsigned char buf_phase;

#define PIN_1	0x01
#define PIN_5	0x02
static void buf_reset(void)
{
	buf_used = 0;
	buf_phase = 0;
}

static void buf_addBit(char value)
{
	if (buf_phase & 0x01) {
		maplebuf[buf_used] = PIN_5;
		if (value) {
			maplebuf[buf_used] |= PIN_1; // prepare data
		}
		buf_used++;
	}
	else {
		maplebuf[buf_used] = PIN_1;
		if (value) {
			maplebuf[buf_used] |= PIN_5; // prepare data
		}
		buf_used++;
	}
	buf_phase ^= 1;
}

static int maplebus_decode(unsigned char *data, unsigned int maxlen)
{
	unsigned char dst_b;
	unsigned int dst_pos;
	unsigned char last;
	unsigned char last_fell;
	int i;

	// Look for the initial phase 1 (Pin 1 high, Pin 5 low). This
	// is to skip what we got of the sync/start of frame sequence.
	// 
	for (i=0; i<MAPLE_BUF_SIZE; i++) {
		if ((maplebuf[i]&0x03) == 0x01)
			break;
	}
	if (i==MAPLE_BUF_SIZE) {
		return -1; // timeout
	}

	dst_pos = 0;
	data[0] = 0;
	dst_b = 0x80;
	last = maplebuf[i] & 0x03;
	last_fell = 0;
	for (; i<MAPLE_BUF_SIZE; i++) {
		unsigned char fell;
		unsigned char cur = maplebuf[i];

		if (cur == last) {
			continue; // no change
		}

		fell = last & (cur ^ last);

		if (!fell) {
			// pin(s) changed, but none fell.
			last = cur;
			continue;
		}

		if (fell == last_fell) {
			// two identical consecutive phases marks the end of the packet.
				PORTB |= 0x10;
				PORTB &= ~0x10;
			break;
		}

		// when any of the two pins fall, the
		// other pin is the data.
		if (fell) {
			if (fell == 0x03) {
				// two pins at the same time!
				PORTB |= 0x10;
				PORTB &= ~0x10;
			}

			if (cur) {
				data[dst_pos] |= dst_b;
			}
			else {
			}
		}		
		
		dst_b >>= 1;
		if (!dst_b) {
			dst_b = 0x80;
			dst_pos++;
			data[dst_pos] = 0;
		}

		last_fell = fell;
		last = cur;
	}

	
#if 0
	for (i=0; i<dst_pos; i++) {
		int j;
		for (j=0; j<8; j++) {
			if (data[i] & (0x80 >> j)) {
				PORTB |= 0x10;
			} else {
				PORTB &= ~0x10;
			}
			_delay_us(5);
		}
	}
	PORTB &= ~0x10;
#endif

	return dst_pos;
}

/**
 * \param data Destination buffer to store reply (payload + crc + eot)
 * \param maxlen The length of the destination buffer
 * \return -1 on timeout, -2 lrc/frame error, otherwise the number of bytes received
 */
int maple_receivePacket(unsigned char *data, unsigned int maxlen)
{
	unsigned char *tmp = maplebuf;
	unsigned char lrc;
	int res, i;
	
	//
	//  __       _   _   _
	//    |_____| |_| |_| |_
	//  ___   _     _   _
	//     |_| |___| |_| |_
	//   310022011023102310
	//     ^   ^  ^  ^^  ^^
	//

	asm volatile( 
			"	push r30		\n" // 2
			"	push r31		\n"	// 2
		
//			"	sbi 0x5, 4		\n" // PB4
//			"	cbi 0x5, 4		\n"

			// Loop until a change is detected.	
			"	in r17, %1		\n"
			"wait_start:		\n"
			"	in r16, %1		\n"
			"	cp r16, r17		\n"
			"	breq wait_start	\n"

			"	sbi 0x5, 4		\n" // PB4
			"	cbi 0x5, 4		\n"

			// We will loose the first bit(s), but
			// it's only the start of frame.

			"start_rx:			\n"
			#include "rxcode.asm"			

			"	sbi 0x5, 4		\n" // PB4
			"	cbi 0x5, 4		\n"
			"	pop r31			\n" // 2
			"	pop r30			\n" // 2
		: "=z"(tmp)
		: "I" (_SFR_IO_ADDR(PINC))
		: "r16","r17") ;

	res = maplebus_decode(data, maxlen);
	if (res<=0)
		return res;

	// A packet containts n groups of 4 bytes, plus 1 byte crc.
	if (((res-1) & 0x3) != 0) {
		return -2; // frame error
	}

	for (lrc=0, i=0; i<res; i++) {
		lrc ^= data[i];
	}
	if (lrc)
		return -2; // LRC error

	return res-1; // remove lrc
}

void maple_sendPacket(unsigned char *data, unsigned char len)
{
	int i;
	unsigned char b;

	// SET PINS
	// SET PINS
	// SET PINS
	// DLY

	buf_reset();
	for (i=0; i<len; i++) {
		for (b=0x80; b; b>>=1)
		{
			buf_addBit(data[i] & b);
		}
	}

	// Output
	PORTC |= 0x03;
	DDRC |= 0x03;

	// DC controller pin 1 and pin 5
#define SET_1		"	sbi %0, 0\n"
#define CLR_1		"	cbi %0, 0\n"
#define SET_5		"	sbi %0, 1\n"
#define CLR_5		"	cbi %0, 1\n"
#define DLY_8		"	nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
#define DLY_4		"	nop\nnop\nnop\nnop\n"

	asm volatile(
		"push r31\n"
		"push r30\n"

		"mov r19, %1	\n" // Length in bytes		
		"ldi r20, 0x01	\n" // phase 1 pin 1 high, pin 5 low
		"ldi r21, 0x02	\n" // phase 2 pin 1 low, pin 2 high

		"ld r16, z+		\n"

		// Sync
		SET_1 SET_5
		DLY_8
		CLR_1 
		DLY_8

		CLR_5 	
		DLY_8
		SET_5
		DLY_8

		CLR_5
		DLY_8
		SET_5
		DLY_8

		CLR_5
		DLY_8
		SET_5
		DLY_8

		CLR_5
		DLY_8
		SET_5
		DLY_8

		SET_1
		CLR_5

		// Pin 5 is low, Pin 1 is high. Ready for 1st phase

		// Note: Coded for 16Mhz (8 cycles = 500ns)
"next_byte:\n"

		"out %0, r20	\n" // 1  initial phase 1 state
		"out %0, r16	\n" // 1  data
		"cbi %0, 0		\n" // 1  falling edge on pin 1
		"ld r16, z+		\n" // 2  load phase 2 data
		"dec r19		\n" // 1  Decrement counter for brne below
		"nop			\n" // 1
		"nop			\n" // 1
		
		"out %0, r21	\n" // 1  initial phase 2 state
		"out %0, r16	\n" // 1  data
		"cbi %0, 1		\n" // 1  falling edge on pin 5
		"ld r16, z+		\n" // 2
		"nop			\n" // 1
		"brne next_byte	\n" // 2

"done:\n"

		// End of transmission
		SET_5 DLY_4 CLR_5 DLY_4
		CLR_1 DLY_8 SET_1 DLY_8 CLR_1 DLY_8 SET_1 DLY_4 SET_5

		"pop r30		\n"
		"pop r31		\n"


		:
		: "I" (_SFR_IO_ADDR(PORTC)), "r"(buf_used/2), "z"(maplebuf)
		: "r1","r16","r17","r18","r19","r20","r21"
	);

	DDRC &= ~0x03;
}


void maple_sendFrame(uint32_t *words, unsigned char nwords)
{
	uint8_t data[nwords * 4 + 1];
	uint8_t *d;
	uint8_t lrc=0;
	int i;

	d = data;
	for (i=0; i<nwords; i++) {
		*d = words[i];
		lrc ^= *d;
		d++;

		*d = words[i] >> 8;
		lrc ^= *d;
		d++;

		*d = words[i] >> 16;
		lrc ^= *d;
		d++;

		*d = words[i] >> 24;
		lrc ^= *d;
		d++;
	}
	data[nwords *4] = lrc;
	
	maple_sendPacket(data, nwords * 4 + 1);	
}


