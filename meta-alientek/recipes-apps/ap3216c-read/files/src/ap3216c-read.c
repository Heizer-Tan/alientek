/* SPDX-License-Identifier: MIT */
/* ap3216c-read：读取 /dev/ap3216c，支持单次或循环打印 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define AP3216C_DEV_PATH "/dev/ap3216c"
#define AP3216C_BUF_SIZE 128
#define AP3216C_LOOP_DELAY_US 500000U

static void printUsage(const char *progName)
{
	fprintf(stderr, "用法: %s [-w]\n", progName);
	fprintf(stderr, "  -w  持续循环读取\n");
}

static int readOnce(const int fd)
{
	char buf[AP3216C_BUF_SIZE];
	const ssize_t n = read(fd, buf, sizeof(buf) - 1);

	if (n < 0) {
		fprintf(stderr, "ap3216c-read: read: %s\n", strerror(errno));
		return 1;
	}
	buf[n] = '\0';
	fputs(buf, stdout);
	return 0;
}

int main(int argc, char *argv[])
{
	int loopMode = 0;
	int fd;

	if (argc > 2) {
		printUsage(argv[0]);
		return 1;
	}
	if (argc == 2) {
		if (strcmp(argv[1], "-w") != 0) {
			printUsage(argv[0]);
			return 1;
		}
		loopMode = 1;
	}

	fd = open(AP3216C_DEV_PATH, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "ap3216c-read: open %s: %s\n", AP3216C_DEV_PATH,
			strerror(errno));
		return 1;
	}

	if (loopMode != 0) {
		while (1) {
			if (readOnce(fd) != 0) {
				close(fd);
				return 1;
			}
			usleep(AP3216C_LOOP_DELAY_US);
			if (lseek(fd, 0, SEEK_SET) < 0) {
				fprintf(stderr, "ap3216c-read: lseek: %s\n", strerror(errno));
				close(fd);
				return 1;
			}
		}
	}

	if (readOnce(fd) != 0) {
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}
