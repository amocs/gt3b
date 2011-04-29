/*
    lcd - LCD routines
    Copyright (C) 2011 Pavel Semerad

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



/* LCD controler is HOLTEK HT1621B */

#include "gt3b.h"
#include "lcd.h"
#include <string.h>


// HT1621B modes
#define HT_COMMAND	0b100
// 6 address bits (but only 5 used in this chip) and 4 bits of data
#define HT_WRITE	0b101
#define HT_READ		0b110

// HT1621B commands, 9bits, but last bit is used in no command
#define HT_SYS_DIS	0b00000000
#define HT_SYS_EN	0b00000001
#define HT_LCD_OFF	0b00000010
#define HT_LCD_ON	0b00000011
#define HT_TIMER_DIS	0b00000100
#define HT_WDT_DIS	0b00000101
#define HT_TIMER_EN	0b00000110
#define HT_WDT_EN	0b00000111
#define HT_TONE_OFF	0b00001000
#define HT_TONE_ON	0b00001001
#define HT_CLR_TIMER	0b00001100
#define HT_CLR_WDT	0b00001110
#define HT_XTAL_32K	0b00010100
#define HT_RC_256K	0b00011000
#define HT_EX_256K	0b00011100
#define HT_BIAS_12	0b00100000
#define HT_BIAS_13	0b00100001
    #define HT_BIAS_SHIFT 2
#define HT_TONE_4K	0b01000000
#define HT_TONE_2K	0b01100000
#define HT_IRQ_DIS	0b10000000
#define HT_IRQ_EN	0b10001000
#define HT_F1		0b10100000
#define HT_F2		0b10100001
#define HT_F4		0b10100010
#define HT_F8		0b10100011
#define HT_F16		0b10100100
#define HT_F32		0b10100101
#define HT_F64		0b10100110
#define HT_F128		0b10100111
#define HT_TEST		0b11100000
#define HT_NORMAL	0b11100011


// microcontroller pins setting
#define CS0 BRES(PC_ODR, 3)
#define CS1 BSET(PC_ODR, 3)
#define WR0 BRES(PF_ODR, 4)
#define WR1 BSET(PF_ODR, 4)
#define DATA0 BRES(PE_ODR, 5)
#define DATA1 BSET(PE_ODR, 5)
#define BCK0 BRES(PD_ODR, 2)
#define BCK1 BSET(PD_ODR, 2)





// LCD task
TASK(LCD, 128);


/* low level LCD routines and initialisation */

// bit which will be sent to LCD controller in timer interrupt
static u16 lcd_tmr_bits;
static u8  lcd_tmr_cnt;

@interrupt void lcd_interrupt(void) {
    BRES(TIM4_SR, 0);		// clear intr flag
    if (--lcd_tmr_cnt & 0x01) {
	// rising WR/ edge
	WR1;
	return;
    }
    if (lcd_tmr_cnt) {
	// falling WR/ edge and new data
	WR0;
	if (lcd_tmr_bits & 0x8000) {
	    DATA1;
	}
	else {
	    DATA0;
	}
	lcd_tmr_bits <<= 1;	// to next bit
	return;
    }
    // end of sending
    BRES(TIM4_CR1, 0);		// disable timer
    awake(LCD);			// wakeup LCD task
}


// send cnt bits to LCD controller, only low bits of "bits" are used
// use timer4 to get 600kHz clock for driving WR/ signal
static void lcd_send_bits(u8 cnt, u16 bits) {
    // if last sending not yet done, wait for it stopping task
    while (BCHK(TIM4_CR1, 0)) {
	stop();
    }
    // initialise variables
    lcd_tmr_bits = bits << 16 - cnt;
    lcd_tmr_cnt = (u8)(cnt << 1); // 2 timer more to generate WR0 and WR1
    // reset and start timer
    TIM4_CNTR = 0;		// reset timer value
    BSET(TIM4_CR1, 0);		// enable timer
    // write first bit
    WR0;
    if (lcd_tmr_bits & 0x8000) {
	DATA1;
    }
    else {
	DATA0;
    }
    lcd_tmr_bits <<= 1;	// to next bit
}


// send command to LCD
static void lcd_command(u8 cmd) {
    CS0;
    lcd_send_bits(12, (HT_COMMAND << 9) | (cmd << 1));
    CS1;
}


static void lcd_loop(void);

// initialize LCD pins and LCD controller
void lcd_init(void) {
    // set direction of pins
    IO_OPF(C, 3);  // CS/ pin
    IO_OP(E, 5);   // DATA pin
    IO_OP(F, 4);   // WR/ pin
    IO_OP(D, 2);   // Backlight pin

    // set default values to pins
    CS1;
    WR1;
    DATA1;
    BCK0;

    // initialize timer 4 used to time WR/ signal
    BSET(CLK_PCKENR1, 4);     // enable clock to TIM4
    TIM4_CR1 = 0b00000100;    // no auto-reload, URS-overflow, disable
    TIM4_IER = 1;             // enable overflow interrupt
    TIM4_PSCR = 0;            // prescaler = 1
    TIM4_ARR = 30;            // it will be about 600kHz for WR/ signal

    // initialize LCD task, will be used in following lcd_command-s
    build(LCD);
    activate(LCD, lcd_loop);

    // initialize HT1621B
    lcd_command(HT_BIAS_13 | (0b10 << HT_BIAS_SHIFT));  // BIAS 1/3, 4 COMs
    lcd_command(HT_RC_256K);  // clock RC 256kHz
    lcd_command(HT_SYS_DIS);  // OSC+BIAS off
    lcd_command(HT_WDT_DIS);  // disable WDT
    lcd_command(HT_SYS_EN);   // OSC on
    lcd_command(HT_LCD_ON);   // BIAS on

}






/* actual LCD segment memory and updating routine */

#define MAX_SEGMENT 32
static u8 lcd_segments[MAX_SEGMENT];
// bit flags of segments, which were modified from last update
static volatile u16 lcd_modified_segments;
static volatile u16 lcd_modified_segments2;

// flags used for communicating between tasks to activate LCD update
static volatile _Bool lcd_update_flag;
static volatile _Bool lcd_clr_flag;
static volatile _Bool lcd_set_flag;


// set LCD segment to given comms, remember modified only when different
// will be sent to LCD controller after update
static void lcd_seg_comms(u8 segment, u8 comms) {
    u8 *seg = &lcd_segments[segment];
    if (*seg == comms)  return;		// no change
    *seg = comms;
    if (segment < 16) {
	lcd_modified_segments |= (u16)1 << segment;
    }
    else {
	lcd_modified_segments2 |= (u16)1 << (segment - 16);
    }
}


// send changed LCD segments to LCD controller
static void lcd_seg_update(void) {
    u8 i;
    u16 bit;
    for (i = 0; i < MAX_SEGMENT; i++) {
	if (i < 16) {
	    bit = (u16)1 << i;
	    if (!(lcd_modified_segments & bit))  continue;
	    lcd_modified_segments &= ~bit;
	}
	else {
	    bit = (u16)1 << (i - 16);
	    if (!(lcd_modified_segments2 & bit))  continue;
	    lcd_modified_segments2 &= ~bit;
	}
	CS0;
	lcd_send_bits(13, (HT_WRITE << 10) | (i << 4) | lcd_segments[i]);
	CS1;
	if (lcd_clr_flag || lcd_set_flag)  break;
    }
}





/* routines for manipulating each segment individually and blinking */

// low 4 bits contains actual segment values
// high 8 bits contains segments, which will be blinking
static u8 lcd_bitmap[MAX_SEGMENT];
static volatile _Bool lcd_was_inverted;	// 1=there was some blink inversion
volatile _Bool lcd_blink_flag;		// set in timer interrupt
volatile u8 lcd_blink_cnt;		// blink counter updated in timer


// set LCD segment "pos" ON or OFF
// pos contains SEGMENT address in bits 0-4 and COM number in bits 7-8
// set bit in array lcs_segments only and set flag that this segment was
//   changed (segment can be changed more times and we will send it to
//   LCD controller only once)
void lcd_segment(u8 pos, u8 on_off) {
    u8 segment = (u8)(pos & 0b00011111);
    u8 com_bit = (u8)(1 << ((pos & 0b11000000) >> 6));
    if (on_off) {
	lcd_bitmap[segment] |= com_bit;
    }
    else {
	lcd_bitmap[segment] &= (u8)~com_bit;
    }
}


// set blinking state for given segment
// 0 - off
// 1 - on only when corresponding LCD segment is on
// 2 - on always
void lcd_segment_blink(u8 pos, u8 on_off) {
    u8 segment = (u8)(pos & 0b00011111);
    u8 com_bit = (u8)(1 << ((pos & 0b11000000) >> 6));
    u8 blnk_bit = (u8)(com_bit << 4);
    if (on_off) {
	if (on_off == 1 && !(lcd_bitmap[segment] & com_bit))  return;  // is OFF
	lcd_bitmap[segment] |= blnk_bit;
    }
    else {
	lcd_bitmap[segment] &= (u8)~blnk_bit;
    }
}


// show normal LCD bitmap
static void lcd_show_normal(void) {
    u8 i;
    lcd_update_flag = 0;
    lcd_was_inverted = 0;
    lcd_blink_cnt = 0;		// reset blink counter
    for (i = 0; i < MAX_SEGMENT; i++) {
	lcd_seg_comms(i, (u8)(lcd_bitmap[i] & 0xf));
    }
    lcd_seg_update();
}


// show blinked LCD bitmap
static void lcd_show_inverted(void) {
    u8 i, c;
    lcd_was_inverted = 0;
    for (i = 0; i < MAX_SEGMENT; i++) {
	c = lcd_bitmap[i];
	if (!(c & 0xf0))  continue;	// nothing to invert
	lcd_seg_comms(i, (u8)((c & 0xf) ^ ((c & 0xf0) >> 4)));
	lcd_was_inverted = 1;
    }
    if (lcd_was_inverted)  lcd_seg_update();
}


// do LCD item blinking in regular timer times
static void lcd_blink(void) {
    u8 on_off;
    lcd_blink_flag = 0;
    if (lcd_blink_cnt < LCD_BLNK_CNT_BLANK) {
	// do it only when something was inverted
	if (lcd_was_inverted)  lcd_show_normal();
    }
    else {
	lcd_show_inverted();
    }
}






// full LCD clear or set all segments ON
// quicker way, doing continuous-write to LCD controller
// memory locations representing LCD states must be set accordingly
static void lcd_clr_set(u16 data) {
    u8 i;
    // clear flags
    lcd_clr_flag = 0;
    lcd_set_flag = 0;
    lcd_update_flag = 0;
    lcd_blink_flag = 0;
    lcd_blink_cnt = 0;		// reset blink counter
    // set memory locations
    memset(lcd_segments, (u8)(data & 0x0f), MAX_SEGMENT);
    memset(lcd_bitmap, (u8)(data & 0x0f), MAX_SEGMENT);
    lcd_modified_segments = 0;
    lcd_modified_segments2 = 0;
    lcd_was_inverted = 0;
    CS0;
    // set lcd address to 0
    lcd_send_bits(9, HT_WRITE << 6);
    // send 32x 4bits of data in group of 16 bits
    for (i = 0; i < MAX_SEGMENT / 4; i++) {
	lcd_send_bits(16, data);
    }
    CS1;
}





// main LCD task, handle update/cle/set/blink requests
static void lcd_loop(void) {
    u8 i;
    while (1) {
	// process clear flag
	if (lcd_clr_flag) {
	    lcd_clr_set(0);
	    continue;
	}
	// process set flag
	if (lcd_set_flag) {
	    lcd_clr_set(0xffff);
	    continue;
	}
	// process update flag
	if (lcd_update_flag) {
	    lcd_show_normal();
	    continue;
	}
	// process blink flag
	if (lcd_blink_flag) {
	    lcd_blink();
	    continue;
	}
	// wait for next wakeup
	stop();
    }
}






// LCD segment addresses

// bits from left-top down, then next column
const u8 lcd_seg_char1[] = {
    0xda, 0x1a, 0x5a, 0x9a, 0x9d, 0x5d, 0x1d,
    0xd9, 0x19, 0x59, 0x99, 0x9e, 0x5e, 0x1e,
    0xd8, 0x18, 0x58, 0x98, 0x9f, 0x5f, 0x1f,
    0xd7, 0x17, 0x57, 0x97, 0x91, 0x51, 0x11,
    0xd6, 0x16, 0x56, 0x96, 0x90, 0x50, 0x10,
    0
};
const u8 lcd_seg_char2[] = {
    0xd5, 0x15, 0x55, 0x95, 0x8f, 0x4f, 0x0f,
    0xd4, 0x14, 0x54, 0x94, 0x8e, 0x4e, 0x0e,
    0xd3, 0x13, 0x53, 0x93, 0x8d, 0x4d, 0x0d,
    0xd2, 0x12, 0x52, 0x92, 0x8c, 0x4c, 0x0c,
    0xc0, 0x00, 0x40, 0x80, 0x8b, 0x4b, 0x0b,
    0
};
const u8 lcd_seg_char3[] = {
    0xc1, 0x01, 0x41, 0x81, 0x8a, 0x4a, 0x0a,
    0xc2, 0x02, 0x42, 0x82, 0x89, 0x49, 0x09,
    0xc3, 0x03, 0x43, 0x83, 0x88, 0x48, 0x08,
    0xc4, 0x04, 0x44, 0x84, 0x87, 0x47, 0x07,
    0xc5, 0x05, 0x45, 0x85, 0x86, 0x46, 0x06,
    0
};
const u8 lcd_seg_7seg[] = {
    0x9c, 0x1c, 0x9b, 0x5c, 0xdb, 0x5b, 0x1b,
    0
};
// first line, second line
const u8 lcd_seg_menu[] = {
    0xd0, 0xcd, 0xcb, 0xca, 0xd1, 0xce, 0xcc, 0xc9,
    0
};

static const struct lcd_items_s {
    u8 *segments;	// address of lcd_seg_...
    u8 bits;		// number of bits used in each bitmap byte
} lcd_items[5] = {
    { lcd_seg_char1, 7 },
    { lcd_seg_char2, 7 },
    { lcd_seg_char2, 7 },
    { lcd_seg_7seg, 7 },
    { lcd_seg_menu, 8 }
};




// set LCD element id to given bitmap
void lcd_set(u8 id, u8 *bitmap) {
    struct lcd_items_s *li = &lcd_items[id];
    u8 *seg = li->segments;	// actual lcd segment pointer
    u8 sp = *seg++;		// actual lcd segment
    u8 bits = li->bits;		// number of bits in bitmap
    u8 bitpos = 8;		// bit position in bitmap byte
    u8 fake_bitmap = (u8)(bitmap >= (u8 *)0xff00);
    u8 bm;			// actual byte of bitmap
    if (fake_bitmap) {
	// special addresses are actually bitmap for all segments
	bm = (u8)((u16)bitmap & 0xff);
    }
    else {
	bm = *bitmap++;
    }
    do {
	// for seven-bits bitmaps, rotate first
	if (bitpos > bits) {
	    bm <<= 1;
	    bitpos -= 1;
	}
	// set segment
	lcd_segment(sp, (u8)(bm & 0x80 ? LS_ON : LS_OFF));
	if (!(sp = *seg++))  break; 
	// to next bitmap bit/byte
	if (bitpos > 0) {
	    // next bit
	    bm <<= 1;
	    bitpos -= 1;
	}
	else {
	    // next bitmap byte
	    bitpos = 8;
	    if (fake_bitmap) {
		bm = (u8)((u16)bitmap & 0xff);
	    }
	    else {
		bm = *bitmap++;
	    }
	}
    } while (1);
}


// set blink for LCD element id
void lcd_set_blink(u8 id, u8 on_off) {
    u8 *seg = lcd_items[id].segments;
    do {
	lcd_segment_blink(*seg, on_off);
    } while (*++seg);
}






// functions to write characters from character map to char id

// character map, columns from left to right, least significant bit down
static const u8 lcd_charmap[] = {
    0b0000000, 0b0000000, 0b0000000, 0b0000000, 0b0000000,	// space
    0b0000000, 0b0000000, 0b1111010, 0b0000000, 0b0000000,	// !
    0b0000000, 0b1110000, 0b0000000, 0b1110000, 0b0000000,	// "
    0b0010100, 0b1111111, 0b0010100, 0b1111111, 0b0010100,	// #
    0b0010010, 0b0101010, 0b1111111, 0b0101010, 0b0100100,	// $
    0b0010001, 0b0001001, 0b0000100, 0b0110010, 0b0110001,	// %
    0b0110110, 0b1001001, 0b1010101, 0b0100010, 0b0000101,	// &
    0b0000000, 0b1010000, 0b1100000, 0b0000000, 0b0000000,	// '
    0b0000000, 0b0011100, 0b0100010, 0b1000001, 0b0000000,	// (
    0b0000000, 0b1000001, 0b0100010, 0b0011100, 0b0000000,	// )
    0b0010100, 0b0001000, 0b0111110, 0b0001000, 0b0010100,	// *
    0b0001000, 0b0001000, 0b0111110, 0b0001000, 0b0001000,	// +
    0b0000000, 0b0000000, 0b0000101, 0b0000110, 0b0000000,	// ,
    0b0000100, 0b0000100, 0b0000100, 0b0000100, 0b0000100,	// -
    0b0000000, 0b0000011, 0b0000011, 0b0000000, 0b0000000,	// .
    0b0000010, 0b0000100, 0b0001000, 0b0010000, 0b0100000,	// /
    0b0111110, 0b1000101, 0b1001001, 0b1010001, 0b0111110,	// 0
    0b0000000, 0b0100001, 0b1111111, 0b0000001, 0b0000000,	// 1
    0b0100001, 0b1000011, 0b1000101, 0b1001001, 0b0110001,	// 2
    0b1000010, 0b1000001, 0b1010001, 0b1101001, 0b1000110,	// 3
    0b0001100, 0b0010100, 0b0100100, 0b1111111, 0b0000100,	// 4
    0b1110010, 0b1010001, 0b1010001, 0b1010001, 0b1001110,	// 5
    0b0011110, 0b0101001, 0b1001001, 0b1001001, 0b0000110,	// 6
    0b1000000, 0b1000111, 0b1001000, 0b1010000, 0b1100000,	// 7
    0b0110110, 0b1001001, 0b1001001, 0b1001001, 0b0110110,	// 8
    0b0110000, 0b1001001, 0b1001001, 0b1001010, 0b0111100,	// 9
    0b0000000, 0b0110110, 0b0110110, 0b0000000, 0b0000000,	// :
    0b0000000, 0b0110101, 0b0110110, 0b0000000, 0b0000000,	// ;
    0b0001000, 0b0010100, 0b0100010, 0b1000001, 0b0000000,	// <
    0b0010100, 0b0010100, 0b0010100, 0b0010100, 0b0010100,	// =
    0b0000000, 0b1000001, 0b0100010, 0b0010100, 0b0001000,	// >
    0b0100000, 0b1000000, 0b1000101, 0b1001000, 0b0110000,	// ?
    0b0100110, 0b1001001, 0b1001101, 0b1000101, 0b0111110,	// @
    0b0111111, 0b1000100, 0b1000100, 0b1000100, 0b0111111,	// A
    0b1111111, 0b1001001, 0b1001001, 0b1001001, 0b0110110,	// B
    0b0111110, 0b1000001, 0b1000001, 0b1000001, 0b0100010,	// C
    0b1111111, 0b1000001, 0b1000001, 0b0100010, 0b0011100,	// D
    0b1111111, 0b1001001, 0b1001001, 0b1001001, 0b1000001,	// E
    0b1111111, 0b1001000, 0b1001000, 0b1001000, 0b1000000,	// F
    0b0111110, 0b1000001, 0b1001001, 0b1001001, 0b0101111,	// G
    0b1111111, 0b0001000, 0b0001000, 0b0001000, 0b1111111,	// H
    0b0000000, 0b1000001, 0b1111111, 0b1000001, 0b0000000,	// I
    0b0000010, 0b0000001, 0b1000001, 0b1111110, 0b1000000,	// J
    0b1111111, 0b0001000, 0b0010100, 0b0100010, 0b1000001,	// K
    0b1111111, 0b0000001, 0b0000001, 0b0000001, 0b0000001,	// L
    0b1111111, 0b0100000, 0b0011000, 0b0100000, 0b1111111,	// M
    0b1111111, 0b0010000, 0b0001000, 0b0000100, 0b1111111,	// N
    0b0111110, 0b1000001, 0b1000001, 0b1000001, 0b0111110,	// O
    0b1111111, 0b1001000, 0b1001000, 0b1001000, 0b0110000,	// P
    0b0111110, 0b1000001, 0b1000101, 0b1000010, 0b0111101,	// Q
    0b1111111, 0b1001000, 0b1001100, 0b1001010, 0b0110001,	// R
    0b0110001, 0b1001001, 0b1001001, 0b1001001, 0b1000110,	// S
    0b1000000, 0b1000000, 0b1111111, 0b1000000, 0b1000000,	// T
    0b1111110, 0b0000001, 0b0000001, 0b0000001, 0b1111110,	// U
    0b1111100, 0b0000010, 0b0000001, 0b0000010, 0b1111100,	// V
    0b1111110, 0b0000001, 0b0001110, 0b0000001, 0b1111110,	// W
    0b1100011, 0b0010100, 0b0001000, 0b0010100, 0b1100011,	// X
    0b1110000, 0b0001000, 0b0000111, 0b0001000, 0b1110000,	// Y
    0b1000011, 0b1000101, 0b1001001, 0b1010001, 0b1100001,	// Z
};

// 7seg map, numbers 0-9,a-f(10-15)
// columns from left to right and top to down
static const u8 lcd_7segmap[] = {
    0b1110111,	// 0
    0b0000011,	// 1
    0b0111110,	// 2
    0b0011111,	// 3
    0b1001011,	// 4
    0b1011101,	// 5
    0b1111101,	// 6
    0b0010011,	// 7
    0b1111111,	// 8
    0b1011111,	// 9
    0b1111011,	// A
    0b1101101,	// b
    0b0101100,	// c
    0b0101111,	// d
    0b1111100,	// E
    0b1111000	// F
};


// write char to LCD item id (LCHR1..3)
void lcd_char(u8 id, u8 c) {
    lcd_set(id, lcd_charmap + (c - LCHAR_MIN) * LCD_CHAR_COLS);
}


// write 3 chars
void lcd_chars(u8 *chars) {
    lcd_char(LCHR1, *chars++);
    lcd_char(LCHR2, *chars++);
    lcd_char(LCHR3, *chars);
}


// common conversion to 2 chars
static u8 chr[3];  // results of number->string conversions
static void lcd_num2char(u8 num) {
    chr[1] = (u8)('0' + num / 10);
    chr[2] = (u8)('0' + num % 10);
}


// write unsigned number to 3 chars (>=1000 as 'A')
void lcd_char_num3(u16 num) {
    chr[0] = (u8)('0' + num / 100);
    lcd_num2char((u8)(num % 100));
    // if more than 999, go to letters
    if (chr[0] > '9')  chr[0] += 'A' - '0' - 10;
    // remove leading spaces
    if (chr[0] == '0') {
	chr[0] = ' ';
	if (chr[1] == 0)  chr[1] = ' ';
    }
    lcd_chars(chr);
}


// write signed number, max -99..99
void lcd_char_num2(s8 num) {
    // check signum
    u8 sig = ' ';
    if (num < 0) {
	sig = '-';
	num = (u8)-num;
    }
    lcd_num2char(num);
    // remove leading spaces
    if (chr[1] == '0') {
	chr[1] = sig;
	chr[0] = ' ';
    }
    else  chr[0] = sig;
    lcd_chars(chr);
}


// write signed number, use labels for <0, =0, >0 (eg. "LNR")
void lcd_char_num2_lbl(s8 num, u8 *labels) {
    // set label based on signum
    if (num == 0)  chr[0] = labels[1];
    else if (num > 0)  chr[0] = labels[2];
    else {
	num = (u8)-num;
	chr[0] = labels[0];
    }
    lcd_num2char(num);
    lcd_chars(chr);
}


// write given number to 7-segment display
void lcd_7seg(u8 number) {
    lcd_set(L7SEG, lcd_7segmap + number);
}


// select menu-s
void lcd_menu(u8 menus) {
    lcd_set(LMENU, (u8 *)(menus | 0xff00));
}






// update/clear/set LCD display

// send modified data to LCD controller
void lcd_update(void) {
    lcd_update_flag = 1;
    awake(LCD);
    stop();
}


// clear entire LCD
void lcd_clr(void) {
    lcd_clr_flag = 1;
    awake(LCD);
    stop();
}


// set all LCD segments ON
void lcd_set_full_on(void) {
    lcd_set_flag = 1;
    awake(LCD);
    stop();
}
