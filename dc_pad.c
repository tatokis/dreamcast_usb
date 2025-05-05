/* Dreamcast to USB : Sega dc controllers to USB adapter
 * Copyright (C) 2013 Rapha�l Ass�nat
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The author may be contacted at raph@raphnet.net
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include <string.h>
#include "usbdrv.h"
#include "gamepad.h"
#include "dc_pad.h"
#include "maplebus.h"
#include "usbconfig.h"

#define MOUSE_REPORT_SIZE		5
#define CONTROLLER_REPORT_SIZE	8
#define KEYBOARD_REPORT_SIZE	7
#define MAX_REPORT_SIZE			8


#define NUM_REPORTS				1

// report matching the most recent bytes from the controller
static unsigned char last_built_report[NUM_REPORTS][MAX_REPORT_SIZE];

// the most recently reported bytes
static unsigned char last_sent_report[NUM_REPORTS][MAX_REPORT_SIZE];

static unsigned char cur_report_size = CONTROLLER_REPORT_SIZE;

static Gamepad dcGamepad;

static void dcUpdate(void);

/*
 * [0] X
 * [1] Y
 * [2] Ltrig
 * [3] Rtrig
 * [4] Btn 0-3 (4-7 are the hat switch and ignored in the descriptor)
 * [5] Btn 8-15
 */
static const unsigned char dcPadReport[] PROGMEM = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,                    // USAGE (Game pad)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)

                                   // Analog stick
    0x35, 0x00,                    //     (GLOBAL) PHYSICAL_MINIMUM   0x00 (0)
    0x46, 0xff, 0x00,              //     (GLOBAL) PHYSICAL_MAXIMUM   0x00ff (255)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x15, 0x80,                    //     LOGICAL_MINIMUM (-128)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)

                                   // Triggers
    0x09, 0x33,                    //     USAGE (Rx)
    0x09, 0x34,                    //     USAGE (Ry)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0xff,                    //     LOGICAL_MAXIMUM (255)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)

                                   // Buttons
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x04,                    //     USAGE_MAXIMUM (Button 4)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)

                                   // Padding to ignore the hat switch
    0x81, 0x01,                    //     INPUT (Cnst,Arr,Abs)

                                   // Remaining buttons + 2 fake buttons for triggers
    0x19, 0x05,                    //     USAGE_MINIMUM (Button 5)
    0x29, 0x10,                    //     USAGE_MAXIMUM (Button 12+4)
    0x95, 0x0c,                    //     REPORT_COUNT (12)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
                                   //     Padding (6 bits)
    0x95, 0x04,                    //     REPORT_COUNT (6)
    0x81, 0x01,                    //     INPUT (Cnst,Arr,Abs)

                                   // D-Pad
    0x05, 0x01,                    //     (GLOBAL) USAGE_PAGE         0x0001 Generic Desktop Page
    0x09, 0x39,                    //     (LOCAL)  USAGE              0x00010039 Hat Switch (Dynamic Value)
    0x15, 0x01,                    //     (GLOBAL) LOGICAL_MINIMUM    0x01 (1)
    0x25, 0x08,                    //     (GLOBAL) LOGICAL_MAXIMUM    0x08 (8)
    0x46, 0x3B, 0x01,              //     (GLOBAL) PHYSICAL_MAXIMUM   0x013B (315)
    0x65, 0x14,                    //     (GLOBAL) UNIT               0x14 Rotation in degrees [1° units] (4=System=English Rotation, 1=Rotation=Degrees)
    0x75, 0x04,                    //     (GLOBAL) REPORT_SIZE        0x04 (4) Number of bits per field
    0x95, 0x01,                    //     (GLOBAL) REPORT_COUNT       0x01 (1) Number of fields
    0x81, 0x02,                    //     (MAIN)   INPUT              0x00000002 (1 field x 4 bits) 0=Data 1=Variable 0=Absolute 0=NoWrap 0=Linear 0=PrefState 0=NoNull 0=NonVolatile 0=Bitmap
    0xc0,                          //   END_COLLECTION
    0xc0,                          // END_COLLECTION
};

/*
 * [0] Mouse buttons
 * [1] Mouse X
 * [2] Mouse X
 * [3] Mouse Y
 * [4] Mouse Y
 */
static const unsigned char dcMouseReport[] PROGMEM = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                    // USAGE (Mouse)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x04,                    //     USAGE_MAXIMUM (Button 4)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x75, 0x04,                    //     REPORT_SIZE (4)
    0x81, 0x03,                    //     INPUT (Cnst,Var,Abs)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x16, 0x01, 0xFE,                    //     LOGICAL_MINIMUM (-511)
    0x26, 0xFF, 0x01,                    //     LOGICAL_MAXIMUM (511)
    0x75, 0x10,                    //     REPORT_SIZE (16)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)
    0xc0,                          //   END_COLLECTION
    0xc0,                          // END_COLLECTION
};


/* [0] Modifier byte
 * [1] Reserved
 * [2] Key array
 * [3] Key array
 * [4] Key array
 * [5] Key array
 * [6] Key array
 * [7] Key array
 *
 * See Universal Serial Bus HID Tables - 10 Keyboard/Keypad Page (0x07)
 * for key codes.
 *
 */
static const unsigned char dcKeyboardReport[] PROGMEM = {
	0x05, 0x01, // Usage page : Generic Desktop
	0x09, 0x06, // Usage (Keyboard)
	0xA1, 0x01, // Collection (Application)
		0x05, 0x07, // Usage Page (Key Codes)
		0x19, 0xE0, // Usage Minimum (224)
		0x29, 0xE7, // Usage Maximum (231)
		0x15, 0x00, // Logical Minimum (0)
		0x25, 0x01, // Logical Maximum (1)
		
			// Modifier Byte
		0x75, 0x01, // Report Size(1)
		0x95, 0x08, // Report Count(8)
		0x81, 0x02, // Input (Data, Variable, Absolute)

			// Reserved Byte
//		0x95, 0x01, // Report Count(1)
//		0x75, 0x08, // Report Size(8)

		0x95, 0x06, // Report Count(6)
		0x75, 0x08, // Report Size(8)
		0x15, 0x00, // Logical Minimum (0)
		0x26, 0xFF, 0x00, // Logical maximum (255)

			// Key array
//		0x05, 0x07, // Usage Page (key Codes)
		0x19, 0x00, // Usage Minimum(0)
		0x29, 0xFF, // Usage Maximum(255)
		0x81, 0x00, // Input (Data, Array)

    0xc0,                          // END_COLLECTION
};

const unsigned char dcPadDevDesc[] PROGMEM = {    /* USB device descriptor */
    18,         /* sizeof(usbDescrDevice): length of descriptor in bytes */
    USBDESCR_DEVICE,    /* descriptor type */
    0x01, 0x01, /* USB version supported */
    USB_CFG_DEVICE_CLASS,
    USB_CFG_DEVICE_SUBCLASS,
    0,          /* protocol */
    8,          /* max packet size */
    USB_CFG_VENDOR_ID,	// Vendor ID
    0 + USB_CFG_DEVICE_ID, // Product ID
    USB_CFG_DEVICE_VERSION, // Version: Minor, Major
    1, // Manufacturer String
    2, // Product string
    3, // Serial number string
    1, /* number of configurations */
};

const unsigned char dcMouseDevDesc[] PROGMEM = {    /* USB device descriptor */
    18,         /* sizeof(usbDescrDevice): length of descriptor in bytes */
    USBDESCR_DEVICE,    /* descriptor type */
    0x01, 0x01, /* USB version supported */
    USB_CFG_DEVICE_CLASS,
    USB_CFG_DEVICE_SUBCLASS,
    0,          /* protocol */
    8,          /* max packet size */
    USB_CFG_VENDOR_ID,	// Vendor ID
    1 + USB_CFG_DEVICE_ID, // Product ID
    USB_CFG_DEVICE_VERSION, // Version: Minor, Major
    1, // Manufacturer String
    2, // Product string
    3, // Serial number string
    1, /* number of configurations */
};

const unsigned char dcKeyboardDevDesc[] PROGMEM = {    /* USB device descriptor */
    18,         /* sizeof(usbDescrDevice): length of descriptor in bytes */
    USBDESCR_DEVICE,    /* descriptor type */
    0x01, 0x01, /* USB version supported */
    USB_CFG_DEVICE_CLASS,
    USB_CFG_DEVICE_SUBCLASS,
    0,          /* protocol */
    8,          /* max packet size */
    USB_CFG_VENDOR_ID,  // Vendor ID
    2 + USB_CFG_DEVICE_ID, // Product ID
    USB_CFG_DEVICE_VERSION, // Version: Minor, Major
    1, // Manufacturer String
    2, // Product string
    3, // Serial number string
    1, /* number of configurations */
};


#define DEFAULT_FUNCTION	MAPLE_FUNC_CONTROLLER

static uint16_t cur_connected_device = DEFAULT_FUNCTION;

/* Used to report the change of function to the main loop
 * which will then re-init. */
static char dcDescriptorsChanged(void)
{
	static uint16_t previous_function = DEFAULT_FUNCTION;

	if (cur_connected_device != previous_function) {
		previous_function = cur_connected_device;
		return 1;
	}
	return 0;
}

static void setConnectedDevice(uint16_t func)
{
	cur_connected_device = func;

	switch (func)
	{
		case MAPLE_FUNC_CONTROLLER:
			dcGamepad.reportDescriptor = (void*)dcPadReport;
			dcGamepad.reportDescriptorSize = sizeof(dcPadReport);
			dcGamepad.deviceDescriptor = (void*)dcPadDevDesc;
			dcGamepad.deviceDescriptorSize = sizeof(dcPadDevDesc);
			cur_report_size = CONTROLLER_REPORT_SIZE;
			break;

		case MAPLE_FUNC_MOUSE:
			dcGamepad.reportDescriptor = (void*)dcMouseReport;
			dcGamepad.reportDescriptorSize = sizeof(dcMouseReport);
			dcGamepad.deviceDescriptor = (void*)dcMouseDevDesc;
			dcGamepad.deviceDescriptorSize = sizeof(dcMouseDevDesc);
			cur_report_size = MOUSE_REPORT_SIZE;
			break;
		
		case MAPLE_FUNC_KEYBOARD:
			dcGamepad.reportDescriptor = (void*)dcKeyboardReport;
			dcGamepad.reportDescriptorSize = sizeof(dcKeyboardReport);
			dcGamepad.deviceDescriptor = (void*)dcKeyboardDevDesc;
			dcGamepad.deviceDescriptorSize = sizeof(dcKeyboardDevDesc);
			cur_report_size = KEYBOARD_REPORT_SIZE;
			break;
	}
}



static void dcInit(void)
{
	setConnectedDevice(DEFAULT_FUNCTION);

	/* Try to detect the exact peripheral before continuing. The allows
	 * the adapter to enumerate as the correct device right away. */
	dcUpdate();
	dcUpdate();
	dcUpdate();
}

#define MAX_ERRORS 100

#define STATE_RESET_DEVICE		0
#define STATE_GET_INFO			1
#define STATE_READ_PAD			2
#define STATE_READ_MOUSE		3
#define STATE_READ_KEYBOARD		4
#define STATE_LCD_DETECT		5
#define STATE_BANNER_DISPLAY	6
#define STATE_NULL				7
static unsigned char state = STATE_RESET_DEVICE;

const char lcd_data_raphnet[200] PROGMEM = {
	0x00, 0x00, 0x00, 0x04,
	0x00, 0x00, 0x00, 0x00,
#include "raphnet.c"				
};

const char lcd_data_image[200] PROGMEM = {
	0x00, 0x00, 0x00, 0x04,
	0x00, 0x00, 0x00, 0x00,
#include "blob.c"				
};


static uint8_t lcd_addr = 0;

static void updateLcd(char id)
{
	unsigned char tmp[30];

	if (lcd_addr) {
		maple_sendFrame_P(MAPLE_CMD_BLOCK_WRITE,
					lcd_addr,
					MAPLE_DC_ADDR | MAPLE_ADDR_PORTB,
					200, id ? lcd_data_image : lcd_data_raphnet);
		maple_receiveFrame(tmp, 30);
	}
}

static void pollSubs(void)
{
	int i, v;
	unsigned char tmp[30];

	for (i=0; i<5; i++) {
		maple_sendFrame(MAPLE_CMD_RQ_DEV_INFO,
						MAPLE_ADDR_SUB(i) | MAPLE_ADDR_PORTB,
						MAPLE_DC_ADDR | MAPLE_ADDR_PORTB,
						0, NULL);
		v =  maple_receiveFrame(tmp, 30);
		if (v==-2) {
			_delay_ms(2);
			uint16_t func = tmp[4] | tmp[5]<<8;

			if (func & MAPLE_FUNC_LCD) {
				lcd_addr = MAPLE_ADDR_SUB(i) | MAPLE_ADDR_PORTB;
			}
		}
	}
}
/*
static inline float boundf(const float a)
{
	const float f = (a < -128.f ? -128.f : a);
	return (f > 127.f ? 127.f : f);
}
*/
static inline int8_t axis_mult(const uint8_t a)
{
	// Let's pretend it's signed
	uint8_t b = a - 0x80;

	// Discard sign bit and shift to get the smaller number to add to the axis
	// Since we're doing everything manually, if the number is negative, fix it up first
	int8_t add;
	if(b & 0x80)
		add = -(int8_t)((~b & 0x7f) >> 2);
	else
		add = ((b & 0x7f) >> 2);

	int res = (int8_t)b + add;
	if(res > INT8_MAX)
	        return INT8_MAX;

	if(res < INT8_MIN)
		return INT8_MIN;

	return res;
}
static void dcReadPad(void)
{
	static unsigned char err_count = 0;
	unsigned char tmp[30];
	static unsigned char func_data[4];
	static int lcd_detect_count = 0;
	int v;

	switch (state)
	{
		case STATE_NULL:
		{
		}
		break;

		case STATE_RESET_DEVICE:
		{
			maple_sendFrame(MAPLE_CMD_RESET_DEVICE,
							MAPLE_ADDR_MAIN | MAPLE_ADDR_PORTA,
							MAPLE_DC_ADDR, 0, NULL);
			
			state = STATE_GET_INFO;
		}
		break;

		case STATE_GET_INFO:
		{
			maple_sendFrame(MAPLE_CMD_RQ_DEV_INFO,
							MAPLE_ADDR_MAIN | MAPLE_ADDR_PORTB,
							MAPLE_DC_ADDR | MAPLE_ADDR_PORTB, 0, NULL);

			v = maple_receiveFrame(tmp, 30);

			// Too much data arrives and we stop listening before the controller stop transmitting. The delay
			// here is to wait until the bus is idle again before continuing.
			_delay_ms(2); 
			if (v==-2) {
				uint16_t func;

				// 0-3 Header
				// 4-7 Func
				// ...
				
				func = tmp[4] | tmp[5]<<8;

				if (func & MAPLE_FUNC_CONTROLLER) {
					setConnectedDevice(MAPLE_FUNC_CONTROLLER);
					state = STATE_LCD_DETECT;
					lcd_detect_count = 0;
				} else if (func & MAPLE_FUNC_MOUSE) {
					state = STATE_READ_MOUSE;
					memcpy(func_data, tmp + 5, 4);
					setConnectedDevice(MAPLE_FUNC_MOUSE);
				} else if (func & MAPLE_FUNC_KEYBOARD) {
					state = STATE_READ_KEYBOARD;
					setConnectedDevice(MAPLE_FUNC_KEYBOARD);
				}
			}
			
			err_count = 0;
		}
		break;

			// Try for 2 seconds to find the address of the LCD.
			//
			// After 2 seconds of trying, if found, send the
			// image. 
			//
			// Sending the image right away after detection does not
			// seem to work. This delay works around this.
		case STATE_LCD_DETECT:
		{
			pollSubs();
			if (!lcd_addr) 
			{
			}
			else {
				if (lcd_detect_count > 220) {
					updateLcd(0);
					state = STATE_BANNER_DISPLAY;
					lcd_detect_count = 0;
					break;
				}
			}
			
			lcd_detect_count++;

			if (lcd_detect_count > 400) {
				state = STATE_READ_PAD;
			}
		}
		break;	

		case STATE_BANNER_DISPLAY:
		{
			lcd_detect_count++;
			if (lcd_detect_count > 400) {
				updateLcd(1);
				state = STATE_READ_PAD;
			}
		}
		break;
		
		case STATE_READ_MOUSE:
		{
			int16_t rel_x, rel_y;
			uint8_t btns;
			
			maple_sendFrame1W(MAPLE_CMD_GET_CONDITION, 
							MAPLE_ADDR_PORTB | MAPLE_ADDR_MAIN, 
							MAPLE_ADDR_PORTB | MAPLE_DC_ADDR, 
							MAPLE_FUNC_MOUSE);

			v = maple_receiveFrame(tmp, 30);
			
			// The mouse sends too much data, it fills the receive buffer. Also,
			// there is a pause in the transmission that does not help.
			// The wheel drata and the the LRC are missed.
			//
			// As such, mouse support is incomplete and a hack.
			//
/*			if (v==-2) {
				err_count++;
				if (err_count > MAX_ERRORS) {
					state = STATE_RESET_DEVICE;
				}
				return;
			}
			err_count = 0;
*/	
			// 8  : Buttons
			// 9  : Buttons
			// 10 : Buttons
			// 11 : Buttons 
			// 12 : Y axis MSB
			// 13 : Y axis LSB
			// 14 : X axis MSB
			// 15 : X axis LSB
			//
			// 16 : Wheel MSB
			// 17 : Wheel LSB
			

			// bit 0 : Middle button
			// bit 1 : Right button
			// bit 2 : Left button
			// bit 3 : Thumb button
			tmp[11] ^= 0xf;
			btns = 0;

			if (tmp[11] & 2) btns = 0x02; // DC Right -> USB btn 1
			if (tmp[11] & 4) btns = 0x01; // DC Left  -> USB btn 0

			// If the mouse has a physical middle button, let it work
			// normally. Otherwise, use the thumb button.
			if (func_data[2] & 0x01) {
				if (tmp[11] & 1) btns = 0x04; // DC Middle -> USB btn 2
				if (tmp[11] & 8) btns = 0x08; // DC Thumb -> USB btn 3
			} else {
				if (tmp[11] & 8) btns = 0x04; // DC Thumb -> btn 2
			}

			rel_x = (tmp[15] | tmp[14]<<8) - 0x200;
			rel_y = (tmp[13] | tmp[12]<<8) - 0x200;

			last_built_report[0][0] = btns;
			last_built_report[0][1] = rel_x & 0xff;
			last_built_report[0][2] = rel_x >> 8;
			last_built_report[0][3] = rel_y & 0xff;
			last_built_report[0][4] = rel_y >> 8;
		}
		break;

		case STATE_READ_PAD:
		{
			maple_sendFrame1W(MAPLE_CMD_GET_CONDITION, 
							MAPLE_ADDR_PORTB | MAPLE_ADDR_MAIN, 
							MAPLE_DC_ADDR | MAPLE_ADDR_PORTB,
							MAPLE_FUNC_CONTROLLER);

			v = maple_receiveFrame(tmp, 30);
			
			if (v<=0) {
				err_count++;
				if (err_count > MAX_ERRORS) {
					state = STATE_GET_INFO;
				}
				return;
			}
			err_count = 0;

			if (v < 16)
				return;

			// 8 : Buttons
			// 9 : Buttons
			// 10 : R trig
			// 11 : L trig
			// 12 : Joy X axis
			// 13 : Joy Y axis
			// 14 : Joy X2 axis
			// 15 : Joy Y2 axis

			// HACK: Limit the analog stick range to effectively map it to a square
			if(PIND & _BV(6)) {
                                last_built_report[0][0] = axis_mult(tmp[12]);
                                last_built_report[0][1] = axis_mult(tmp[13]);

				// This also works, but I really did not want to use floats for this
				/*float x = (signed char)(tmp[12] - (unsigned char)0x80);
				float y = (signed char)(tmp[13] - (unsigned char)0x80);

				x *= 1.35f;
				y *= 1.35f;

				last_built_report[0][0] = (signed char)boundf(x);
				last_built_report[0][1] = (signed char)boundf(y);*/
			} else {
				last_built_report[0][0] = tmp[12] - 0x80;
				last_built_report[0][1] = tmp[13] - 0x80;
			}
			last_built_report[0][2] = tmp[10];
			last_built_report[0][3] = tmp[11];
			last_built_report[0][4] = tmp[8] ^ 0xff;
			last_built_report[0][5] = tmp[9] ^ 0xff;
			last_built_report[0][6] = (tmp[10] != 0) | ((tmp[10] == 0xff) << 1) | ((tmp[11] != 0) << 2)  | ((tmp[11] == 0xff) << 3);
			switch(tmp[8] >> 4) {
				case 0xe:
					last_built_report[0][7] = 1;
					break;
				case 0x6:
					last_built_report[0][7] = 2;
					break;
				case 0x7:
					last_built_report[0][7] = 3;
					break;
				case 0x5:
					last_built_report[0][7] = 4;
					break;
				case 0xd:
					last_built_report[0][7] = 5;
					break;
				case 0x9:
					last_built_report[0][7] = 6;
					break;
				case 0xb:
					last_built_report[0][7] = 7;
					break;
				case 0xa:
					last_built_report[0][7] = 8;
					break;
				default:
					last_built_report[0][7] = 0;
			}
		}
		break;

		case STATE_READ_KEYBOARD:
		{
			maple_sendFrame1W(MAPLE_CMD_GET_CONDITION, 
							MAPLE_ADDR_PORTB | MAPLE_ADDR_MAIN, 
							MAPLE_DC_ADDR | MAPLE_ADDR_PORTB,
							MAPLE_FUNC_KEYBOARD);

			v = maple_receiveFrame(tmp, 30);

			if (v<=0) {
				err_count++;
				if (err_count > MAX_ERRORS) {
					state = STATE_GET_INFO;
				}
				return;
			}
			err_count = 0;
			
			if (v < 16)
				return;	

			// Dreamcast data
			// 
			// 8 : shift
			// 9 : led
			// 10 : key
			// 11 : key
			// 12 : key
			// 13 : key
			// 14 : key
			// 15 : key
			//
			//
			// The keycodes sent by the Dreamcast keyboards
			// do not require any translation ; they are usable as-is.
			//
			// Compare http://mc.pp.se/dc/kbd.html and
			// the USB HID Usage Table document table (10 Keyboard/Keypad Page (0x07))
			//
			last_built_report[0][0] = tmp[8]; // shift keys
			last_built_report[0][1] = 0; // Reserved
			last_built_report[0][2] = tmp[10];
			last_built_report[0][3] = tmp[11];
			last_built_report[0][4] = tmp[12];
			last_built_report[0][5] = tmp[13];
		}
		break;
	}
}

static void dcUpdate(void)
{
	dcReadPad();
}

static char dcBuildReport(unsigned char *reportBuffer, unsigned char report_id)
{
	report_id = 0;

	if (reportBuffer != NULL)
	{
		memcpy(reportBuffer, last_built_report[report_id], cur_report_size);
	}
	memcpy(last_sent_report[report_id], last_built_report[report_id], cur_report_size);	

	return cur_report_size;
}

static char dcChanged(unsigned char report_id)
{
	report_id = 0;

	return memcmp(last_built_report[report_id], last_sent_report[report_id], cur_report_size);
}

static Gamepad dcGamepad = {
	num_reports: 		1,
	init: 				dcInit,
	update: 			dcUpdate,
	changed:			dcChanged,
	buildReport:		dcBuildReport,
	descriptorsChanged:	dcDescriptorsChanged,
};

Gamepad *dcGetGamepad(void)
{
	return &dcGamepad;
}
