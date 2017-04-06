/*
Copyright (C) 2017 by Ahmet Inan <inan@distec.de>

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int put_word_be(uint64_t word)
{
	for (int i = 56; 0 <= i; i -= 8) {
		if (putchar_unlocked((word >> i) & 255) < 0) {
			fprintf(stderr, "failed writing one byte to stdout\n");
			return 1;
		}
	}
	return 0;
}

int encode(char *name, uint64_t offset)
{
	fprintf(stderr, "encoding %s to stdout\n", name);
	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		perror(name);
		return 1;
	}
	off64_t end = lseek64(fd, 0, SEEK_END);
	if (end < 0) {
		perror(name);
		return 1;
	}
	// total size
	if (put_word_be(end))
		return 1;
	off64_t data = lseek64(fd, offset, SEEK_DATA);
	while (data >= 0) {
		// data begins at this offset
		if (put_word_be(data))
			return 1;
		off64_t hole = lseek64(fd, data, SEEK_HOLE);
		if (hole < 0) {
			perror(name);
			return 1;
		}
		// hole begins at this offset
		if (put_word_be(hole))
			return 1;
		if (data != lseek64(fd, data, SEEK_SET)) {
			perror(name);
			return 1;
		}
		uint8_t buf[4096];
		while (data < hole) {
			ssize_t bytes = hole - data;
			if (bytes > 4096)
				bytes = 4096;
			bytes = read(fd, buf, bytes);
			data += bytes;
			if (bytes < 0) {
				perror(name);
				return 1;
			}
			for (int i = 0; i < bytes; ++i) {
				if (putchar_unlocked(buf[i]) < 0) {
					fprintf(stderr, "failed writing one byte to stdout\n");
					return 1;
				}
			}
		}
		data = lseek64(fd, hole, SEEK_DATA);
	}
	return 0;
}

int get_word_be(uint64_t *word)
{
	*word = 0;
	for (int i = 0; i < 8; ++i) {
		int byte = getchar_unlocked();
		if (byte < 0)
			return 1;
		*word <<= 8;
		*word |= byte;
	}
	return 0;
}

int decode(char *name, int patch)
{
	int flags = O_WRONLY;
	if (!patch) {
		flags |= O_CREAT | O_TRUNC;
		fprintf(stderr, "decoding stdin to %s\n", name);
	} else {
		fprintf(stderr, "decoding stdin and patching %s\n", name);
	}
	int fd = open(name, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		perror(name);
		return 1;
	}
	uint64_t total;
	if (get_word_be(&total)) {
		fprintf(stderr, "failed reading total size word from stdin\n");
		return 1;
	}
	if (!patch) {
		fprintf(stderr, "truncating %s to size %" PRIu64 "\n", name, total);
		if (ftruncate(fd, total)) {
			perror(name);
			return 1;
		}
	}
	while (1) {
		uint64_t data;
		if (get_word_be(&data))
			return 0;
		uint64_t hole;
		if (get_word_be(&hole)) {
			fprintf(stderr, "failed reading hole offset word from stdin\n");
			return 1;
		}
		if ((off64_t)data != lseek64(fd, data, SEEK_SET)) {
			perror(name);
			return 1;
		}
		uint8_t buf[4096];
		while (data < hole) {
			ssize_t bytes = hole - data;
			if (bytes > 4096)
				bytes = 4096;
			data += bytes;
			for (int i = 0; i < bytes; ++i) {
				int byte = getchar_unlocked();
				if (byte < 0) {
					fprintf(stderr, "failed reading one byte from stdin\n");
					return 1;
				}
				buf[i] = byte;
			}
			for (uint8_t *p = buf; bytes;) {
				ssize_t count = write(fd, p, bytes);
				if (count < 0) {
					perror(name);
					return 1;
				}
				bytes -= count;
				p += count;
			}
		}
	}
	return 0;
}

int usage(char *name)
{
	fprintf(stderr, "usage: %s {e|d|p} file [offset]\n", name);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 3 || 4 < argc || strlen(argv[1]) != 1 || !strlen(argv[2]))
		return usage(argv[0]);
	switch (argv[1][0]) {
		case 'e': {
			uint64_t offset = 0;
			if (argc == 4) {
				char *endptr;
				errno = 0;
				offset = strtoll(argv[3], &endptr, 10);
				if (errno || endptr == argv[3])
					return usage(argv[0]);
			}
			return encode(argv[2], offset);
		} case 'd': {
			if (argc != 3)
				return usage(argv[0]);
			return decode(argv[2], 0);
		} case 'p': {
			if (argc != 3)
				return usage(argv[0]);
			return decode(argv[2], 1);
		}
	}
	return usage(argv[0]);
}

