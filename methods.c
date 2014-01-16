/*
 * Modbusfs
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

#include "modbusfs.h"

static modbus_t *ctx;
static pthread_mutex_t ctx_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct modbusfs_client_s *clients;
static int clients_num;

/*
 * Local functions
 */

static int parse_path(char *path, char ***elem, size_t *num)
{
	char delimiter[] = "/";
	char *tok, *env;

	*elem = NULL;
	*num = 0;

	tok = strtok_r(path, delimiter, &env);
	while (tok) {
		*elem = realloc(*elem, sizeof(char *) * (*num + 1));
		EXIT_ON(!*elem);

		(*elem)[*num] = tok;
		(*num)++;

		tok = strtok_r(NULL, delimiter, &env);
	}

	return 0;
}

/* TODO: we should check group & other permissions too */
static int have_permissions(int flags, int mode)
{
	flags &= O_ACCMODE;

	if (flags == O_RDONLY && !(mode & S_IRUSR))
		return 0;
	if (flags == O_WRONLY && !(mode & S_IWUSR))
		return 0;

	return 1;	/* ok */
}

static int read_register(modbus_t * ctx, int addr, int idx, uint16_t * dest)
{
	int ret;

	EXIT_ON(pthread_mutex_lock(&ctx_mutex));

	ret = modbus_set_slave(ctx, addr);
	if (ret == -1)
		goto unlock;

	ret = modbus_read_registers(ctx, idx, 1, dest);

unlock:
	EXIT_ON(pthread_mutex_unlock(&ctx_mutex));

	return ret;
}

static int write_register(modbus_t * ctx, int addr, int idx, int value)
{
	int ret;

	EXIT_ON(pthread_mutex_lock(&ctx_mutex));

	ret = modbus_set_slave(ctx, addr);
	if (ret == -1)
		goto unlock;

	ret = modbus_write_register(ctx, idx, value);

unlock:
	EXIT_ON(pthread_mutex_unlock(&ctx_mutex));

	return ret;
}

static int add_client(uint8_t addr, unsigned int mode)
{
	struct modbusfs_client_s *ptr;

	ptr = realloc(clients,
		      sizeof(struct modbusfs_client_s) * (clients_num + 1));
	if (!ptr)
		return -ENOMEM;
	ptr[clients_num].addr = addr;
	ptr[clients_num].mode = mode;
	ptr[clients_num].regs = NULL;
	ptr[clients_num].regs_num = 0;

	clients = ptr;

	return ++clients_num;
}

static struct modbusfs_client_s *find_client(uint8_t addr) {
	int c;

        for (c = 0; c < clients_num; c++)
		if (clients[c].addr == addr)
			break;
	if (c == clients_num)
		return NULL;

	return &clients[c];
}

static int add_reg(struct modbusfs_client_s *cli, int idx, unsigned int mode)
{
	struct modbusfs_register_s *ptr;

	ptr = realloc(cli->regs,
		      sizeof(struct modbusfs_register_s) * (cli->regs_num + 1));
	if (!ptr)
		return -ENOMEM;
	ptr[cli->regs_num].idx = idx;
	ptr[cli->regs_num].mode = mode;
	ptr[cli->regs_num].cli = cli;

	cli->regs = ptr;

	return cli->regs_num++;
}

static struct modbusfs_register_s *find_register(struct modbusfs_client_s *cli,
					int idx)
{
	int r;

	for (r = 0; r < cli->regs_num; r++)
		if (cli->regs[r].idx == idx)
			break;
	if (r == cli->regs_num)
		return NULL;

	return &cli->regs[r];
}

static modbus_t *client_connect(enum modbus_type_e modbus_type,
				       struct modbus_parms_s modbus_parms)
{
	modbus_t *ctx;
	int ret;

	switch (modbus_type) {
	case RTU:
		dbg("RTU on %s at %d %d%c%d",
				     modbus_parms.rtu.serial_dev,
				     modbus_parms.rtu.baud,
				     modbus_parms.rtu.bytes,
				     modbus_parms.rtu.parity,
				     modbus_parms.rtu.stop);
		ctx = modbus_new_rtu(modbus_parms.rtu.serial_dev,
				     modbus_parms.rtu.baud,
				     modbus_parms.rtu.parity,
				     modbus_parms.rtu.bytes,
				     modbus_parms.rtu.stop);
		break;

	default:
		BUG();
	}
	if (!ctx) {
		err("MODBUS init error: %s", modbus_strerror(errno));
		goto exit;
	}

	ret = modbus_connect(ctx);
	if (ret == -1) {
		err("MODBUS connect error: %s", modbus_strerror(errno));
		goto free;
	}

	return ctx;

free:
	modbus_free(ctx);
exit:
	return NULL;
}

/*
 * FUSER methods
 */

static int modbusfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			    off_t offset, struct fuse_file_info *fi)
{
	char **elem;
	size_t num;
	int i, c, addr, idx;
	char name[64];
	int ret;
	int res = 0;

	dbg("path=%s", path);
	parse_path(strdupa(path), &elem, &num);

	switch (num) {
	case 0 :	/* / */
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);

		/* The control files */
		filler(buf, "exports", NULL, 0);

		/* List all clients registers */
		for (c = 0; c < clients_num; c++) {
			addr = clients[c].addr;

			sprintf(name, "%d", addr);
			filler(buf, name, NULL, 0);
		}

		break;

	case 1 :	/* /<addr> */
		ret = sscanf(elem[0], "%d", &addr);
		if (ret != 1) {
			res = -ENOENT;
			goto exit;
		}

		for (c = 0; c < clients_num; c++)
			if (clients[c].addr == addr)
				break;
		if (c == clients_num) {
			res = -ENOENT;
			goto exit;
		}
		dbg("addr=%d", addr);

		/* The control files */
		filler(buf, "exports", NULL, 0);

		/* List all clients */
		for (i = 0; i < clients[c].regs_num; i++) {
			idx = clients[c].regs[i].idx;

			sprintf(name, "%d", idx);
			filler(buf, name, NULL, 0);
		}

		break;

	default :
		res = -ENOENT;
	}

exit:
	free(elem);
	return res;
}

static int modbusfs_getattr(const char *path, struct stat *stbuf)
{
	char **elem;
	size_t num;
	int addr, idx;
	struct modbusfs_client_s *cli;
	struct modbusfs_register_s *reg;
	int ret;
	int res = 0;

	dbg("path=%s", path);
	parse_path(strdupa(path), &elem, &num);

	memset(stbuf, 0, sizeof(struct stat));
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();

	switch (num) {
	case 0 :	/* / */
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;

		break;

	case 1 :	/* /<addr|ctrl> */
		ret = sscanf(elem[0], "%d", &addr);
		if (ret == 1) {	/* it's a client address! */
			dbg("addr=%d", addr);
			cli = find_client(addr);
			if (!cli) {
				res = -ENOENT;
				goto exit;
			}

			stbuf->st_mode = S_IFDIR | cli->mode;
			stbuf->st_nlink = 2;
		} else if (strcmp(elem[0], "exports") == 0) {
			stbuf->st_mode = S_IFREG | S_IWUSR;
			stbuf->st_nlink = 1;
			stbuf->st_size = 0;	/* write only! */
		} else
			res = -ENOENT;

		break;

	case 2 :	/* /<addr>/<reg|ctrl> */
		ret = sscanf(elem[0], "%d", &addr);
		if (ret != 1) {
			res = -ENOENT;
			goto exit;
		}
		dbg("addr=%d", addr);

		ret = sscanf(elem[1], "%d", &idx);
		if (ret == 1) {	/* it's a register! */
			dbg("addr=%d idx=%d", addr, idx);

			cli = find_client(addr);
			if (!cli) {
				res = -ENOENT;
				goto exit;
			}

			reg = find_register(cli, idx);
			if (!reg) {
				res = -ENOENT;
				goto exit;
			}

			stbuf->st_mode = S_IFREG | reg->mode;
			stbuf->st_nlink = 1;
			stbuf->st_size = 4;	/* all register are uint16_t! (0xHHHH) */
		} else if (strcmp(elem[1], "exports") == 0) {
			stbuf->st_mode = S_IFREG | S_IWUSR;
			stbuf->st_nlink = 1;
			stbuf->st_size = 0;	/* write only! */
		} else
			res = -ENOENT;

		break;

        default :
                res = -ENOENT;
	}

exit:
	free(elem);
	return res;
}

static int modbusfs_truncate(const char *path, off_t size)
{
	/* MODBUS files cannot be truncated!!! */
	return 0;
}

static int modbusfs_read(const char *path, char *buf,
			 size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct modbusfs_data_s *data = (struct modbusfs_data_s *)fi->fh;
	int addr, idx;
	int ret;
	uint16_t val;

	if (size < 4)
		return -EIO;

	dbg("path=%s", path);

        if (data->cli && data->reg) {           /* Is it a client register? */
                addr = data->cli->addr;
                idx = data->reg->idx;
		dbg("addr=%d idx=%d", addr, idx);

		/* Read register content only at first read! */
		if (offset == 0) {
			ret = read_register(ctx, addr, idx, &val);
			if (ret == -1)
				return -EIO;

			return sprintf(buf, "%x", val);
		} else
			return 0;
	} else if (data->cli && !data->reg) {   /* Is it a client ctrl file? */
                if (data->ctrl_file == CTRL_EXPORTS) {
                        addr = data->cli->addr;
			dbg("addr=%d exports", addr);

			return -EACCES;
		} else
                	BUG();
	} else if (!data->cli) {		/* Is it a global ctrl file? */
                if (data->ctrl_file == CTRL_EXPORTS) {
			dbg("exports");

			return -EACCES;
		} else
                	BUG();
	}

	BUG();
}

static int modbusfs_write(const char *path, const char *buf,
			  size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct modbusfs_data_s *data = (struct modbusfs_data_s *)fi->fh;
	int addr, idx;
	unsigned int mode;
	struct modbusfs_client_s *cli;
	struct modbusfs_register_s *reg;
	int ret;
	int val;

	dbg("path=%s", path);

	if (data->cli && data->reg) {		/* Is it a client register? */
               	addr = data->cli->addr;
               	idx = data->reg->idx;
		dbg("addr=%d idx=%d", addr, idx);

		/* Read user data */
		ret = sscanf(buf, "%x", &val);
		if (ret != 1 || val < 0 || val > 0xffff)
			return -ENOENT;
		dbg("val=%x", val);

		/* Write register content */
		ret = write_register(ctx, addr, idx, val);
		if (ret == -1)
			return -EIO;

	        return size;
	} else if (data->cli && !data->reg) { 	/* Is it a client ctrl file? */
                if (data->ctrl_file == CTRL_EXPORTS) {
                	addr = data->cli->addr;
			dbg("addr=%d exports", addr);

			/* Read user data */
			ret = sscanf(buf, "%d %o", &idx, &mode);
			if (ret != 2)
				return -EINVAL;
			dbg("idx=%d mode=%o", idx, mode);

			/* Check user input */
			if (idx < 0 || idx > 0xffff)
				return -EINVAL;
			if ((mode & MODE_REG_MASK) != mode)
				return -EINVAL;

                        cli = find_client(addr);
                        if (!cli)
                                return ENOENT;

                        reg = find_register(cli, idx);
                        if (reg)
                                return -EEXIST;

			ret = add_reg(cli, idx, mode);
			if (ret < 0)
				return ret;

		        return size;
		} else
                	BUG();
	} else if (!data->cli) {		/* Is it a global ctrl file? */
                if (data->ctrl_file == CTRL_EXPORTS) {
			dbg("exports");

			/* Read user data */
			ret = sscanf(buf, "%d %o", &addr, &mode);
			if (ret != 2)
				return -EINVAL;

			/* Check user input */
			if (addr < 1 || addr > 254)
				return -EINVAL;
                        if ((mode & MODE_CLI_MASK) != mode)
                                return -EINVAL;

                        cli = find_client(addr);
                        if (cli)
                                return -EEXIST;

			ret = add_client(addr, mode);
			if (ret < 0)
				return ret;

		        return size;
                } else
                        BUG();
	}

	BUG();
}

static int modbusfs_open(const char *path, struct fuse_file_info *fi)
{
	char **elem;
	size_t num;
	int addr, idx;
	struct modbusfs_data_s *data = NULL;
	struct modbusfs_client_s *cli;
	struct modbusfs_register_s *reg;
	int ret;
	int res;

	dbg("path=%s", path);
	parse_path(strdupa(path), &elem, &num);

	/* Allocate per file data */
        data = malloc(sizeof(struct modbusfs_data_s));
        if (!data) {
        	res = -ENOMEM;
		goto error;
	}
	data->cli = NULL;
	data->reg = NULL;
	data->ctrl_file = CTRL_NONE;

	switch (num) {
	case 0 :	/* / */
		BUG();

	case 1 :	/* /<ctrl> */
		if (strcmp(elem[0], "exports") == 0) {
			if ((fi->flags & O_ACCMODE) != O_WRONLY) {
				res = -EACCES;
				goto error;
			}
			data->ctrl_file = CTRL_EXPORTS;
		} else
			BUG();

		break;

	case 2 :	/* /<addr>/<reg|ctrl> */
                ret = sscanf(elem[0], "%d", &addr);
                if (ret != 1) {
                        res = -ENOENT;
                        goto error;
                }
                dbg("addr=%d", addr);

                cli = find_client(addr);
                if (!cli) {
                        res = -ENOENT;
                        goto error;
                }

		data->cli = cli;

                ret = sscanf(elem[1], "%d", &idx);
                if (ret == 1) { /* it's a register! */
                        dbg("idx=%d", idx);

                        reg = find_register(cli, idx);
                        if (!reg) {
                                res = -ENOENT;
                                goto error;
                        }

			if (!have_permissions(fi->flags, reg->mode)) {
                                res = -EACCES;
                                goto error;
                        }

			data->reg = reg;
		} else if (strcmp(elem[1], "exports") == 0) {
                        if ((fi->flags & O_ACCMODE) != O_WRONLY) {
                                res = -EACCES;
                                goto exit;
                        }

			data->ctrl_file = CTRL_EXPORTS;
		} else
			BUG();

		break;

        default :
                res = -ENOENT;
		goto error;
	}

	fi->fh = (unsigned long) data;
	fi->direct_io = 1;
	fi->nonseekable = 1;

exit:
        free(elem);
        return 0;

error:
	if (data)
		free(data);
	free(elem);
	return res;
}

static int modbusfs_release(const char *path, struct fuse_file_info *fi)
{
	struct modbusfs_data_s *data = (struct modbusfs_data_s *)fi->fh;

	if (data)
		free(data);

	return 0;
}

static struct fuse_operations modbusfs_oper = {
	.readdir	= modbusfs_readdir,
	.getattr	= modbusfs_getattr,
	.truncate	= modbusfs_truncate,
	.read		= modbusfs_read,
	.write		= modbusfs_write,
	.open		= modbusfs_open,
	.release	= modbusfs_release,
};

int modbusfs_start(struct fuse_args args,
		   enum modbus_type_e modbus_type,
		   struct modbus_parms_s modbus_parms)
{
	/*
	 * Connect to the MODBUS devices
	 */

	ctx = client_connect(modbus_type, modbus_parms);
	if (!ctx) {
		err("modbus connection failed: %s", modbus_strerror(errno));
		return -1;
	}

	/*
	 * Start FUSE
	 */

	return fuse_main(args.argc, args.argv, &modbusfs_oper, NULL);
}
