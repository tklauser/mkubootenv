/*
 * mkubootenv.c
 *
 * Create a U-Boot environment image suitable for flashing.
 *
 * The input is a text file containing environment variable
 * definitions in the following format (variables separated by
 * newline):
 *
 * baudrate=115200
 * bootdelay=5
 * ethaddr=00:15:12:00:00:01
 * kernel_addr=4050000
 * boot_ram=bootm ${kernel_addr}
 * bootcmd=run boot_ram
 *
 * Copyright (c) 2009, Zurich University of Applied Science
 * Copyright (c) 2009, Tobias Klauser <klto@zhaw.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "crc32.h"

#define DEBUG

#define CMD_NAME		"mkubootenv"

#define CRC32_SIZE		sizeof(uint32_t)
/* minimum trailing null bytes */
#define TRAILER_SIZE		2

#define err(fmt, args...)	fprintf(stderr, "%s: Error: " fmt, CMD_NAME, ##args)
#define warn(fmt, args...)	fprintf(stderr, "%s: Warning: " fmt, CMD_NAME, ##args)
#ifdef DEBUG
# define dbg(fmt, args...)	fprintf(stdout, fmt, ##args)
#else
# define dbg(fmt, args...)
#endif

/* file "object" */
struct file {
	const char *name;
	int fd;
	uint8_t *ptr;
	size_t size;
};

static void usage_and_exit(int status) __attribute__((noreturn));

static inline void uboot_env_init_file(struct file *f)
{
	memset(f, 0, sizeof(struct file));
	f->ptr = MAP_FAILED;
}

static int uboot_env_prepare_source(struct file *s)
{
	struct stat sbuf;

	s->fd = open(s->name, O_RDONLY);
	if (s->fd < 0) {
		err("Can't open source file '%s': %s\n", s->name,
				strerror(errno));
		return EXIT_FAILURE;
	}

	if (fstat(s->fd, &sbuf) < 0) {
		err("Can't stat source file '%s': %s\n", s->name,
				strerror(errno));
		close(s->fd);
		return -1;
	}

	s->ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, s->fd, 0);
	if (s->ptr == MAP_FAILED) {
		err("Can't mmap source image file '%s': %s\n", s->name,
				strerror(errno));
		close(s->fd);
		return -1;
	}

	s->size = sbuf.st_size;

	return 0;
}

static int uboot_env_prepare_target(struct file *t)
{
	t->fd = open(t->name, O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (t->fd < 0) {
		err("Can't open target image file '%s': %s\n", t->name,
				strerror(errno));
		return -1;
	}

	/*
	 * seek to the end of the target file to write a byte, so we have the
	 * whole target image size mapped for writing
	 */
	if (lseek(t->fd, t->size - 1, SEEK_SET) < 0) {
		err("Can't seek in target image file '%s': %s\n", t->name,
				strerror(errno));
		close(t->fd);
		return -1;
	}

	if (write(t->fd, "", 1) < 0) {
		err("Can't write to target image file '%s': %s\n", t->name,
				strerror(errno));
		close(t->fd);
		return -1;
	}

	t->ptr = mmap(NULL, t->size, PROT_READ|PROT_WRITE, MAP_SHARED, t->fd, 0);
	if (t->ptr == MAP_FAILED) {
		err("Can't mmap target image file '%s': %s\n", t->name,
				strerror(errno));
		close(t->fd);
		return -1;
	}

	return 0;
}

static void uboot_env_cleanup_file(struct file *f)
{
	if (f->ptr != MAP_FAILED)
		munmap(f->ptr, f->size);
	if (f->fd > 0)
		close(f->fd);
}

static void uboot_env_to_img(struct file *s, struct file *t)
{
	uint8_t *p, *q, *end;
	uint32_t *crc;

	dbg("source file (env):       %s\n", s->name);
	dbg("target image file (bin): %s\n", t->name);
	dbg("target size:             %zd\n", t->size);

	/* CRC32 placeholder, will be filled later */
	dbg("writing crc dummy...\n");
	end = t->ptr + CRC32_SIZE;
	for (p = t->ptr; p < end; p++)
		*p = 0;

	/* copy the source file, replacing \n by \0 */
	dbg("writing data...\n");
	end = s->ptr + s->size;
	for (q = s->ptr; q < end; q++, p++)
		*p = (*q == '\n') ? '\0' : *q;

	/* trailer */
	dbg("writing trailer...\n");
	end = s->ptr + t->size;
	for (q = s->ptr + s->size; q < end; q++)
		*p = 0;

	/* now for the real CRC32 */
	dbg("calculating crc...\n");
	crc = (uint32_t *) t->ptr;
	*crc = crc32(0, t->ptr + CRC32_SIZE, t->size - CRC32_SIZE);
	dbg("crc: %08x\n", *crc);
}

static void uboot_img_to_env(struct file *s, struct file *t)
{
	uint8_t *p, *q, *end;
	uint32_t *crc;

	dbg("source file (bin):       %s\n", s->name);
	dbg("target image file (env): %s\n", t->name);
	dbg("target size:             %zd\n", t->size);

	/* check CRC */
	crc = (uint32_t *) s->ptr;
	if (*crc != crc32(0, s->ptr + CRC32_SIZE, s->size - CRC32_SIZE))
		warn("source image with bad CRC\n");

	p = t->ptr;
	end = s->ptr + CRC32_SIZE + t->size;
	for (q = s->ptr + CRC32_SIZE; q < end; p++, q++)
		*p = (*q == '\0') ? '\n' : *q;
}

static void usage_and_exit(int status)
{
	printf("usage: mkenv [-t <type>] [-s <size>] <source file> <target file>\n"
	       "  -s <size>  set size of the target image file to <size> bytes. If <size> is\n"
	       "             bigger than the source file, the target image gets padded with null\n"
	       "             bytes. If <size> is smaller than the source file, an error is emitted.\n"
	       "  -r         reverse operation: get plaintext env file (target) from binary image\n"
	       "             file (source)\n");
	exit(status);
}

int main(int argc, char **argv)
{
	int i;
	int status = EXIT_FAILURE;
	ssize_t img_size = -1;
	bool reverse = false;
	struct file s, t;	/* source and target file */

	if (argc < 2)
		usage_and_exit(EXIT_FAILURE);

	/* parse commandline options */
	for (i = 1; (i + 1 < argc) && (argv[i][0] == '-'); i++) {
		switch (argv[i][1]) {
		case 's':
			img_size = strtol(argv[++i], NULL, 10);
			if (img_size <= 0) {
				err("Invalid target image size: %zd. Must be greater than 0.\n", img_size);
				exit(EXIT_FAILURE);
			}
			break;
		case 'r':
			reverse = true;
			break;
		case 'h':
			status = EXIT_SUCCESS;
			/* fall through */
		default:
			usage_and_exit(status);
			break;
		}
	}

	/* we expect two filenames */
	if (i + 2 > argc)
		usage_and_exit(EXIT_FAILURE);

	if (reverse && img_size >= 0)
		warn("Image size specified in reverse mode will be ignored\n");

	uboot_env_init_file(&s);
	uboot_env_init_file(&t);
	s.name = argv[i];
	t.name = argv[i+1];

	if (uboot_env_prepare_source(&s))
		exit(EXIT_FAILURE);

	if (!reverse) {
		ssize_t min_img_size = CRC32_SIZE + s.size + TRAILER_SIZE;

		/* TODO: Check source file format:
		 *	var=value\n
		 */

		/*
		 * check whether the size hasn't been set or whether the source file +
		 * CRC + trailer fits into the specified size.
		 */
		if (img_size < 0)
			img_size = min_img_size;
		else if (img_size < min_img_size) {
			err("Specified size (%zd) is too small for the source file to "
					"fit into. Must be at least %zd bytes.\n",
					img_size, min_img_size);
			goto cleanup_source;
		}

		t.size = img_size;
		if (uboot_env_prepare_target(&t))
			goto cleanup_source;

		uboot_env_to_img(&s, &t);
	} else {
		uint8_t *p;
		uint8_t *end;
		bool found_data_end = false;

		/* get the length of the data part */
		end = s.ptr + CRC32_SIZE + s.size;
		for (p = s.ptr + CRC32_SIZE; (p < end - 1) && (!found_data_end); p++) {
			/* two null bytes mark the end of the data section */
			if (*p == '\0' && *(p + 1) == '\0')
				found_data_end = true;
		}

		/* calculate the plain text file size */
		t.size = p - (s.ptr + CRC32_SIZE);
		if (uboot_env_prepare_target(&t))
			goto cleanup_source;

		uboot_img_to_env(&s, &t);
	}

	status = EXIT_SUCCESS;

	uboot_env_cleanup_file(&t);
cleanup_source:
	uboot_env_cleanup_file(&s);

	exit(status);
}
