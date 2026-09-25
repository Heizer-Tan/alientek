/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AP3216C I2C misc 驱动：导出 /dev/ap3216c，读取 IR/ALS/PS。
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define AP3216C_DRV_NAME          "ap3216c"
#define AP3216C_SYS_CFG           0x00
#define AP3216C_IR_DATA_LOW       0x0A
#define AP3216C_ENABLE_ALL        0x03
#define AP3216C_SW_RESET          0x04
#define AP3216C_POWER_DOWN        0x00
#define AP3216C_DATA_REG_COUNT    6U
#define AP3216C_READ_BUF_SIZE     64U

struct ap3216c_data {
	struct i2c_client *client;
	struct miscdevice miscdev;
	struct mutex lock;
};

static int ap3216c_write_reg(const struct i2c_client *client,
			     const u8 reg, const u8 val)
{
	const s32 ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		return (int)ret;
	return 0;
}

static int ap3216c_read_reg(const struct i2c_client *client, const u8 reg,
			    u8 *val)
{
	const s32 ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		return (int)ret;
	*val = (u8)ret;
	return 0;
}

static u16 ap3216c_parse_ir(const u8 *buf)
{
	if ((buf[0] & BIT(7)) != 0U)
		return 0;
	return (u16)(((u16)buf[1] << 2) | (buf[0] & 0x03U));
}

static u16 ap3216c_parse_als(const u8 *buf)
{
	return (u16)(((u16)buf[3] << 8) | buf[2]);
}

static u16 ap3216c_parse_ps(const u8 *buf)
{
	if ((buf[4] & BIT(6)) != 0U)
		return 0;
	return (u16)((((u16)buf[5] & 0x3FU) << 4) | (buf[4] & 0x0FU));
}

static int ap3216c_read_values(struct ap3216c_data *data, u16 *ir,
			       u16 *als, u16 *ps)
{
	u8 buf[AP3216C_DATA_REG_COUNT];
	u8 reg = AP3216C_IR_DATA_LOW;
	int ret;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(buf); ++i, ++reg) {
		ret = ap3216c_read_reg(data->client, reg, &buf[i]);
		if (ret < 0)
			return ret;
	}

	*ir = ap3216c_parse_ir(buf);
	*als = ap3216c_parse_als(buf);
	*ps = ap3216c_parse_ps(buf);
	return 0;
}

static ssize_t ap3216c_misc_read(struct file *file, char __user *userBuf,
				 size_t count, loff_t *ppos)
{
	struct miscdevice *miscdev = file->private_data;
	struct ap3216c_data *data = container_of(miscdev, struct ap3216c_data,
						 miscdev);
	char buf[AP3216C_READ_BUF_SIZE];
	u16 ir = 0;
	u16 als = 0;
	u16 ps = 0;
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
	if (copy_to_user(userBuf, buf, len) != 0)
		return -EFAULT;
	*ppos += len;
	return len;
}

static loff_t ap3216c_misc_llseek(struct file *file, const loff_t offset,
				 const int whence)
{
	if (whence != SEEK_SET || offset != 0)
		return -EINVAL;
	file->f_pos = 0;
	return 0;
}

static const struct file_operations ap3216c_fops = {
	.owner = THIS_MODULE,
	.read = ap3216c_misc_read,
	.llseek = ap3216c_misc_llseek,
};

static int ap3216c_hw_init(struct ap3216c_data *data)
{
	int ret;

	ret = ap3216c_write_reg(data->client, AP3216C_SYS_CFG, AP3216C_SW_RESET);
	if (ret < 0)
		return ret;
	msleep(50);
	return ap3216c_write_reg(data->client, AP3216C_SYS_CFG,
				 AP3216C_ENABLE_ALL);
}

static int ap3216c_probe(struct i2c_client *client)
{
	struct ap3216c_data *data;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->client = client;
	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = AP3216C_DRV_NAME;
	data->miscdev.fops = &ap3216c_fops;
	data->miscdev.parent = &client->dev;
	mutex_init(&data->lock);
	ret = ap3216c_hw_init(data);
	if (ret < 0)
		return ret;

	i2c_set_clientdata(client, data);
	ret = misc_register(&data->miscdev);
	if (ret < 0) {
		ap3216c_write_reg(client, AP3216C_SYS_CFG, AP3216C_POWER_DOWN);
		return ret;
	}

	dev_info(&client->dev, "AP3216C ready at 0x%02x\n", client->addr);
	return 0;
}

static void ap3216c_remove(struct i2c_client *client)
{
	struct ap3216c_data *data = i2c_get_clientdata(client);

	misc_deregister(&data->miscdev);
	ap3216c_write_reg(client, AP3216C_SYS_CFG, AP3216C_POWER_DOWN);
}

static const struct of_device_id ap3216c_of_match[] = {
	{ .compatible = "alientek,ap3216c" },
	{ }
};
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

static struct i2c_driver ap3216c_driver = {
	.driver = {
		.name = AP3216C_DRV_NAME,
		.of_match_table = ap3216c_of_match,
	},
	.probe = ap3216c_probe,
	.remove = ap3216c_remove,
};
module_i2c_driver(ap3216c_driver);

MODULE_AUTHOR("Cursor Assistant");
MODULE_DESCRIPTION("AP3216C I2C misc driver for Alientek i.MX6ULL Alpha");
MODULE_LICENSE("GPL");
