/*
 * Modbusfs methods
 *
 * Copyright (C) 2013-2014      Rodolfo Giometti <giometti@linux.it>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <getopt.h>

#include "modbusfs.h"

int enable_debug;

static enum modbus_type_e modbus_type = RTU;
static struct modbus_parms_s modbus_parms = {
	/* RTU */ {"/dev/ttyUSB0", 115200, 8, 'N', 1}
};

/*
 * Local functions
 */

static int parse_rtu_opts(char *opts)
{
	int ret;

	if (strlen(opts) == 0)
		return 0;	/* use defaults */

	ret = sscanf(opts, "%" to_str(SERIAL_DEV_MAX) "[a-zA-Z0-9/_],%d,%d%c%d",
		     modbus_parms.rtu.serial_dev,
		     &modbus_parms.rtu.baud,
		     &modbus_parms.rtu.bytes,
		     &modbus_parms.rtu.parity, &modbus_parms.rtu.stop);
	if (ret < 1)
		return -1;

	dbg("serial_dev=%s baud=%d bytes=%d parity=%c stop=%d",
				     modbus_parms.rtu.serial_dev,
				     modbus_parms.rtu.baud,
				     modbus_parms.rtu.bytes,
				     modbus_parms.rtu.parity,
				     modbus_parms.rtu.stop);

	return 0;
}

/*
 * Main
 */

static void usage(void)
{
	fprintf(stderr, "usage: %s [<dev>] <mountpoint> [options]\n", NAME);
	fprintf(stderr, "where <dev> can be:\n\n"
		"\trtu:[<ttydev>[,<baud>[,<bits><parity><stop>]]]\n");
	fprintf(stderr, "\n");
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	int i;
	char *ptr;
	int ret;

	/*
	 * Check command line
	 */

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			usage();
			fuse_opt_add_arg(&args, "-ho");
			continue;
		}

		ptr = index(argv[i], ':');
		if (ptr && strncmp(argv[i], "rtu", sizeof("rtu") - 1) == 0) {
			modbus_type = RTU;

			ret = parse_rtu_opts(ptr + 1);
			if (ret < 0) {
				err("invalid RTU options");
				exit(EXIT_FAILURE);
			}

			continue;
		}

		if (strcmp(argv[i], "-d") == 0 ||
		    strcmp(argv[i], "--debug") == 0) {
			enable_debug++;

			/* Check debug level */
			if (enable_debug < 2)
				continue;	/* no fuse debug messages */
		}

		fuse_opt_add_arg(&args, argv[i]);
	}

	/*
	 * Do the job
	 */

	return modbusfs_start(args, modbus_type, modbus_parms);
}
