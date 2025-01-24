#include <stdint.h>
#include "tl_common.h"
#if BOARD == BOARD_LKTMZL02
#include "chip_8258/timer.h"
#include "lcd.h"
#include "device.h"
#include "sensors.h"
#include "battery.h"

#define VKL060_I2C_ADDR		0x3E // VKL060

#define _LCD_SPEED_CODE_SEC_ _attribute_ram_code_sec_

u8 display_buff[LCD_BUF_SIZE];
u8 display_cmp_buff[LCD_BUF_SIZE];
u8 i2c_address_lcd; // = 0x78; // B1.4 uses Address 0x78 and B1.9 uses 0x7c
u8 lcd_blink;

#define I2C_TCLK_US	2 // 2 us


/*
 *   LCD LKTMZL02 real buffer:  byte.bit

                                ------------|
 0.2 0.1 0.0					0.5 0.6	0.4 |
  |   |   |                      |   |   |  |0.7
                                ------------|

              --1.4--         --2.4--            --3.0--
       |    |         |     |         |        |         |
       |   1.5       1.1   2.5       2.1      3.1       4.5
       |    |         |     |         |        |         |      o 3.5
-1.0- 0.3     --1.2--         --2.2--            --4.6--          +--- 3.5
       |    |         |     |         |        |         |     3.5|
       |   1.6       1.3   2.6       2.3      3.2       4.7       ---- 3.4
       |    |         |     |         |        |         |     3.5|
              --1.7--         --2.7--     *      --3.3--          ---- 3.6
                                         3.7

          --4.0--         --5.0--
        |         |     |         |
       4.1       5.5   5.1       6.5
        |         |     |         |
          --5.6--         --6.6--
        |         |     |         |
       4.2       5.7   5.2       6.7     %
        |         |     |         |     6.4
          --4.3--         --5.3--


  None: 2.0, 4.4, 5.4, 6.0..6.3

 Work buffer:  byte.bit

                                ------------|
 0.2 0.1 0.0					0.5 0.6	0.4 |
  |   |   |                      |   |   |  |0.7
                                ------------|

              --1.4--         --2.4--            --4.4--
       |    |         |     |         |        |         |
       |   1.5       1.1   2.5       2.1      4.5       4.1
       |    |         |     |         |        |         |      o 3.1
-1.0- 0.3     --1.2--         --2.2--            --4.2--          +--- 3.1
       |    |         |     |         |        |         |     3.1|
       |   1.6       1.3   2.6       2.3      4.6       4.3       ---- 3.0
       |    |         |     |         |        |         |     3.1|
              --1.7--         --2.7--     *      --4.7--          ---- 3.2
                                         3.3

          --5.4--         --6.4--
        |         |     |         |
       5.5       5.1   6.5       6.1
        |         |     |         |
          --5.2--         --6.2--
        |         |     |         |
       5.6       5.3   6.6       6.3     %
        |         |     |         |     6.0
          --5.7--         --6.7--


  None: 2.0, 3.4..3.7, 4.0, 5.0

 */

/* 0,1,2,3,4,5,6,7,8,9,A,b,C,d,E,F*/
const uint8_t display_numbers[] = {
		// 76543210
		0b011111010, // 0
		0b000001010, // 1
		0b011010110, // 2
		0b010011110, // 3
		0b000101110, // 4
		0b010111100, // 5
		0b011111100, // 6
		0b000011010, // 7
		0b011111110, // 8
		0b010111110, // 9
		0b001111110, // A
		0b011101100, // b
		0b011110000, // C
		0b011001110, // d
		0b011110100, // E
		0b001110100  // F
};
                    //76543210
#define LCD_SYM_b  0b011101100 // "b"
#define LCD_SYM_H  0b001101110 // "H"
#define LCD_SYM_h  0b001101100 // "h"
#define LCD_SYM_i  0b001000000 // "i"
#define LCD_SYM_L  0b011100000 // "L"
#define LCD_SYM_o  0b011001100 // "o"
#define LCD_SYM_t  0b011100100 // "t"
#define LCD_SYM_0  0b011111010 // "0"
#define LCD_SYM_A  0b001111110 // "A"
#define LCD_SYM_a  0b011111100 // "a"
#define LCD_SYM_P  0b001110110 // "P"

#define LCD_SYM_BLE	0x07	// connect
#define LCD_SYM_BAT	0x80	// battery


void soft_i2c_start(void) {
	gpio_set_output_en(I2C_SCL_LCD, 0); // SCL set "1"
	gpio_set_output_en(I2C_SDA_LCD, 0); // SDA set "1"
	sleep_us(I2C_TCLK_US);
	gpio_set_output_en(I2C_SDA_LCD, 1); // SDA set "0"
	sleep_us(I2C_TCLK_US);
	gpio_set_output_en(I2C_SCL_LCD, 1); // SCL set "0"
	//sleep_us(10);
}

void soft_i2c_stop(void) {
	gpio_set_output_en(I2C_SDA_LCD, 1); // SDA set "0"
	sleep_us(I2C_TCLK_US);
	gpio_set_output_en(I2C_SCL_LCD, 0); // SCL set "1"
	sleep_us(I2C_TCLK_US);
	gpio_set_output_en(I2C_SDA_LCD, 0); // SDA set "1"
}

int soft_i2c_wr_byte(uint8_t b) {
	int ret, i = 8;
	while(i--) {
		sleep_us(I2C_TCLK_US/2);
		if(b & 0x80)
			gpio_set_output_en(I2C_SDA_LCD, 0); // SDA set "1"
		else
			gpio_set_output_en(I2C_SDA_LCD, 1); // SDA set "0"
		sleep_us(I2C_TCLK_US/2);
		gpio_set_output_en(I2C_SCL_LCD, 0); // SCL set "1"
		sleep_us(I2C_TCLK_US);
		gpio_set_output_en(I2C_SCL_LCD, 1); // SCL set "0"
		b <<= 1;
	}
	sleep_us(I2C_TCLK_US/2);
	gpio_set_output_en(I2C_SDA_LCD, 0); // SDA set "1"
	sleep_us(I2C_TCLK_US/2);
	gpio_set_output_en(I2C_SCL_LCD, 0); // SCL set "1"
	sleep_us(I2C_TCLK_US);
	ret = gpio_read(I2C_SDA_LCD);
	gpio_set_output_en(I2C_SCL_LCD, 1); // SCL set "0"
	return ret;
}

int soft_i2c_send_buf(uint8_t addr, uint8_t * pbuf, int size) {
	int ret = 0;
	soft_i2c_start();
	ret = soft_i2c_wr_byte(addr);
	if(ret == 0) {
		while(size--) {
			ret = soft_i2c_wr_byte(*pbuf);
			if(ret)
				break;
			pbuf++;
		}
	}
	soft_i2c_stop();
	return ret;
}
int soft_i2c_send_byte(uint8_t addr, uint8_t b) {
	int ret;
	soft_i2c_start();
	ret = soft_i2c_wr_byte(addr);
	if(ret == 0)
		soft_i2c_wr_byte(b);
	soft_i2c_stop();
	return ret;
}

#define lcd_send_i2c_byte(a)  soft_i2c_send_byte(i2c_address_lcd, a)
#define lcd_send_i2c_buf(b, a)  soft_i2c_send_buf(i2c_address_lcd, (uint8_t *) b, a)

const uint8_t lcd_init_cmd[]	=	{
		// LCD controller initialize:
		0xea, // System Set: Software Reset, Internal oscillator circuit
//		0xe8, // System Set: Internal oscillator circuit
		0xc8, // Mode Set: Display enable, 1/3 Bias
		0xbc, // Display control: 52 Hz, FRAME flip, low power mode1
//		0x80, // load data pointer
//		0xf0, // blink control off,  0xf2 - blink
//		0xfc, // All pixel control (APCTL): Normal
		0x0c, 0,0,0,0,0,0,0
};

_LCD_SPEED_CODE_SEC_
void send_to_lcd(void){
	if(g_zcl_thermostatUICfgAttrs.display_off)
		return;
	if (i2c_address_lcd) {
		uint8_t buf[8];
		buf[0] = 0x0b;
		buf[1] = display_buff[0];
		buf[2] = display_buff[1];
		buf[3] = display_buff[2];
		buf[4] = (display_buff[4] >> 4) | (display_buff[3] << 4);
		buf[5] = (display_buff[5] >> 4) | (display_buff[4] << 4);
		buf[6] = (display_buff[6] >> 4) | (display_buff[5] << 4);
		buf[7] = (display_buff[6] << 4);
		lcd_send_i2c_buf(buf, sizeof(buf));
	}
}

void init_lcd(void){
	i2c_address_lcd = VKL060_I2C_ADDR << 1;
	if(g_zcl_thermostatUICfgAttrs.display_off) {
		if(lcd_send_i2c_byte(0xD0) || lcd_send_i2c_byte(0xEA)) // LCD reset
			i2c_address_lcd = 0;
	} else {
		if(lcd_send_i2c_buf((uint8_t *) lcd_init_cmd, sizeof(lcd_init_cmd)))
			i2c_address_lcd = 0;
		else {
			// pm_wait_us(200);
			send_to_lcd();
		}
	}
}


#if 0
/* 0x00 = "  "
 * 0x20 = "°Г"
 * 0x40 = " -"
 * 0x60 = "°F"
 * 0x80 = " _"
 * 0xA0 = "°C"
 * 0xC0 = " ="
 * 0xE0 = "°E" */
_LCD_SPEED_CODE_SEC_
void show_temp_symbol(uint8_t symbol) {
	if (symbol & 0x20)
		display_buff[3] |= BIT(1);
	else
		display_buff[3] &= ~(BIT(1));
	if (symbol & 0x40)
		display_buff[3] |= BIT(0); //"-"
	else
		display_buff[3] &= ~BIT(0); //"-"
	if (symbol & 0x80)
		display_buff[3] |= BIT(2); // "_"
	else
		display_buff[3] &= ~BIT(2); // "_"
}
#endif

_LCD_SPEED_CODE_SEC_
void show_ble_symbol(bool state){
	if (state)
		display_buff[0] |= LCD_SYM_BLE;
	else 
		display_buff[0] &= ~LCD_SYM_BLE;
}

_LCD_SPEED_CODE_SEC_
void show_battery_symbol(bool state){
	display_buff[0] &= 0x0f;
	if (state) {
		display_buff[0] |= BIT(7);
		if (measured_battery.level >= 50) {
			display_buff[0] |= BIT(5);
			if (measured_battery.level >= 100) {
				display_buff[0] |= BIT(6);
				if (measured_battery.level >= 160) {
					display_buff[0] |= BIT(4);
				}
			}
		}
	}
}

/* number in 0.1 (-9995..19995), Show: -999 .. 1999
   symbol 0 - none, 1 - C, 2 - F  */
_LCD_SPEED_CODE_SEC_
__attribute__((optimize("-Os"))) void show_big_number_x10(s16 number, u8 symbol){

	display_buff[3] = 0;

	if(symbol == 1)
		display_buff[3] = BIT(1) | BIT(2);
	else if(symbol == 2)
		display_buff[3] = BIT(0) | BIT(1);

	display_buff[4] = 0;

	if (number > 19995) {
   		display_buff[1] = LCD_SYM_H; // "H"
   		display_buff[2] = LCD_SYM_i; // "i"
	} else if (number < -9995) {
   		display_buff[1] = LCD_SYM_L; // "L"
   		display_buff[2] = LCD_SYM_o; // "o"
	} else {
		display_buff[1] = 0;
		display_buff[2] = 0;
		/* number: -9995..19995 */
		if (number > 1995 || number < -995) {
			// show: -999..1999
			//display_buff[3] &= ~(BIT(3)); // no point
			if (number < 0){
				number = -number;
				display_buff[1] = BIT(0); // "-"
			}
			number = (number + 5) / 10; // round(div 10)
		} else {
			// show: -99.9..199.9
			display_buff[3] |= BIT(3); // point
			if (number < 0){
				number = -number;
				display_buff[1] = BIT(0); // "-"
			}
		}
		/* number: -99..1999 */
		if (number > 999) display_buff[0] |= BIT(3); // "1" 1000..1999
		else display_buff[0] &= ~(BIT(3)); // "0" -999..999
		if (number > 99) display_buff[1] |= display_numbers[number / 100 % 10];
		if (number > 9) display_buff[2] |= display_numbers[number / 10 % 10];
		else display_buff[2] |= LCD_SYM_0; // "0"
	    display_buff[4] = display_numbers[number %10];
	}
}

/* -9 .. 99 */
_LCD_SPEED_CODE_SEC_
__attribute__((optimize("-Os"))) void show_small_number(int16_t number, bool percent){
//	display_buff[5] = 0;
	display_buff[6] = percent? BIT(0) : 0;
	if (number > 99) {
		display_buff[5] |= LCD_SYM_H; // "H"
		display_buff[6] |= LCD_SYM_i; // "i"
	} else if (number < -9) {
		display_buff[5] |= LCD_SYM_L; // "L"
		display_buff[6] |= LCD_SYM_o; // "o"
	} else {
		if (number < 0) {
			number = -number;
			display_buff[5] = BIT(2); // "-"
		}
		if (number > 9) display_buff[5] = display_numbers[number / 10 % 10];
		display_buff[6] |= display_numbers[number %10];
	}
}

void show_blink_screen(void) {
	memset(&display_buff, 0, sizeof(display_buff));
	display_buff[0] = LCD_SYM_BLE;
	display_buff[1] = BIT(2); // "_"
	display_buff[2] = BIT(2); // "_"
	display_buff[4] = BIT(2); // "_"
	send_to_lcd();
	lcd_blink = 0xf2;
	lcd_send_i2c_byte(lcd_blink);
}

// #define SHOW_REBOOT_SCREEN()
void show_reboot_screen(void) {
	memset(&display_buff, 0xff, sizeof(display_buff));
	send_to_lcd();
}

#if	USE_DISPLAY_CLOCK
_LCD_SPEED_CODE_SEC_
void show_clock(void) {
	uint32_t tmp = utc_time_sec / 60;
	uint32_t min = tmp % 60;
	uint32_t hrs = (tmp / 60) % 24;
	display_buff[0] &= ~BIT(3);
	display_buff[1] = display_numbers[(hrs / 10) % 10];
	display_buff[2] = display_numbers[hrs % 10];
	display_buff[3] = 0;
	display_buff[4] = 0;
	display_buff[5] = display_numbers[(min / 10) % 10];
	display_buff[6] = display_numbers[min % 10];
}
#endif // USE_DISPLAY_CLOCK

_LCD_SPEED_CODE_SEC_
void update_lcd(void){
	if(g_zcl_thermostatUICfgAttrs.display_off)
		return;
	if(memcmp(display_cmp_buff, display_buff, sizeof(display_buff))) {
		send_to_lcd();
		memcpy(display_cmp_buff, display_buff, sizeof(display_buff));
	}
	if(lcd_blink == 0xf2) {
		lcd_blink = 0xf0;
		lcd_send_i2c_byte(lcd_blink);
	}
}

#endif // BOARD_LKTMZL02
