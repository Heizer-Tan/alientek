/* SPDX-License-Identifier: MIT */
#ifndef INPUT_DEVICE_H
#define INPUT_DEVICE_H

/*
 * 在 /dev/input 中按名称子串查找 event 设备。
 * silent=0 时失败打印到 stderr；返回已打开 fd 或 -1。
 */
int inputFindDeviceByName(const char *nameSubstr, const char *progName,
			  int silent);

#endif
