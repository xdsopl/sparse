
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

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

int encode(char *name)
{
	fprintf(stderr, "encoding %s to stdout\n", name);
	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		perror(name);
		return 1;
	}
	off_t end = lseek(fd, 0, SEEK_END);
	if (end < 0) {
		perror(name);
		return 1;
	}
	// total size
	if (put_word_be(end))
		return 1;
	off_t data = lseek(fd, 0, SEEK_DATA);
	while (data >= 0) {
		// data begins at this offset
		if (put_word_be(data))
			return 1;
		off_t hole = lseek(fd, data, SEEK_HOLE);
		if (hole < 0) {
			perror(name);
			return 1;
		}
		// hole begins at this offset
		if (put_word_be(hole))
			return 1;
		if (data != lseek(fd, data, SEEK_SET)) {
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
		data = lseek(fd, hole, SEEK_DATA);
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
		fprintf(stderr, "truncating %s to size %ld\n", name, total);
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
		if ((off_t)data != lseek(fd, data, SEEK_SET)) {
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
	fprintf(stderr, "usage: %s {e|d|p} file\n", name);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc != 3 || strlen(argv[1]) != 1 || !strlen(argv[2]))
		return usage(argv[0]);
	switch (argv[1][0]) {
		case 'e':
			return encode(argv[2]);
		case 'd':
			return decode(argv[2], 0);
		case 'p':
			return decode(argv[2], 1);
	}
	return usage(argv[0]);
}

