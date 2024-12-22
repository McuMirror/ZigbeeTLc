/*
 * epd_mho_c401n.c
 * Author: kloemi & pvvx
 */
#include "tl_common.h"
#if BOARD == BOARD_MHO_C401N
#include "chip_8258/timer.h"

#include "lcd.h"
#include "device.h"

#define _EPD_SPEED_CODE_SEC_ _attribute_ram_code_sec_

#define LOW     0
#define HIGH    1

/* MHO-C401-2022 LCD buffer:  byte.bit

  7.4  8.4--8.7-- .  10.4--10.7--- .       12.4--12.7--- .     BAT 14.6
   |    |         |    |           |         |           |
  7.4  8.3       9.2 10.3        11.2      12.3        13.2   o|--- 14.4
   |    |         |    |           |         |           |     |  --14.2
  7.4  8.2--8.6--9.1 10.2--10.6--11.1      12.2--12.6--13.1    |  --14.0
   |    |         |    |           |         |           |
  7.4  8.1       9.0 10.1        11.0      12.1        13.0
   |    |         |    |           |  11.4   |           |
  7.4  8.0--8.5-- .  10.0--10.5--- .    *  12.0--12.5--- .


       1.4--1.7-- .   3.4--3.7-- .
        |         |    |         |     *7.2 П6.6      П6.6
 BLE   1.3       2.2  3.3       4.2         U6.5      U6.5
 0.4    |         |    |         |               П6.0
       1.2--1.6--2.1  3.2--3.6--4.1    (7.0 `5.2 -5.6  )7.0 ;6.2
  @     |         |    |         |               U5.4
 0.0   1.1       2.0  3.1       4.0
        |         |    |         |               %5.0
       1.0--1.5-- .   3.0--3.5-- .

Background: 15=0xff ?
None: 0.1..0.3, 0.5..0.7, 2.3..2.7, 4.3..4.7, 5.1, 5,3, 5.5, 5.7, 6.1, 6.3, 6.7, 7.1, 7.3, 7.5..7.7, 9.3..9.7, 11.3, 11.5..11.7, 13.3..13.7, 14.1, 14.3, 14.5
*/


#define DEF_EPD_REFRESH_CNT	2048

u8 display_buff[LCD_BUF_SIZE];
u8 display_cmp_buff[LCD_BUF_SIZE];
u8 stage_lcd;
u8 epd_updated;
u16 lcd_refresh_cnt;

const u8 T_LUT_ping[5] = {0x07B, 0x081, 0x0E4, 0x0E7, 0x008};
const u8 T_LUT_init[14] = {0x082, 0x068, 0x050, 0x0E8, 0x0D0, 0x0A8, 0x065, 0x07B, 0x081, 0x0E4, 0x0E7, 0x008, 0x0AC, 0x02B };
const u8 T_LUT_work[9] = {0x082, 0x080, 0x000, 0x0C0, 0x080, 0x080, 0x062, 0x0AC, 0x02B};

//----------------------------------
// define segments
// the data in the arrays consists of {byte, bit} pairs of each segment
//----------------------------------
const u8 top_left[22] =   { 8, 7,  9, 2,  9, 1,  9, 0,  8, 5,  8, 0,  8, 1,  8, 2,  8, 3,  8, 4,  8, 6};
const u8 top_middle[22] = {10, 7, 11, 2, 11, 1, 11, 0, 10, 5, 10, 0, 10, 1, 10, 2, 10, 3, 10, 4, 10, 6};
const u8 top_right[22] =  {12, 7, 13, 2, 13, 1, 13, 0, 12, 5, 12, 0, 12, 1, 12, 2, 12, 3, 12, 4, 12, 6};
const u8 bottom_left[22] = {1, 7,  2, 2,  2, 1,  2, 0,  1, 5,  1, 0,  1, 1,  1, 2,  1, 3,  1, 4,  1, 6};
const u8 bottom_right[22]= {3, 7,  4, 2,  4, 1,  4, 0,  3, 5,  3, 0,  3, 1,  3, 2,  3, 3,  3, 4,  3, 6};

// These values closely reproduce times captured with logic analyser
#define delay_SPI_end_cycle() sleep_us(2)
#define delay_EPD_SCL_pulse() sleep_us(2)

/*
Now define how each digit maps to the segments:
          1
 10 :-----------
    |           |
  9 |           | 2
    |     11    |
  8 :-----------: 3
    |           |
  7 |           | 4
    |     5     |
  6 :----------- 
*/

const u8 digits[16][11] = {
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0},  // 0
    {2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0},   // 1
    {1, 2, 3, 5, 6, 7, 8, 10, 11, 0, 0}, // 2
    {1, 2, 3, 4, 5, 6, 10, 11, 0, 0, 0}, // 3
    {2, 3, 4, 8, 9, 10, 11, 0, 0, 0, 0}, // 4
    {1, 3, 4, 5, 6, 8, 9, 10, 11, 0, 0}, // 5
    {1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0}, // 6
    {1, 2, 3, 4, 10, 0, 0, 0, 0, 0, 0},  // 7
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, // 8
    {1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 0}, // 9
    {1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 0}, // A
    {3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0}, // b
    {5, 6, 7, 8, 11, 0, 0, 0, 0, 0, 0},  // c
    {2, 3, 4, 5, 6, 7, 8, 11, 0, 0, 0},  // d
    {1, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0}, // E
    {1, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0}  // F
};


/* 0=off, 1=happy, 2=sad */
void show_smiley(u8 state){
 	// off
	display_buff[5] &= ~(BIT(2) | BIT(4) | BIT(6)); // do not reset %
	display_buff[6] = 0;
	display_buff[7] &= ~(BIT(4));
	if (state == 1) {
		display_buff[7] |= BIT(0); /* (  ) */
		display_buff[5] |= BIT(4) | BIT(6);
		display_buff[6] |= BIT(4) | BIT(6);
	} else if (state == 2) {
		display_buff[7] |= BIT(0); /* (  ) */
		display_buff[6] |= BIT(0) | BIT(4) | BIT(6);
	}
}

void show_battery_symbol(bool state){
 	if (state)
		display_buff[14] |= BIT(6);
	else
		display_buff[14] &= ~BIT(6);
}


void show_ble_symbol(bool state){
 	if (state)
		display_buff[0] |= BIT(4); // "*"
	else
		display_buff[0] &= ~BIT(4);
}

void show_connected_symbol(bool state){
 	if (state)
		display_buff[0] |= BIT(0);
	else
		display_buff[0] &= ~BIT(0);
}

_EPD_SPEED_CODE_SEC_
__attribute__((optimize("-Os")))
static void epd_set_digit(u8 *buf, u8 digit, const u8 *segments) {
    // set the segments, there are up to 11 segments in a digit
    int segment_byte;
    int segment_bit;
    for (int i = 0; i < 11; i++) {
        // get the segment needed to display the digit 'digit',
        // this is stored in the array 'digits'
        int segment = digits[digit][i] - 1;
        // segment = -1 indicates that there are no more segments to display
        if (segment >= 0) {
            segment_byte = segments[2 * segment];
            segment_bit = segments[1 + 2 * segment];
            buf[segment_byte] |= (1 << segment_bit);
        }
        else
            // there are no more segments to be displayed
            break;
    }
}

/* number in 0.1 (-995..19995), Show: -99 .. -9.9 .. 199.9 .. 1999 */
_EPD_SPEED_CODE_SEC_
__attribute__((optimize("-Os")))
void show_big_number_x10(s16 number, u8 symbol){

	display_buff[14] &= ~(BIT(0) | BIT(2) | BIT(4));
 	if (symbol == 1) {
 		display_buff[14] |= BIT(4) | BIT(0);
 	} else if(symbol == 2) {
 		display_buff[14] |= BIT(4) | BIT(2);
 	}

	display_buff[8] = 0;
	display_buff[9] = 0;
	display_buff[10] = 0;
	display_buff[11] = 0; //"_"
	display_buff[12] = 0;
	display_buff[13] = 0;
	if (number > 19995) {
		// "Hi"
   		display_buff[8] = BIT(4);
   		display_buff[9] = BIT(0) | BIT(3);
   		display_buff[10] = BIT(0) | BIT(1) | BIT(6) | BIT(7);
   		display_buff[11] = BIT(4) | BIT(5) | BIT(6) | BIT(7);
	} else if (number < -995) {
		// "Lo"
   		display_buff[8] = BIT(4) | BIT(5);
   		display_buff[9] = BIT(0) | BIT(3) | BIT(4) | BIT(5);
   		display_buff[10] = BIT(3) | BIT(4) | BIT(5);
   		display_buff[11] = BIT(5) | BIT(6) | BIT(7);
	} else {
		/* number: -995..19995 */
		if (number > 1995 || number < -95) {
			display_buff[11] &= ~BIT(4); // no point, show: -99..1999
			if (number < 0){
				number = -number;
				display_buff[8] = BIT(6); // "-"
			}
			number = (number + 5) / 10; // round(div 10)
		} else { // show: -9.9..199.9
			display_buff[11] |= BIT(4); // point
			if (number < 0){
				number = -number;
				display_buff[8] = BIT(6); // "-"
			}
		}
		/* number: -99..1999 */
		if (number > 999) display_buff[7] |= BIT(4); // "1" 1000..1999
		if (number > 99) epd_set_digit(display_buff, number / 100 % 10, top_left);
		if (number > 9) epd_set_digit(display_buff, number / 10 % 10, top_middle);
		else epd_set_digit(display_buff, 0, top_middle);
		epd_set_digit(display_buff, number % 10, top_right);
	}
}

/* -9 .. 99 */
_EPD_SPEED_CODE_SEC_
__attribute__((optimize("-Os")))
void show_small_number(s16 number, bool percent){
	display_buff[1] = 0;
	display_buff[2] = 0;
	display_buff[3] = 0;
	display_buff[4] = 0;
	display_buff[5] &= ~BIT(0);
	if (percent)
		display_buff[5] |= BIT(0); // "%" TODO 'C', '.', '(  )' ?
	if (number > 99) {
		// "Hi"
		display_buff[1] |= BIT(1) | BIT(4);
		display_buff[2] |= BIT(0) | BIT(3);
		display_buff[3] |= BIT(2) | BIT(5);
		display_buff[4] |= BIT(4) | BIT(7);
	} else if (number < -9) {
		//"Lo"
		display_buff[1] |= BIT(2) | BIT(3);
		display_buff[2] |= BIT(4) | BIT(7);
		display_buff[3] |= BIT(6) | BIT(3);
		display_buff[4] |= BIT(2);
	} else {
		if (number < 0) {
			number = -number;
			display_buff[1] |= BIT(6); // "-"
		}
		if (number > 9) epd_set_digit(display_buff, number / 10 % 10, bottom_left);
		epd_set_digit(display_buff, number % 10, bottom_right);
	}
}

_EPD_SPEED_CODE_SEC_
__attribute__((optimize("-Os")))
static void transmit(u8 cd, u8 data_to_send) {
    gpio_write(EPD_SCL, LOW);
    gpio_write(EPD_CSB, LOW);
    delay_EPD_SCL_pulse();

    // send the first bit, this indicates if the following is a command or data
    gpio_write(EPD_SDA, cd);
    delay_EPD_SCL_pulse();
    gpio_write(EPD_SCL, HIGH);
    delay_EPD_SCL_pulse();

    // send 8 bits
    for (int i = 0; i < 8; i++) {
        // start the clock cycle
        gpio_write(EPD_SCL, LOW);
        // set the MOSI according to the data
        if (data_to_send & 0x80)
            gpio_write(EPD_SDA, HIGH);
        else
            gpio_write(EPD_SDA, LOW);
        // prepare for the next bit
        data_to_send = (data_to_send << 1);
        delay_EPD_SCL_pulse();
        // the data is read at rising clock (halfway the time MOSI is set)
        gpio_write(EPD_SCL, HIGH);
        delay_EPD_SCL_pulse();
    }

    // finish by ending the clock cycle and disabling SPI
    gpio_write(EPD_SCL, LOW);
    delay_SPI_end_cycle();
    gpio_write(EPD_CSB, HIGH);
    delay_SPI_end_cycle();
}


static void transmit_blk(u8 cd, const u8 * pdata, size_t size_data) {
	for (int i = 0; i < size_data; i++)
		transmit(cd, pdata[i]);
}

void init_lcd(void) {
	// pulse RST_N low for 110 microseconds
    gpio_write(EPD_RST, LOW);
    pm_wait_us(110);
	lcd_refresh_cnt = DEF_EPD_REFRESH_CNT;
    stage_lcd = 1; // Update/Init, stage 1
    epd_updated = 0;
    gpio_write(EPD_RST, HIGH);
	display_buff[15] = 0;
}

__attribute__((optimize("-Os"))) int task_lcd(void) {
	while (gpio_read(EPD_BUSY)) {
		switch (stage_lcd) {
		case 1: // Update/Init, stage 1
			transmit_blk(0, T_LUT_ping, sizeof(T_LUT_ping));
			stage_lcd = 2;
			break;
		case 2: // Update/Init, stage 2
			if (epd_updated == 0) {
				transmit_blk(0, T_LUT_init, sizeof(T_LUT_init));
			} else {
				transmit_blk(0, T_LUT_work, sizeof(T_LUT_work));
			}
			stage_lcd = 3;
			break;
		case 3: // Update/Init, stage 3
			transmit(0, 0x040);
			transmit(0, 0x0A9);
			transmit(0, 0x0A8);
			transmit_blk(1, display_buff, sizeof(display_buff));
			transmit(0, 0x0AB);
			transmit(0, 0x0AA);
			transmit(0, 0x0AF);
			if (epd_updated) {
				stage_lcd = 4;
				// EPD_BUSY: ~500 ms
			} else {
				epd_updated = 1;
				stage_lcd = 2;
				// EPD_BUSY: ~1000 ms
			}
			break;
		case 4: // Update, stage 4
			transmit(0, 0x0AE);
			transmit(0, 0x028);
			transmit(0, 0x0AD);
		default:
			if(!g_zcl_thermostatUICfgAttrs.display_off
			&& memcmp(display_cmp_buff, display_buff, sizeof(display_buff))) {
				memcpy(display_cmp_buff, display_buff, sizeof(display_buff));
				if (lcd_refresh_cnt) {
					lcd_refresh_cnt--;
					stage_lcd = 1;
				} else {
					//init_lcd(); // pulse RST_N low for 110 microseconds
					lcd_refresh_cnt = DEF_EPD_REFRESH_CNT;
				    stage_lcd = 1; // Update/Init, stage 1
				    epd_updated = 0;
				}
			} else
				stage_lcd = 0;
		}
		if(!stage_lcd)
			break;
	}
#if PM_ENABLE
	cpu_set_gpio_wakeup(EPD_BUSY, Level_High , stage_lcd != 0);
#endif
	return stage_lcd;
}

void show_blink_screen(void) {
	memset(display_buff, 0, LCD_BUF_SIZE);
	display_buff[1] = BIT(6);
	display_buff[3] = BIT(6);
	display_buff[8] = BIT(6);
	display_buff[10] = BIT(6);
	task_lcd();
}

#if	USE_CLOCK

void show_clock(void) {
	uint32_t tmp = utc_time_sec / 60;
	uint32_t min = tmp % 60;
	uint32_t hrs = tmp / 60 % 24;
	memset(display_buff, 0, sizeof(display_buff));
	epd_set_digit(display_buff, min / 10 % 10, bottom_left);
	epd_set_digit(display_buff, min % 10, bottom_right);
	epd_set_digit(display_buff, hrs / 10 % 10, top_left);
	epd_set_digit(display_buff, hrs % 10, top_middle);
}
#endif // USE_CLOCK

#endif // BOARD == BOARD_MHO_C401N
