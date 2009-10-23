/*
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

#define CMD_NAME		"mkenv"
#define DEFAULT_IMG_TYPE	"binary"

#define CRC32_SIZE		sizeof(uint32_t)
/* minimum trailing null bytes */
#define TRAILER_SIZE		sizeof(uint16_t)

#define err(fmt, args...)	fprintf(stderr, "%s: Error: " fmt, CMD_NAME, ##args)
#ifdef DEBUG
# define dbg(fmt, args...)	fprintf(stdout, fmt, ##args)
#else
# define dbg(fmt, args...)
#endif

/* file "object" */
struct file {
	char *name;
	int fd;
	uint8_t *ptr;
	struct stat sbuf;
};

static void usage_and_exit(int status) __attribute__((noreturn));

static void usage_and_exit(int status)
{
	printf("usage: mkenv [-t <type>] [-s <size>] <source file> <target image file>\n"
	       "  -s <size>  set size of the target image file to <size> bytes. If <size> is\n"
	       "             bigger than the source file, the target image gets padded with null\n"
	       "             bytes. If <size> is smaller than the source file, an error is emitted.\n"
	       "  -t <type>  set type of the target image, where <type> is one of:\n"
	       "             - binary (default if -t is not specified)\n"
	       "             - srec\n"
	       "  -r         reverse operation: get source file from target image file\n");
	exit(status);
}

int main(int argc, char **argv)
{
	int i;
	int status = EXIT_FAILURE;
	struct file s, t;	/* source and target file */
	ssize_t img_size = -1;
	ssize_t min_img_size;
	char img_type[7];
	bool reverse = false;
	uint8_t *p, *q;
	uint32_t *crc;

	if (argc < 2)
		usage_and_exit(EXIT_FAILURE);

	snprintf(img_type, sizeof(img_type), "%s", DEFAULT_IMG_TYPE);

	/* parse commandline options */
	for (i = 1; (i + 1 < argc) && (argv[i][0] == '-'); i++) {
		switch (argv[i][1]) {
		case 's':
			img_size = strtol(argv[++i], NULL, 10);
			break;
		case 't':
			snprintf(img_type, sizeof(img_type), "%s", argv[++i]);
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

	s.name = argv[i];
	t.name = argv[++i];

	s.fd = open(s.name, O_RDONLY);
	if (s.fd < 0) {
		err("Can't open source file '%s': %s\n", s.name,
				strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (fstat(s.fd, &s.sbuf) < 0) {
		err("Can't stat source file '%s': %s\n", s.name,
				strerror(errno));
		goto err_out_close_s;
	}

	s.ptr = mmap(NULL, s.sbuf.st_size, PROT_READ, MAP_SHARED, s.fd, 0);
	if (s.ptr == MAP_FAILED) {
		err("Can't mmap source image file '%s': %s\n", s.name,
				strerror(errno));
		goto err_out_close_s;
	}

	/* TODO: Check source file format:
	 *	var=value\n
	 */

	/* check whether the size hasn't been set or whether the source file +
	 * CRC + trailer fits into the specified size.
	 */
	min_img_size = CRC32_SIZE + s.sbuf.st_size + TRAILER_SIZE;
	if (img_size < 0)
		img_size = min_img_size;
	else if (img_size < min_img_size) {
		err("Specified size (%zd) is too small for the source file to "
				"fit into. Must be at least %zd bytes.\n",
				img_size, min_img_size);
		goto err_out_munmap_s;
	}

	t.fd = open(t.name, O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (t.fd < 0) {
		err("Can't open target image file '%s': %s\n", t.name,
				strerror(errno));
		goto err_out_munmap_s;
	}

	/* seek to the end of the target file to write a byte */
	if (lseek(t.fd, img_size - 1, SEEK_SET) < 0) {
		err("Can't seek in target image file '%s': %s\n", t.name,
				strerror(errno));
		goto err_out_close_t;
	}

	if (write(t.fd, "", 1) < 0) {
		err("Can't write to target image file '%s': %s\n", t.name,
				strerror(errno));
		goto err_out_close_t;
	}

	t.ptr = mmap(NULL, img_size, PROT_READ|PROT_WRITE, MAP_SHARED, t.fd, 0);
	if (t.ptr == MAP_FAILED) {
		err("Can't mmap target image file '%s': %s\n", t.name,
				strerror(errno));
		goto err_out_close_t;
	}

	printf("source file:       %s\n", s.name);
	printf("target image file: %s\n", t.name);
	printf("size:              %zd\n", img_size);
	printf("type:              %s\n", img_type);

	/* CRC32 placeholder, will be filled later */
	dbg("writing crc...\n");
	for (p = t.ptr; p < t.ptr + CRC32_SIZE; p++)
		*p = 0;

	/* copy the source file, replacing \n by \0 */
	dbg("writing data...\n");
	for (q = s.ptr; q < s.ptr + s.sbuf.st_size; q++, p++) {
		uint8_t c = (*q == '\n') ? '\0' : *q;

		*p = c;
	}

	/* trailer */
	dbg("writing trailer...\n");
	for (q = s.ptr + s.sbuf.st_size; q < s.ptr + img_size; q++)
		*p = 0;

	/* now for the real CRC32 */
	dbg("calculating crc...\n");
	crc = (uint32_t *) t.ptr;
	*crc = crc32(0, t.ptr + CRC32_SIZE, img_size - CRC32_SIZE);
	dbg("crc: %08x\n", *crc);

	status = EXIT_SUCCESS;

	munmap(t.ptr, img_size);
err_out_close_t:
	close(t.fd);
err_out_munmap_s:
	munmap(s.ptr, s.sbuf.st_size);
err_out_close_s:
	close(s.fd);

	exit(status);
}
