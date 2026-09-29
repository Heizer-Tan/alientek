/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 正点原子 Alpha 板载 LED：platform + misc → /dev/alientek-led
 * DT：compatible = "alientek,led"，属性 led-gpio
 * 用户态：write "0"/"1"（或单字节），read 返回 "0\n"/"1\n"
 */

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/miscdevice.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define ALIENTEK_LED_NAME "alientek-led"

struct alientek_led {
	struct gpio_desc *gpiod;
	struct miscdevice misc;
	struct mutex lock;
	bool on;
};

static ssize_t alientek_led_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct alientek_led *led = file->private_data;
	char out[2];

	if (*ppos != 0)
		return 0;
	if (count < 2)
		return -EINVAL;

	mutex_lock(&led->lock);
	out[0] = led->on ? '1' : '0';
	out[1] = '\n';
	mutex_unlock(&led->lock);

	if (copy_to_user(buf, out, 2))
		return -EFAULT;
	*ppos = 2;
	return 2;
}

static ssize_t alientek_led_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct alientek_led *led = file->private_data;
	char kbuf[8];
	size_t n;
	bool on;

	if (count == 0)
		return 0;
	n = min(count, sizeof(kbuf) - 1);
	if (copy_from_user(kbuf, buf, n))
		return -EFAULT;
	kbuf[n] = '\0';

	if (kbuf[0] == '1')
		on = true;
	else if (kbuf[0] == '0')
		on = false;
	else
		return -EINVAL;

	mutex_lock(&led->lock);
	gpiod_set_value_cansleep(led->gpiod, on ? 1 : 0);
	led->on = on;
	mutex_unlock(&led->lock);
	return count;
}

static int alientek_led_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct alientek_led *led =
		container_of(misc, struct alientek_led, misc);

	file->private_data = led;
	return 0;
}

static const struct file_operations alientek_led_fops = {
	.owner = THIS_MODULE,
	.open = alientek_led_open,
	.read = alientek_led_read,
	.write = alientek_led_write,
	.llseek = noop_llseek,
};

static int alientek_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct alientek_led *led;
	int ret;

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	mutex_init(&led->lock);

	/* DT：led-gpio；GPIOD_OUT_HIGH = 逻辑亮（ACTIVE_LOW 时物理为低） */
	led->gpiod = devm_gpiod_get(dev, "led", GPIOD_OUT_HIGH);
	if (IS_ERR(led->gpiod)) {
		ret = PTR_ERR(led->gpiod);
		dev_err(dev, "led-gpio 获取失败: %d\n", ret);
		return ret;
	}
	led->on = true;

	led->misc.minor = MISC_DYNAMIC_MINOR;
	led->misc.name = ALIENTEK_LED_NAME;
	led->misc.fops = &alientek_led_fops;
	led->misc.parent = dev;

	ret = misc_register(&led->misc);
	if (ret) {
		dev_err(dev, "misc_register 失败: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, led);
	dev_info(dev, "/dev/%s 就绪\n", ALIENTEK_LED_NAME);
	return 0;
}

static void alientek_led_remove(struct platform_device *pdev)
{
	struct alientek_led *led = platform_get_drvdata(pdev);

	misc_deregister(&led->misc);
}

static const struct of_device_id alientek_led_of_match[] = {
	{ .compatible = "alientek,led" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, alientek_led_of_match);

static struct platform_driver alientek_led_driver = {
	.probe = alientek_led_probe,
	.remove = alientek_led_remove,
	.driver = {
		.name = "alientek-led",
		.of_match_table = alientek_led_of_match,
	},
};

module_platform_driver(alientek_led_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alientek Alpha");
MODULE_DESCRIPTION("Alientek board LED misc character device");
MODULE_ALIAS("platform:alientek-led");
