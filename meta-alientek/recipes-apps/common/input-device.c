/* SPDX-License-Identifier: MIT */
/* 共享：按名称查找 /dev/input/event* */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "input-device.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define INPUT_DIR "/dev/input"
#define EVENT_PREFIX "event"

static int nameMatches(int fd, const char *nameSubstr)
{
	char name[256];

	memset(name, 0, sizeof(name));
	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
		return 0;
	return strstr(name, nameSubstr) != NULL;
}

int inputFindDeviceByName(const char *nameSubstr, const char *progName,
			  int silent)
{
	DIR *dir;
	struct dirent *ent;
	char path[PATH_MAX];
	const char *tag = progName ? progName : "input";

	if (!nameSubstr || !*nameSubstr)
		return -1;
	dir = opendir(INPUT_DIR);
	if (!dir) {
		if (!silent)
			fprintf(stderr, "%s: opendir %s: %s\n", tag, INPUT_DIR,
				strerror(errno));
		return -1;
	}
	while ((ent = readdir(dir)) != NULL) {
		int fd;

		if (strncmp(ent->d_name, EVENT_PREFIX, strlen(EVENT_PREFIX)) !=
		    0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, ent->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		if (nameMatches(fd, nameSubstr)) {
			closedir(dir);
			return fd;
		}
		close(fd);
	}
	closedir(dir);
	if (!silent)
		fprintf(stderr, "%s: no input device matching \"%s\"\n", tag,
			nameSubstr);
	return -1;
}
