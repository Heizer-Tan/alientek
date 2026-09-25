/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AP3216C misc 字符设备：/dev/ap3216c 的 read / llseek。
 *
 * 用户态约定（勿改格式）：
 *   输出一行文本 ir=<u16> als=<u16> ps=<u16>\n
 *   ap3216c-read / ap3216c-logger / dashboard 均按此解析。
 */

#include "ap3216c.h"

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

/*
 * 一次性读：*ppos==0 时采样并输出一行；再次 read 返回 0（EOF）。
 * 用户态循环读需配合 llseek(0, SEEK_SET)。
 */
static ssize_t ap3216c_misc_read(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct miscdevice *miscdev = file->private_data;
	struct ap3216c_data *data =
		container_of(miscdev, struct ap3216c_data, miscdev);
	char buf[AP3216C_READ_BUF_SIZE];
	u16 ir = 0, als = 0, ps = 0;
	int len;
	int ret;

	if (*ppos != 0)
		return 0;

	mutex_lock(&data->lock);
	ret = ap3216c_read_values(data, &ir, &als, &ps);
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;

	len = scnprintf(buf, sizeof(buf), "ir=%u als=%u ps=%u\n", ir, als, ps);
	if (count < (size_t)len)
		return -EINVAL;
	if (copy_to_user(user_buf, buf, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

/* 仅允许 seek 到文件头，供用户态重复 read */
static loff_t ap3216c_misc_llseek(struct file *file, loff_t offset, int whence)
{
	if (whence != SEEK_SET || offset != 0)
		return -EINVAL;
	file->f_pos = 0;
	return 0;
}

const struct file_operations ap3216c_fops = {
	.owner = THIS_MODULE,
	.read = ap3216c_misc_read,
	.llseek = ap3216c_misc_llseek,
};
