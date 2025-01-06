/*
	MIT License

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.

	Developer: Truong Hy
	Version  : 20241220
	Target   : ARM Cortex-A9 on the DE10-Nano development board
	           (Intel Cyclone V SoC FPGA)
	Type     : Standalone C application

	An example standalone program demonstrating the readout of the 3-axis digital
	accelerometer (Analog Devices ADXL345) through the I2C interface.
	Measurements are sent to the USB-UART port.  View them with a serial terminal
	program with these settings: 115200 baud, 8 data bits, no parity and 1 stop
	bit.

	I2C interface
	-------------

	On the DE10-Nano board the ADXL345 is configured for I2C and is connected to
	the HPS I2C0 controller.  The I2C rate is 400kHz and it responds to the 7-bit
	slave device address 0x53.

	Optional INT1 pin
	-----------------
	
	The ADXL345 has an optional INT1 pin connected to the HPS GPIO61.  Depending
	on its interrupt settings the ADXL345 will toggle this pin when a trigger is
	set or cleared, which we can read using the HPS GPIO2 register group.  In this
	example it is used to detect when new samples arrive.  Setting the define
	OPT_ADXL345_INT1_ENABLE to 1 will make use of this pin, but if set to 0 then
	polling is used instead.
*/

// Arm CMSIS includes
#include "RTE_Components.h"   // CMSIS
#include CMSIS_device_header  // CMSIS

// Trulib includes
#include "tru_c5soc_hps_i2c_ll.h"
#include "tru_c5soc_hps_gpio_ll.h"
#include "tru_adxl345_ll.h"
#include "tru_logger.h"

// Intel HWLIB includes
#include "alt_clock_manager.h"

// Interrupt options
#define OPT_ADXL345_INT1_ENABLE       0                         // 0 = polling mode, 1 = interrupt via INT1 pin
// FIFO options
#define OPT_ADXL345_FIFO_ENABLE       1                         // 0 = Bypass (don't use FIFO), 1 = FIFO mode (use FIFO)
#define OPT_ADXL345_WATERLEVEL        1                         // 1 to 31 = sets the number of entries that will start a trigger
// Rate and range options
#define OPT_ADXL345_RATE              TRU_ADXL345_RATE_3P13_HZ  // See tru_adxl345_ll.h for the list of rates
#define OPT_ADXL345_RANGE             TRU_ADXL345_RANGE_2G      // See tru_adxl345_ll.h for the list of ranges
// Calibration offset options
#define OPT_ADXL345_OFSX              0                         // OFFSETX = OFSX * 15.6mg
#define OPT_ADXL345_OFSY              0                         // OFFSETY = OFSY * 15.6mg
#define OPT_ADXL345_OFSZ              0                         // OFFSETZ = OFSZ * 15.6mg
// Tap detect options.  Tap only work with a fast measurement rate, e.g. 50Hz or more
#define OPT_ADXL345_TAP_SINGLE_ENABLE 1
#define OPT_ADXL345_TAP_DOUBLE_ENABLE 1
#define OPT_ADXL345_TAP_X_ENABLE      0
#define OPT_ADXL345_TAP_Y_ENABLE      0
#define OPT_ADXL345_TAP_Z_ENABLE      1
#define OPT_ADXL345_TAP_THR           0x4   // THRESHOLD = THR * 62.5 mg
#define OPT_ADXL345_TAP_DUR           0x30  // DURATION = DUR * 625 us
#define OPT_ADXL345_TAP_LAT           0x3   // LATENT = LAT * 1.25ms
#define OPT_ADXL345_TAP_WIN           0x50  // WINDOW = WIN * 1.25ms

// DE10-Nano specific setting
#define DE10N_ADXL345_INT1_GPIO_PINNUM 61

uint8_t buffer[1];

typedef struct{
	int16_t x;
	int16_t y;
	int16_t z;
}tru_adxl345_data;

typedef struct{
	uint32_t l4_sp_clock_freq_hz;
	uint32_t sample_count;
	tru_adxl345_data sample;
}tru_adxl345_accel_t;

tru_adxl345_accel_t accel;

void setup_adxl345(void){
	accel.sample_count = 0;

	// Get the L4 Slave Peripheral clock frequency
	alt_clk_freq_get(ALT_CLK_L4_SP, &accel.l4_sp_clock_freq_hz);
	//printf("L4_SP_CLK = %u Hz\n", accel.l4_sp_clock_freq_hz);

	// Initialise
	tru_adxl345_i2c_init(accel.l4_sp_clock_freq_hz, TRU_ADXL345_I2C_SPEED_KHZ, TRU_HPS_I2C_CON_ADDR_7BIT, TRU_ADXL345_I2C_DEV_ADDR);

	// Read ADXL345 device ID from the ADXL345
	tru_adxl345_i2c_read(buffer, 1, TRU_ADXL345_DEVID_ADDR);
	printf("Device ID: 0x%.2x\n", buffer[0]);

	tru_adxl345_i2c_stop_flush_fifo();

	// Set calibration offsets
	buffer[0] = OPT_ADXL345_OFSX;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_OFSX_ADDR);
	buffer[0] = OPT_ADXL345_OFSY;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_OFSY_ADDR);
	buffer[0] = OPT_ADXL345_OFSZ;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_OFSZ_ADDR);

#if OPT_ADXL345_TAP_SINGLE_ENABLE == 1 || OPT_ADXL345_TAP_DOUBLE_ENABLE == 1
	buffer[0] = OPT_ADXL345_TAP_THR;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_THRESH_TAP_ADDR);
	buffer[0] = OPT_ADXL345_TAP_DUR;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_DUR_ADDR);
	buffer[0] = OPT_ADXL345_TAP_LAT;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_LATENT_ADDR);
	buffer[0] = OPT_ADXL345_TAP_WIN;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_WINDOW_ADDR);

	TRU_ADXL345_TAP_AXES_PTR(buffer)->val = 0;
	TRU_ADXL345_TAP_AXES_PTR(buffer)->bits.tap_x_en = OPT_ADXL345_TAP_X_ENABLE;
	TRU_ADXL345_TAP_AXES_PTR(buffer)->bits.tap_y_en = OPT_ADXL345_TAP_Y_ENABLE;
	TRU_ADXL345_TAP_AXES_PTR(buffer)->bits.tap_z_en = OPT_ADXL345_TAP_Z_ENABLE;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_TAP_AXES_ADDR);
#endif

	// Set ADXL345 output rate
	TRU_ADXL345_BW_RATE_PTR(buffer)->val = 0;
	TRU_ADXL345_BW_RATE_PTR(buffer)->bits.rate = OPT_ADXL345_RATE;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_BW_RATE_ADDR);

	// Set ADXL345 data options
	TRU_ADXL345_DATA_FORMAT_PTR(buffer)->val = 0;
	TRU_ADXL345_DATA_FORMAT_PTR(buffer)->bits.range = OPT_ADXL345_RANGE;
	TRU_ADXL345_DATA_FORMAT_PTR(buffer)->bits.fullres = 1;
	TRU_ADXL345_DATA_FORMAT_PTR(buffer)->bits.intinvert = 1;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_DATA_FORMAT_ADDR);

#if OPT_ADXL345_FIFO_ENABLE == 1
	// Set ADXL345 FIFO mode
	TRU_ADXL345_FIFO_CTL_PTR(buffer)->val = 0;
	TRU_ADXL345_FIFO_CTL_PTR(buffer)->bits.fifomode = TRU_ADXL345_FIFOMODE_STREAM;
	TRU_ADXL345_FIFO_CTL_PTR(buffer)->bits.samples = OPT_ADXL345_WATERLEVEL;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_FIFO_CTL_ADDR);
#else
	// Set ADXL345 FIFO mode off
	TRU_ADXL345_FIFO_CTL_PTR(buffer)->val = 0;
	TRU_ADXL345_FIFO_CTL_PTR(buffer)->bits.fifomode = TRU_ADXL345_FIFOMODE_BYPASS;
	TRU_ADXL345_FIFO_CTL_PTR(buffer)->bits.samples = 16;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_FIFO_CTL_ADDR);
#endif

	// Set ADXL345 which triggers generate interrupts
	TRU_ADXL345_INT_ENABLE_PTR(buffer)->val = 0;
	TRU_ADXL345_INT_ENABLE_PTR(buffer)->bits.watermark = OPT_ADXL345_FIFO_ENABLE;
	TRU_ADXL345_INT_ENABLE_PTR(buffer)->bits.doubletap = OPT_ADXL345_TAP_DOUBLE_ENABLE;
	TRU_ADXL345_INT_ENABLE_PTR(buffer)->bits.singletap = OPT_ADXL345_TAP_SINGLE_ENABLE;
	TRU_ADXL345_INT_ENABLE_PTR(buffer)->bits.dataready = 1;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_INT_ENABLE_ADDR);

	// Set ADXL345 interrupt output on INT1 pin
	TRU_ADXL345_INT_MAP_PTR(buffer)->val = 0x0;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_INT_MAP_ADDR);

	// Set ADXL345 to start measuring
	TRU_ADXL345_POWER_CTL_PTR(buffer)->val = 0;
	TRU_ADXL345_POWER_CTL_PTR(buffer)->bits.measure = 1;
	tru_adxl345_i2c_write(buffer, 1, TRU_ADXL345_POWER_CTL_ADDR);
}

// Polling read method
void poll_read(void){
	tru_adxl345_int_source_t int_source;

	while(1){
		int_source.val = 0;

#if OPT_ADXL345_FIFO_ENABLE == 1
		// Get current number of sample entries in the FIFO
		do
			tru_adxl345_i2c_read(buffer, 1, TRU_ADXL345_FIFO_STATUS_ADDR);
		while(TRU_ADXL345_FIFO_STATUS_PTR(buffer)->bits.entries < OPT_ADXL345_WATERLEVEL);
		//printf("ADXL345 FIFO entries = %u\n", TRU_ADXL345_FIFO_STATUS_PTR(buffer)->bits.entries);

		// Read interrupt triggers
		tru_adxl345_i2c_read(&int_source, 1, TRU_ADXL345_INT_SOURCE_ADDR);

		if(int_source.bits.singletap && int_source.bits.doubletap){
			printf("%.10u: TAPPED + DOUBLE\n", accel.sample_count);
		}else if(int_source.bits.singletap){
			printf("%.10u: TAPPED\n", accel.sample_count);
		}

		// Read out samples from ADXL345 FIFO
		for(uint8_t i = 0; i < TRU_ADXL345_FIFO_STATUS_PTR(buffer)->bits.entries; i++){
			tru_adxl345_i2c_read_bm(&accel.sample, 6, TRU_ADXL345_DATAX0_ADDR);
			printf("%.10u: x=%-4i y=%-4i z=%-4i\n", accel.sample_count, accel.sample.x, accel.sample.y, accel.sample.z);
			accel.sample_count++;
		}
#else
		// Wait for data available
		do{
			tru_adxl345_i2c_read(buffer, 1, TRU_ADXL345_INT_SOURCE_ADDR);
			int_source.val |= buffer[0];
		}while(int_source.bits.dataready == 0);

		if(int_source.bits.singletap && int_source.bits.doubletap){
			printf("%.10u: TAPPED + DOUBLE\n", accel.sample_count);
		}else if(int_source.bits.singletap){
			printf("%.10u: TAPPED\n", accel.sample_count);
		}

		// Read out samples
		tru_adxl345_i2c_read_bm(&accel.sample, 6, TRU_ADXL345_DATAX0_ADDR);
		printf("%.10u: x=%-4i y=%-4i z=%-4i\n", accel.sample_count, accel.sample.x, accel.sample.y, accel.sample.z);
		accel.sample_count++;
#endif
	}
}

// Interrupt handler for the ADXL345 INT1 pin
static void gpio2_irq_handler(void){
	tru_adxl345_int_source_t int_source;

	// Read interrupt triggers
	tru_adxl345_i2c_read(&int_source, 1, TRU_ADXL345_INT_SOURCE_ADDR);

	if(int_source.bits.singletap && int_source.bits.doubletap){
		printf("%.10u: TAPPED + DOUBLE\n", accel.sample_count);
	}else if(int_source.bits.singletap){
		printf("%.10u: TAPPED\n", accel.sample_count);
	}

#if OPT_ADXL345_FIFO_ENABLE == 1
	if(int_source.bits.watermark == 1){
		// Get current number of sample entries in the FIFO
		tru_adxl345_i2c_read(buffer, 1, TRU_ADXL345_FIFO_STATUS_ADDR);
		//printf("ADXL345 FIFO entries = %u\n", TRU_ADXL345_FIFO_STATUS_PTR(buffer)->bits.entries);

		// Read out samples from ADXL345 FIFO
		for(uint8_t i = 0; i < TRU_ADXL345_FIFO_STATUS_PTR(buffer)->bits.entries; i++){
			tru_adxl345_i2c_read_bm(&accel.sample, 6, TRU_ADXL345_DATAX0_ADDR);
			printf("%.10u: x=%-4i, y=%-4i, z=%-4i\n", accel.sample_count, accel.sample.x, accel.sample.y, accel.sample.z);
			accel.sample_count++;
		}
	}
#else
	if(int_source.bits.dataready == 1){
		// Read out samples
		tru_adxl345_i2c_read_bm(&accel.sample, 6, TRU_ADXL345_DATAX0_ADDR);
		printf("%.10u: x=%-4i, y=%-4i, z=%-4i\n", accel.sample_count, accel.sample.x, accel.sample.y, accel.sample.z);
		accel.sample_count++;
	}
#endif
}

// Setup ADXL345 INT1 pin
void setup_adxl345_int1_pin(void){
	tru_hps_gpio2_ll_reset_release();
	tru_hps_gpio2_ll_set_pin_input(DE10N_ADXL345_INT1_GPIO_PINNUM);  // Set ADXL345 INT1 GPIO as input mode
	tru_hps_gpio2_ll_int_enable(DE10N_ADXL345_INT1_GPIO_PINNUM);     // Enable interrupt on ADXL345 INT1 GPIO

	irq_mask(0);  // Enable IRQ mode interrupts for this CPU
	IRQ_SetHandler(C5SOC_GPIO2_IRQn, gpio2_irq_handler);  // Register user interrupt handler
	IRQ_SetPriority(C5SOC_GPIO2_IRQn, GIC_IRQ_PRIORITY_LEVEL29_7);  // Set lowest usable priority
	IRQ_SetMode(C5SOC_GPIO2_IRQn, IRQ_MODE_TYPE_IRQ | IRQ_MODE_CPU_0 | IRQ_MODE_TRIG_LEVEL | IRQ_MODE_TRIG_LEVEL_HIGH);
	IRQ_Enable(C5SOC_GPIO2_IRQn);  // Enable the interrupt
}

int main(void){
	printf("ADXL345 accelerometer example\n");

	setup_adxl345();

	// Use interrupt? else poll
#if(OPT_ADXL345_INT1_ENABLE == 1)
	setup_adxl345_int1_pin();
	while(1);
#else
	poll_read();
#endif

	return 0;
}
