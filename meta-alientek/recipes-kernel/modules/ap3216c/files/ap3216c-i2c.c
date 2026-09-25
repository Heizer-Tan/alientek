/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AP3216C I2C 驱动入口：probe / remove 与 module_i2c_driver。
 *
 * 说明：
 *   - 寄存器走 regmap（见 hw）；misc 因本内核无 devm_misc_register，
 *     仍用 misc_register，并在 remove 中先注销再关机。
 *   - 并发 read 由 data->lock 串行化（见 misc / hw）
 */

#include "ap3216c.h"

#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

static int ap3216c_probe(struct i2c_client *client)
{
	struct ap3216c_data *data;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
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
		dev_err(&client->dev, "misc_register 失败: %d\n", ret);
		if (ap3216c_power_down(data) < 0)
			dev_warn(&client->dev, "probe 失败后关机写寄存器失败\n");
		return ret;
	}

	dev_info(&client->dev, "AP3216C ready at 0x%02x (/dev/%s)\n",
		 client->addr, AP3216C_DRV_NAME);
	return 0;
}

static void ap3216c_remove(struct i2c_client *client)
{
	struct ap3216c_data *data = i2c_get_clientdata(client);

	/* 先摘掉用户态入口，再动硬件，避免 remove 过程中仍有 read */
	misc_deregister(&data->miscdev);

	if (ap3216c_power_down(data) < 0)
		dev_warn(&client->dev, "remove 时关机写寄存器失败\n");
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
