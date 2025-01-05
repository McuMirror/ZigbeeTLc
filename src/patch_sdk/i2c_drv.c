#include "tl_common.h"
#include "chip_8258/register.h"
//#include "app_cfg.h"
#include "i2c_drv.h"

#if USE_I2C_DRV

#define _I2C_SEG_

_I2C_SEG_
void init_i2c(void) {
	i2c_gpio_set(I2C_GROUP); // I2C_GPIO_GROUP_C0C1, I2C_GPIO_GROUP_C2C3, I2C_GPIO_GROUP_B6D7, I2C_GPIO_GROUP_A3A4
	reg_i2c_speed = (unsigned char)(CLOCK_SYS_CLOCK_HZ/(4*I2C_CLOCK)); // 100, 400, 700 kHz
    //reg_i2c_id  = slave address
    reg_i2c_mode |= FLD_I2C_MASTER_EN; //enable master mode
	reg_i2c_mode &= ~FLD_I2C_HOLD_MASTER; // Disable clock stretching for Sensor

    reg_clk_en0 |= FLD_CLK0_I2C_EN;    //enable i2c clock
    reg_spi_sp  &= ~FLD_SPI_ENABLE;   //force PADs act as I2C; i2c and spi share the hardware of IC
}

unsigned char scan_i2c_addr(unsigned char address){
	unsigned char r = irq_disable();
	if ((reg_clk_en0 & FLD_CLK0_I2C_EN)==0)
		init_i2c();
#if I2C_CLOCK != 100000
	unsigned char s = reg_i2c_speed;
	reg_i2c_speed = (u8)(CLOCK_SYS_CLOCK_HZ/(4*100000)); // 100 kHz
#endif
	reg_i2c_id = (unsigned char) address;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID | FLD_I2C_CMD_STOP;
	while (reg_i2c_status & FLD_I2C_CMD_BUSY);
#if I2C_CLOCK != 100000
	reg_i2c_speed = s;
#endif
	irq_restore(r);
	return ((reg_i2c_status & FLD_I2C_NAK)? 0 : address);
}

int send_i2c_byte(unsigned char i2c_addr, unsigned char cmd) {
	unsigned char r = irq_disable();
	if ((reg_clk_en0 & FLD_CLK0_I2C_EN)==0)
			init_i2c();
	reg_i2c_id = i2c_addr;
	reg_i2c_adr = cmd;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_STOP;
	while (reg_i2c_status & FLD_I2C_CMD_BUSY);
	irq_restore(r);
	return (reg_i2c_status & FLD_I2C_NAK);
}

_I2C_SEG_
int send_i2c_bytes(unsigned char i2c_addr, unsigned char * dataBuf, size_t dataLen) {
	int err = 0;
	unsigned char r = irq_disable();
	if ((reg_clk_en0 & FLD_CLK0_I2C_EN)==0)
			init_i2c();
	unsigned char * p = dataBuf;
	reg_i2c_id = i2c_addr;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID;
	while (reg_i2c_status & FLD_I2C_CMD_BUSY);
	err = reg_i2c_status & FLD_I2C_NAK;
	if(!err) {
		while (dataLen--) {
			reg_i2c_do = *p++;
			reg_i2c_ctrl = FLD_I2C_CMD_DO;
			while (reg_i2c_status & FLD_I2C_CMD_BUSY);
			err = reg_i2c_status & FLD_I2C_NAK;
			if(err)
				break;
		}
	}
	reg_i2c_ctrl = FLD_I2C_CMD_STOP;
	while (reg_i2c_status & FLD_I2C_CMD_BUSY);
	irq_restore(r);
	return err;
}

_I2C_SEG_
int read_i2c_bytes(unsigned char i2c_addr, unsigned char * dataBuf, int dataLen) {
	int ret = -1;
	int size = dataLen;
	unsigned char *p = dataBuf;
	unsigned char r = irq_disable();
	if ((reg_clk_en0 & FLD_CLK0_I2C_EN)==0)
			init_i2c();
	reg_i2c_id = i2c_addr | FLD_I2C_WRITE_READ_BIT;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID | FLD_I2C_CMD_READ_ID;
    while(reg_i2c_status & FLD_I2C_CMD_BUSY);
	ret = reg_i2c_status & FLD_I2C_NAK;
	if(ret == 0) {
		while(size) {
			size--;
			if(!size)
				reg_i2c_ctrl = FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID | FLD_I2C_CMD_ACK;
			else
				reg_i2c_ctrl = FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID;
			while(reg_i2c_status & FLD_I2C_CMD_BUSY);
			*p++ = reg_i2c_di;
		}
	}
	reg_i2c_ctrl = FLD_I2C_CMD_STOP;
    while(reg_i2c_status & FLD_I2C_CMD_BUSY);
    irq_restore(r);
    return ret;
}


/* Universal I2C/SMBUS read-write transaction
 * wrlen = 0..127 ! */
_I2C_SEG_
int I2CBusUtr(void * outdata, i2c_utr_t * tr, unsigned int wrlen) {
	unsigned char * pwrdata = (unsigned char *) &tr->wrdata;
	unsigned char * poutdata = (unsigned char *) outdata;
	unsigned int cntstart = wrlen - (tr->mode & 0x7f);
	unsigned int rdlen = tr->rdlen & 0x7f;

	unsigned char r = irq_disable();
	if ((reg_clk_en0 & FLD_CLK0_I2C_EN)==0)
			init_i2c();

	reg_i2c_id = *pwrdata++;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID;
	while(reg_i2c_status & FLD_I2C_CMD_BUSY);

	int ret = reg_i2c_status & FLD_I2C_NAK;
	while(ret == 0 && wrlen) {
		// write data
		reg_i2c_do = *pwrdata++;
		reg_i2c_ctrl = FLD_I2C_CMD_DO;
		while(reg_i2c_status & FLD_I2C_CMD_BUSY);
		ret = reg_i2c_status & FLD_I2C_NAK;

		if(--wrlen == cntstart && ret == 0) { // + send start & id
			if(tr->mode & 0x80) {
				reg_i2c_ctrl = FLD_I2C_CMD_STOP;
			} else {
				// hw reset I2C
				reg_rst0 = FLD_RST0_I2C;
				reg_rst0 = 0;
			}
			while(reg_i2c_status & FLD_I2C_CMD_BUSY);
			reg_i2c_id = tr->wrdata[0] |  FLD_I2C_WRITE_READ_BIT;
			// start + id
			reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID;
			while(reg_i2c_status & FLD_I2C_CMD_BUSY);
			ret = reg_i2c_status & FLD_I2C_NAK;
		}
	}
	if(ret == 0) {
		while(rdlen) {
			if(--rdlen == 0 && (tr->rdlen & 0x80) == 0)
				reg_i2c_ctrl = FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID | FLD_I2C_CMD_ACK;
			else
				reg_i2c_ctrl = FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID;
			while(reg_i2c_status & FLD_I2C_CMD_BUSY);
			*poutdata++ = reg_i2c_di;
		}
	}
	reg_i2c_ctrl = FLD_I2C_CMD_STOP; // launch start/stop cycle
	while(reg_i2c_status & FLD_I2C_CMD_BUSY);
	irq_restore(r);
	return ret;
}

#endif // #if USE_I2C_DRV
