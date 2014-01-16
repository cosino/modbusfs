/*
 * Modbusfs header file
 *
 * Copyright (C) 2013-2014	Rodolfo Giometti <giometti@linux.it>
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

#ifndef _MODBUSFS_H
#define _MODBUSFS_H

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <modbus.h>

/*
 * Misc macros
 */

#define NAME                    program_invocation_short_name

#define __to_str(s)	#s
#define to_str(s)	__to_str(s)

#define info(fmt, args...)                                            \
                printf(fmt "\n" , ## args)

#define warn(fmt, args...)                                            \
                printf(fmt "\n" , ## args)

#define err(fmt, args...)                                             \
                fprintf(stderr, fmt "\n" , ## args)

#define dbg(fmt, args...)                                             \
        do {                                                          \
                if (unlikely(enable_debug))                           \
                        fprintf(stderr, "*** %s[%4d]: %s: " fmt "\n" ,\
                                __FILE__, __LINE__, __func__ , ## args);        \
        } while (0)

#define __deprecated            __attribute__ ((deprecated))
#define __packed                __attribute__ ((packed))
#define __constructor           __attribute__ ((constructor))
#define __printf_like(a, b)     __attribute__ ((format (printf, (a), (b))))

#define module_init(f)    \
                int __attribute__ ((weak, alias (#f))) probe(void)

#define min(x, y) ({                                    \
                typeof(x) _min1 = (x);                  \
                typeof(y) _min2 = (y);                  \
                (void) (&_min1 == &_min2);              \
                _min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({                                    \
                typeof(x) _max1 = (x);                  \
                typeof(y) _max2 = (y);                  \
                (void) (&_max1 == &_max2);              \
                _max1 > _max2 ? _max1 : _max2; })

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member)                                 \
        ({                                                              \
                const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
                (type *)( (char *)__mptr - offsetof(type,member) );     \
        })

#define BUILD_BUG_ON_ZERO(e)                                            \
                (sizeof(char[1 - 2 * !!(e)]) - 1)
#define __must_be_array(a)                                              \
                BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(typeof(a), \
                                                        typeof(&a[0])))
#define ARRAY_SIZE(arr)                                                 \
                (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#define unlikely(x)          __builtin_expect(!!(x), 0)
#define BUG()                                                           \
        do {                                                            \
                err("fatal error in %s():%d", __func__, __LINE__);      \
                exit(EXIT_FAILURE);                                     \
        } while (0)
#define EXIT_ON(condition)                                              \
        do {                                                            \
                if (unlikely(condition))                                \
                        BUG();                                          \
        } while(0)
#define BUG_ON(condition)       EXIT_ON(condition)

#define WARN()                                                          \
        do {                                                            \
                err("warning notice in %s():%d", __func__, __LINE__);   \
        } while (0)
#define WARN_ON(condition)                                              \
        do {                                                            \
                if (unlikely(condition))                                \
                        WARN();                                         \
        } while(0)

/*
 * Global types
 */

enum modbus_type_e {
	RTU,
	__TYPE_ERROR
};

#define SERIAL_DEV_MAX		32
struct modbus_parms_s {
	struct modbus_rtu_parms_s {
		char serial_dev[SERIAL_DEV_MAX + 1];
		int baud;
		int bytes;
		char parity;
		int stop;
	} rtu;
};

/* Per register data */
#define MODE_REG_MASK	(S_IRUSR | S_IWUSR |		\
			 S_IRGRP | S_IWGRP |		\
			 S_IROTH | S_IWOTH)

struct modbusfs_register_s {
	int idx;
	unsigned int mode;

	struct modbusfs_client_s *cli;
};

/* Per client data */
#define MODE_CLI_MASK	(S_IRUSR | S_IWUSR | S_IXUSR |	\
			 S_IRGRP | S_IWGRP | S_IXGRP |	\
			 S_IROTH | S_IWOTH | S_IXOTH)

struct modbusfs_client_s {
	int addr;
	unsigned int mode;

	struct modbusfs_register_s *regs;
	int regs_num;
};

/* Per file data */
enum control_file_e {
	CTRL_NONE,
	CTRL_EXPORTS
};

struct modbusfs_data_s {
	struct modbusfs_client_s *cli;
	struct modbusfs_register_s *reg;
	enum control_file_e ctrl_file;
};

/*
 * Exported variables & functions
 */

extern int enable_debug;

extern int modbusfs_start(struct fuse_args args,
			  enum modbus_type_e modbus_type,
			  struct modbus_parms_s modbus_parms);

#endif /* _MODBUSFS_H */
