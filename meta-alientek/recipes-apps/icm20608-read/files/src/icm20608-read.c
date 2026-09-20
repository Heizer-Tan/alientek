/* SPDX-License-Identifier: MIT */
/* icm20608-read：读取 /dev/icm20608，支持单次或循环打印 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ICM20608_DEV_PATH "/dev/icm20608"
#define ICM20608_BUF_SIZE 256
#define ICM20608_LOOP_DELAY_US 500000U

static volatile sig_atomic_t gStopRequested = 0;

static void handleSignal(int sig)
{
	(void)sig;
	gStopRequested = 1;
}

static void printUsage(const char *progName)
{
	fprintf(stderr, "用法: %s [-w]\n", progName);
	fprintf(stderr, "  -w  持续循环读取\n");
}

static int readOnce(const int fd)
{
	char buf[ICM20608_BUF_SIZE];
	const ssize_t n = read(fd, buf, sizeof(buf) - 1);

	if (n < 0) {
		fprintf(stderr, "icm20608-read: read: %s\n", strerror(errno));
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
	struct sigaction sa;

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

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handleSignal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	fd = open(ICM20608_DEV_PATH, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "icm20608-read: open %s: %s\n", ICM20608_DEV_PATH,
			strerror(errno));
		return 1;
	}

	if (loopMode != 0) {
		while (!gStopRequested) {
			if (readOnce(fd) != 0) {
				close(fd);
				return 1;
			}
			usleep(ICM20608_LOOP_DELAY_US);
			if (lseek(fd, 0, SEEK_SET) < 0) {
				fprintf(stderr, "icm20608-read: lseek: %s\n",
					strerror(errno));
				close(fd);
				return 1;
			}
		}
		close(fd);
		return 0;
	}

	if (readOnce(fd) != 0) {
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}
