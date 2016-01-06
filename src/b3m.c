/**
 * Kondo B3M 0.1 Library
 *
 * Copyright 2016 - Yasuo Hayashibara (yasuo@hayashibara.net)
 * Chiba Institute of Technology
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "kondo_driver/b3m.h"

/*-----------------------------------------------------------------------------
 * Convenience macros
 */
#define b3m_error(ki, err) { \
  snprintf(ki->error, 128, "ERROR: %s: %s\n", __func__, err); \
  return -1; }
#define b3m_ftdi_error(ki) { \
  snprintf(ki->error, 128, "ERROR: %s: %s\n", __func__, \
    ftdi_get_error_string(&ki->ftdic)); \
  return -1; }

/*-----------------------------------------------------------------------------
 * Iniitialize the B3M interface
 * Here we mainly setup the FTDI USB-to-serial communication
 * 115200 baud, 8 bits, even parity, 1 stop bit.
 * Returns: 0 if successful, < 0 otherwise
 */
int b3m_init(B3MData * r, int product_id)
{
	assert(r);
	r->debug = 0;

	// init usb
	if (ftdi_init(&r->ftdic) < 0) {
	    fprintf (stderr, "init");
		b3m_ftdi_error(r);
	}
	// select first interface
	if (ftdi_set_interface(&r->ftdic, INTERFACE_A) < 0) {
	    fprintf (stderr, "set_interface");
	    b3m_ftdi_error(r);
	}
	// open usb device
	if (ftdi_usb_open(&r->ftdic, B3M_USB_VID, product_id) < 0) {
	    fprintf (stderr, "open");
	    b3m_ftdi_error(r);
	}

	// set baud rate
	if (ftdi_set_baudrate(&r->ftdic, B3M_BAUD) < 0) {
	    fprintf (stderr, "baud");
		b3m_ftdi_error(r);
		}
	// set line parameters (8E1)
	if (ftdi_set_line_property(&r->ftdic, BITS_8, STOP_BIT_1, EVEN) < 0) {
	    fprintf (stderr, "param");
		b3m_ftdi_error(r);
		}

	return 0;
}

/*-----------------------------------------------------------------------------
 * Close / Deinitialize the B3M Interface.
 * Mainly closes the USB device and deletes the allocated data.
 * Returns 0 if successful, < 0 otherwise
 */
int b3m_close(B3MData * r)
{
	assert(r);

	// close usb device
	if (ftdi_usb_close(&r->ftdic) < 0)
		b3m_ftdi_error(r);

	// deinit
	ftdi_deinit(&r->ftdic);

	return 0;
}

/*-----------------------------------------------------------------------------
 * Write n bytes from the swap to the Kondo.
 * Returns >0 number of bytes written, < 0 if error
 */
int b3m_write(B3MData * r, int n)
{
	assert(r);
	int i;
	if ((i = ftdi_write_data(&r->ftdic, r->swap, n)) < 0)
		b3m_ftdi_error(r);
	return i;
}
/*-----------------------------------------------------------------------------
 * Read n bytes from B3M. Reads immediately from the serial buffer.
 * See b3m_read_timeout for a version that blocks waiting for the data.
 * Returns < 0: error
 * Returns >= 0: number of bytes read
 */
int b3m_read(B3MData * r, int n)
{
	assert(r);
	int i;
	if ((i = ftdi_write_data(&r->ftdic, r->swap, n)) < 0)
		b3m_ftdi_error(r);
	return i;
}
/*-----------------------------------------------------------------------------
 * Read n bytes from the B3M, waiting for at most usecs for the n bytes.
 * Performs this by continuously polling the serial buffer until either
 * all of the bytes are read or the timeout has been reached.
 * Returns < 0: error
 * Returns >= 0: number of bytes read
 */
int b3m_read_timeout(B3MData * r, int n, long usecs)
{
	assert(r);
	static struct timeval tv, end;
	int i = 0, bytes_read = 0;
	gettimeofday(&tv, NULL);
	// determine end time
	end.tv_sec = tv.tv_sec + usecs / 1000000;
	end.tv_usec = tv.tv_usec + usecs % 1000000;
	if (end.tv_usec > 1000000) {
		end.tv_sec += 1;
		end.tv_usec -= 1000000;
	}
	// spam the read until data arrives
	do {
	    if ((i = ftdi_read_data(&r->ftdic, r->swap, n - bytes_read)) < 0) {
		b3m_ftdi_error(r);
	    }
	    bytes_read += i;
	    gettimeofday(&tv, NULL);
	} while (bytes_read < n && (tv.tv_sec <= end.tv_sec || tv.tv_usec < end.tv_usec));
	return bytes_read;
}

/*-----------------------------------------------------------------------------
 * Purge the serial buffers.
 * Returns 0 if successful, < 0 if error
 */
int b3m_purge(B3MData * r)
{
	assert(r);
	if (ftdi_usb_purge_buffers(&r->ftdic) < 0)
		b3m_ftdi_error(r);
	return 0;
}

/*-----------------------------------------------------------------------------
 * Transaction template: Purge, then send out_bytes, then receive in_bytes
 * Blocks for B3M_RX_TIMEOUT microseconds (default value)
 * Returns < 0: error
 * Returns >= 0: number of bytes read
 */
int b3m_trx(B3MData * r, UINT bytes_out, UINT bytes_in)
{
	return b3m_trx_timeout(r, bytes_out, bytes_in, B3M_RX_TIMEOUT);
}

/*-----------------------------------------------------------------------------
 * Transaction template: Purge, then send out_bytes, then receive in_bytes
 * On RX, blocks for at most timeout microseconds before giving up.
 * Returns < 0: error
 * Returns >= 0: number of bytes read
 */
int b3m_trx_timeout(B3MData * r, UINT bytes_out, UINT bytes_in, long timeout)
{
	assert(r);
	int i;
	int j;

	if ((i = b3m_purge(r)) < 0)
		return i;
	if ((i = b3m_write(r, bytes_out)) < 0)
		return i;

	// debug printing
	if (r->debug) {
		printf("send %d bytes: ", i);
		for (j = 0; j < i; j++)
			printf("%x ", r->swap[j]);
		printf("\n");
	}

	// clear swap
	for (i = 0; i < bytes_in; i++)
		r->swap[i] = 0;

	// read the return data
	i = b3m_read_timeout(r, bytes_in, timeout);

	// debug printing
	if (r->debug) {
		printf("recv %d bytes: ", i);
		for (j = 0; j < i; j++)
			printf("%x ", r->swap[j]);
		printf("\n");
	}

	return i;
}

/*-----------------------------------------------------------------------------
 * Set servo position
 * id: the servo id, 0-31
 * pos: the position to set, 0-16383
 * to free the servo, pos = 0
 * to hold the servo, pos = 16383
 * Returns current position >= 0, or < 0 if error.
 */
int b3m_pos(B3MData * r, UINT id, UINT pos)
{
	assert(r);
	int i;
	int sum = 0, time = 0;

	// check for valid data
	if (id > 31)
		b3m_error(r, "Invalid servo ID > 31.");
	if (pos > 16383)
		b3m_error(r, "Invalid servo position > 16383.");

	// build command
	r->swap[0] = 0x09;						// length
	r->swap[1] = B3M_CMD_POSITION;			// command
	r->swap[2] = B3M_RETURN_MOTOR_STATUS;	// option
	r->swap[3] = id;						// id
	r->swap[4] = pos & 0xff;				// pos low
	r->swap[5] = pos >> 8;					// pos high
	r->swap[6] = time &0xff;				// time low
	r->swap[7] = time >> 8;					// time high
	for(i = 0, sum = 0; i < 8; i ++){
		sum += r->swap[i];
	}
	r->swap[8] = sum & 0xff;
	
	// synchronize with servo
	if ((i = b3m_trx_timeout(r, 9, 7, B3M_POS_TIMEOUT)) < 0)
		return i;

	// return the position
	return ((r->swap[5] & 0xFF) << 7) | (r->swap[4] & 0xFF);
}

/*-----------------------------------------------------------------------------
 * Hold servo position
 * This tells the servo to stop and maintain the current position.
 * id: the servo id, 0-31
 * Returns current position >= 0, or < 0 if error.
 */
int b3m_hold(B3MData * r, UINT id)
{
	return b3m_pos(r, id, 16383);
}

/*-----------------------------------------------------------------------------
 * Free servo position
 * This tells the servo to stop and relax (power off motor)
 * id: the servo id, 0-31
 * Returns the current position >= 0, or < 0 if error.
 */
int b3m_free(B3MData * r, UINT id)
{
	return b3m_pos(r, id, 0);
}

/*-----------------------------------------------------------------------------
 * Get servo stretch
 * id: the servo id, 0-31
 * Returns: Value of stretch (>= 0) if successful, < 0 if error
 */
int b3m_get_stretch(B3MData * r, UINT id)
{
	assert(r);
	int i;

	// check valid id
	if (id > 31)
		b3m_error(r, "Invalid servo ID > 31.");

	// build command
	r->swap[0] = id | B3M_CMD_GET; // id and command
	r->swap[1] = B3M_SC_STRETCH; // subcommand

	// synchronize
	if ((i = b3m_trx_timeout(r, 2, 5, B3M_GET_TIMEOUT)) < 0)
		return i;

	// return stretch
	return r->swap[4];
}

/*-----------------------------------------------------------------------------
 * Get servo speed
 * id: the servo id, 0-31
 * Returns: Value of speed (>= 0) if successful, < 0 if error
 */
int b3m_get_speed(B3MData * r, UINT id)
{
	assert(r);
	int i;

	// check valid id
	if (id > 31)
		b3m_error(r, "Invalid servo ID > 31.");

	// build command
	r->swap[0] = id | B3M_CMD_GET; // id and command
	r->swap[1] = B3M_SC_SPEED; // subcommand

	// synchronize
	if ((i = b3m_trx_timeout(r, 2, 5, B3M_GET_TIMEOUT)) < 0)
		return i;

	// return stretch
	return r->swap[4];
}

/*-----------------------------------------------------------------------------
 * Get servo current
 * id: the servo id, 0-31
 * Returns: Value of current (>= 0) if successful, < 0 if error
 */
int b3m_get_current(B3MData * r, UINT id)
{
	assert(r);
	int i;

	// check valid id
	if (id > 31)
		b3m_error(r, "Invalid servo ID > 31.");

	// build command
	r->swap[0] = id | B3M_CMD_GET; // id and command
	r->swap[1] = B3M_SC_CURRENT; // subcommand

	// synchronize
	if ((i = b3m_trx_timeout(r, 2, 5, B3M_GET_TIMEOUT)) < 0)
		return i;

	// return stretch
	return r->swap[4];
}

/*-----------------------------------------------------------------------------
 * Set stretch parameter of the servo
 * id: the id of the servo 0-31
 * stretch: the desired stretch 1(2)-127(254)
 * Returns: the stretch as reported by the servo (>= 0), or < 0 if error
 */
int b3m_set_stretch(B3MData * r, UINT id, UCHAR stretch)
{
	assert(r);
	int i;

	// check valid stuff
	if (id > 31)
		b3m_error(r, "Invalid servo ID > 31.");
	if (stretch < 1 || stretch > 127)
		b3m_error(r, "Invalid stretch, not [1-127]");

	// build command
	r->swap[0] = id | B3M_CMD_SET; // id and command
	r->swap[1] = B3M_SC_STRETCH; // subcommand
	r->swap[2] = stretch;

	// synchronize
	if ((i = b3m_trx_timeout(r, 3, 6, B3M_SET_TIMEOUT)) < 0)
		return i;

	return r->swap[5];
}

/*-----------------------------------------------------------------------------
 * Set speed parameter of the servo
 * id: the id of the servo 0-31
 * stretch: the desired speed 1(2)-127(254)
 * Returns: the speed as reported by the servo (>= 0), or < 0 if error
 */
int b3m_set_speed(B3MData * r, UINT id, UCHAR speed)
{
	assert(r);
	int i;

	// check valid stuff
	if (id > 31)
		b3m_error(r, "Invalid servo ID > 31.");
	if (speed < 1 || speed > 127)
		b3m_error(r, "Invalid speed, not [1-127]");

	// build command
	r->swap[0] = id | B3M_CMD_SET; // id and command
	r->swap[1] = B3M_SC_SPEED; // subcommand
	r->swap[2] = speed;

	// synchronize
	if ((i = b3m_trx_timeout(r, 3, 6, B3M_SET_TIMEOUT)) < 0)
		return i;

	return r->swap[5];
}
/*-----------------------------------------------------------------------------
 * Set current limit parameter of the servo
 * id: the id of the servo 0-31
 * curlim: the desired current limit 1-63
 * Returns: the current limit as reported by the servo (>= 0), or < 0 if error
 */
int b3m_set_current_limit(B3MData * r, UINT id, UCHAR curlim)
{
	assert(r);
	int i;

	// check valid stuff
	if (id > 31)
		b3m_error(r, "Invalid servo ID > 31.");
	if (curlim < 1 || curlim > 63)
		b3m_error(r, "Invalid current, not [1-63]");

	// build command
	r->swap[0] = id | B3M_CMD_SET; // id and command
	r->swap[1] = B3M_SC_CURRENT; // subcommand
	r->swap[2] = curlim;

	// synchronize
	if ((i = b3m_trx_timeout(r, 3, 6, B3M_SET_TIMEOUT)) < 0)
		return i;

	return r->swap[5];
}

/*-----------------------------------------------------------------------------
 * Set temperature limit parameter of the servo
 * id: the id of the servo 0-31
 * templim: the desired current limit 1-127
 * Returns: the temperature limit as reported by the servo (>= 0), or < 0 if error
 */
int b3m_set_temperature_limit(B3MData * r, UINT id, UCHAR templim)
{
	assert(r);
	int i;

	// check valid stuff
	if (id > 31)
		b3m_error(r, "Invalid servo ID > 31.");
	if (templim < 1 || templim > 127)
		b3m_error(r, "Invalid temperature, not [1-127]");

	// build command
	r->swap[0] = id | B3M_CMD_SET; // id and command
	r->swap[1] = B3M_SC_TEMPERATURE; // subcommand
	r->swap[2] = templim;

	// synchronize
	if ((i = b3m_trx_timeout(r, 3, 6, B3M_SET_TIMEOUT)) < 0)
		return i;

	return r->swap[5];
}

/*-----------------------------------------------------------------------------
 * Get the ID of the connected servo
 * This command should be used with only one servo attached to the bus.
 * Returns: ID (0-31), or < 0 if error
 */
int b3m_get_id(B3MData * r)
{
	assert(r);
	int i;

	// build command
	r->swap[0] = 0xFF; // command (0xFF for read)
	r->swap[1] = B3M_SC_READ; // subcommand (read)
	r->swap[2] = B3M_SC_READ; // subcommand (read)
	r->swap[3] = B3M_SC_READ; // subcommand (read)

	// synchronize
	if ((i = b3m_trx_timeout(r, 4, 5, B3M_ID_TIMEOUT)) < 0)
		return i;

	// return the ID
	return r->swap[4] & 0x1F;
}

/*-----------------------------------------------------------------------------
 * Set the ID of the connected servo
 * This command should be used with only one servo attached to the bus.
 * id: the desired ID of the servo 0-31
 * Returns: ID (0-31), or < 0 if error.
 */
int b3m_set_id(B3MData * r, UINT id)
{
	assert(r);
	int i;

	// check id
	if (id > 31)
	    b3m_error(r, "Invalid servo ID, > 31.");

	// build command
	r->swap[0] = id | B3M_CMD_ID;
	r->swap[1] = B3M_SC_WRITE;
	r->swap[2] = B3M_SC_WRITE;
	r->swap[3] = B3M_SC_WRITE;
	if (r->debug) {
	    int i;
	    for (i=0; i<4; i++) {
		printf("%x ", r->swap[i]);
	    }
	    printf("\n");
	}
	// This seems bug. set_id must wait some time before read.
#if 0	
	// synchronize
	if ((i = b3m_trx_timeout(r, 4, 5, B3M_ID_TIMEOUT)) < 0)
		return i;
#endif
	if ((i=b3m_write (r, 4)) < 0) {
	    return -1;
	}
	sleep (3);
	if ((i=b3m_read(r, 5)) < 0) {
	    return -1;
	}	    

	// return the ID
	return r->swap[4] & 0x1F;
}