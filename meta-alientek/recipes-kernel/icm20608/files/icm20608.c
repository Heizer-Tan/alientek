/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ICM20608 SPI misc 驱动：导出 /dev/icm20608，读取 accel/gyro/temp。
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#define ICM20608_DRV_NAME		"icm20608"
#define ICM20608_WHO_AM_I_REG		0x75
#define ICM20608_WHO_AM_I_VAL_AF		0xAF
#define ICM20608_WHO_AM_I_VAL_AE		0xAE
#define ICM20608_SMPLRT_DIV		0x19
#define ICM20608_CONFIG			0x1A
#define ICM20608_GYRO_CONFIG		0x1B
#define ICM20608_ACCEL_CONFIG		0x1C
#define ICM20608_ACCEL_CONFIG2		0x1D
#define ICM20608_PWR_MGMT_1		0x6B
#define ICM20608_PWR_MGMT_2		0x6C
#define ICM20608_ACCEL_XOUT_H		0x3B
#define ICM20608_DATA_BYTES		14U
#define ICM20608_READ_BIT		0x80U
#define ICM20608_ACCEL_SCALE		16384
#define ICM20608_GYRO_SCALE_NUM		164
#define ICM20608_GYRO_SCALE_DEN		10
#define ICM20608_TEMP_SCALE_NUM		3268
#define ICM20608_TEMP_SCALE_DEN		10
#define ICM20608_READ_BUF_SIZE		256U

struct icm20608_data {
	struct spi_device *spi;
	struct miscdevice miscdev;
	struct mutex lock;
};

struct icm20608_sample {
	s16 ax;
	s16 ay;
	s16 az;
	s16 temp_raw;
	s16 gx;
	s16 gy;
	s16 gz;
};

static int icm20608_write_reg(struct spi_device *spi, u8 reg, u8 val)
{
	u8 tx[2] = { reg & 0x7FU, val };
	struct spi_transfer t = {
		.tx_buf = tx,
		.len = 2,
	};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(spi, &m);
}

static int icm20608_read_regs(struct spi_device *spi, u8 reg, u8 *buf,
			      size_t len)
{
	u8 cmd = reg | ICM20608_READ_BIT;
	struct spi_transfer t[2] = {
		{ .tx_buf = &cmd, .len = 1 },
		{ .rx_buf = buf, .len = len },
	};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);
	return spi_sync(spi, &m);
}

static int icm20608_read_reg(struct spi_device *spi, u8 reg, u8 *val)
{
	return icm20608_read_regs(spi, reg, val, 1);
}

static s16 icm20608_be16(const u8 *p)
{
	return (s16)(((u16)p[0] << 8) | p[1]);
}

static int icm20608_read_sample(struct icm20608_data *data,
				struct icm20608_sample *out)
{
	u8 buf[ICM20608_DATA_BYTES];
	int ret;

	ret = icm20608_read_regs(data->spi, ICM20608_ACCEL_XOUT_H, buf,
				 sizeof(buf));
	if (ret < 0)
		return ret;

	out->ax = icm20608_be16(&buf[0]);
	out->ay = icm20608_be16(&buf[2]);
	out->az = icm20608_be16(&buf[4]);
	out->temp_raw = icm20608_be16(&buf[6]);
	out->gx = icm20608_be16(&buf[8]);
	out->gy = icm20608_be16(&buf[10]);
	out->gz = icm20608_be16(&buf[12]);
	return 0;
}

/* 定点：raw/scale，decimals 位小数，写入 buf，返回写入长度 */
static int icm20608_scnprintf_div(char *buf, size_t size, s32 raw, s32 scale,
				  unsigned int decimals)
{
	u32 mul = 1;
	u32 abs_raw;
	u32 int_part;
	u32 frac;
	unsigned int i;
	const int neg = raw < 0;
	const s32 n = neg ? -raw : raw;

	if (scale <= 0)
		return -EINVAL;
	for (i = 0; i < decimals; ++i)
		mul *= 10U;
	abs_raw = (u32)n;
	int_part = abs_raw / (u32)scale;
	frac = ((abs_raw % (u32)scale) * mul) / (u32)scale;
	if (neg)
		return scnprintf(buf, size, "-%u.%0*u", int_part, decimals,
				 frac);
	return scnprintf(buf, size, "%u.%0*u", int_part, decimals, frac);
}

/* 陀螺仪：raw / 16.4 = raw * 10 / 164 */
static int icm20608_scnprintf_gyro(char *buf, size_t size, s16 raw)
{
	s64 scaled;
	s32 whole;
	s32 frac;
	int neg;

	scaled = div_s64((s64)raw * (s64)ICM20608_GYRO_SCALE_DEN * 1000LL,
			 ICM20608_GYRO_SCALE_NUM);
	neg = scaled < 0;
	if (neg)
		scaled = -scaled;
	whole = (s32)div_s64(scaled, 1000);
	frac = (s32)(scaled - (s64)whole * 1000);
	if (neg)
		return scnprintf(buf, size, "-%d.%03d", whole, frac);
	return scnprintf(buf, size, "%d.%03d", whole, frac);
}

/* 温度：temp_raw/326.8 + 25，两位小数 */
static int icm20608_scnprintf_temp(char *buf, size_t size, s16 temp_raw)
{
	s64 centi;
	s32 whole;
	s32 frac;
	int neg;

	centi = div_s64((s64)temp_raw * (s64)ICM20608_TEMP_SCALE_DEN * 100LL,
			ICM20608_TEMP_SCALE_NUM) +
		2500;
	neg = centi < 0;
	if (neg)
		centi = -centi;
	whole = (s32)div_s64(centi, 100);
	frac = (s32)(centi - (s64)whole * 100);
	if (neg)
		return scnprintf(buf, size, "-%d.%02d", whole, frac);
	return scnprintf(buf, size, "%d.%02d", whole, frac);
}

static int icm20608_format_line(char *buf, size_t size,
				const struct icm20608_sample *s)
{
	char ax_g[16];
	char ay_g[16];
	char az_g[16];
	char gx_dps[16];
	char gy_dps[16];
	char gz_dps[16];
	char temp_c[16];
	int ret;

	ret = icm20608_scnprintf_div(ax_g, sizeof(ax_g), s->ax,
				     ICM20608_ACCEL_SCALE, 4);
	if (ret < 0)
		return ret;
	ret = icm20608_scnprintf_div(ay_g, sizeof(ay_g), s->ay,
				     ICM20608_ACCEL_SCALE, 4);
	if (ret < 0)
		return ret;
	ret = icm20608_scnprintf_div(az_g, sizeof(az_g), s->az,
				     ICM20608_ACCEL_SCALE, 4);
	if (ret < 0)
		return ret;
	icm20608_scnprintf_gyro(gx_dps, sizeof(gx_dps), s->gx);
	icm20608_scnprintf_gyro(gy_dps, sizeof(gy_dps), s->gy);
	icm20608_scnprintf_gyro(gz_dps, sizeof(gz_dps), s->gz);
	icm20608_scnprintf_temp(temp_c, sizeof(temp_c), s->temp_raw);

	return scnprintf(buf, size,
			 "ax=%d ay=%d az=%d gx=%d gy=%d gz=%d temp_raw=%d "
			 "ax_g=%s ay_g=%s az_g=%s gx_dps=%s gy_dps=%s "
			 "gz_dps=%s temp_c=%s\n",
			 s->ax, s->ay, s->az, s->gx, s->gy, s->gz, s->temp_raw,
			 ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps, temp_c);
}

static ssize_t icm20608_misc_read(struct file *file, char __user *userBuf,
				  size_t count, loff_t *ppos)
{
	struct miscdevice *miscdev = file->private_data;
	struct icm20608_data *data =
		container_of(miscdev, struct icm20608_data, miscdev);
	struct icm20608_sample sample;
	char buf[ICM20608_READ_BUF_SIZE];
	int len;
	int ret;

	if (*ppos != 0)
		return 0;

	mutex_lock(&data->lock);
	ret = icm20608_read_sample(data, &sample);
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;

	len = icm20608_format_line(buf, sizeof(buf), &sample);
	if (len <= 0)
		return -EINVAL;
	if (count < (size_t)len)
		return -EINVAL;
	if (copy_to_user(userBuf, buf, len) != 0)
		return -EFAULT;
	*ppos += len;
	return len;
}

static loff_t icm20608_misc_llseek(struct file *file, const loff_t offset,
				   const int whence)
{
	if (whence != SEEK_SET || offset != 0)
		return -EINVAL;
	file->f_pos = 0;
	return 0;
}

static const struct file_operations icm20608_fops = {
	.owner = THIS_MODULE,
	.read = icm20608_misc_read,
	.llseek = icm20608_misc_llseek,
};

static int icm20608_hw_init(struct icm20608_data *data)
{
	u8 who = 0;
	int ret;

	ret = icm20608_read_reg(data->spi, ICM20608_WHO_AM_I_REG, &who);
	if (ret < 0)
		return ret;
	/* 板上常见 0xAF（ICM-20608-G）或 0xAE */
	if (who != ICM20608_WHO_AM_I_VAL_AF && who != ICM20608_WHO_AM_I_VAL_AE) {
		dev_err(&data->spi->dev, "unexpected WHO_AM_I 0x%02x\n", who);
		return -ENODEV;
	}
	dev_info(&data->spi->dev, "WHO_AM_I 0x%02x\n", who);

	ret = icm20608_write_reg(data->spi, ICM20608_PWR_MGMT_1, 0x80);
	if (ret < 0)
		return ret;
	msleep(50);

	ret = icm20608_write_reg(data->spi, ICM20608_PWR_MGMT_1, 0x01);
	if (ret < 0)
		return ret;
	ret = icm20608_write_reg(data->spi, ICM20608_PWR_MGMT_2, 0x00);
	if (ret < 0)
		return ret;
	ret = icm20608_write_reg(data->spi, ICM20608_SMPLRT_DIV, 0x00);
	if (ret < 0)
		return ret;
	ret = icm20608_write_reg(data->spi, ICM20608_CONFIG, 0x04);
	if (ret < 0)
		return ret;
	/* Gyro ±2000dps */
	ret = icm20608_write_reg(data->spi, ICM20608_GYRO_CONFIG, 0x18);
	if (ret < 0)
		return ret;
	/* Accel ±2g */
	ret = icm20608_write_reg(data->spi, ICM20608_ACCEL_CONFIG, 0x00);
	if (ret < 0)
		return ret;
	return icm20608_write_reg(data->spi, ICM20608_ACCEL_CONFIG2, 0x04);
}

static int icm20608_probe(struct spi_device *spi)
{
	struct icm20608_data *data;
	int ret;

	data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	data->spi = spi;
	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = ICM20608_DRV_NAME;
	data->miscdev.fops = &icm20608_fops;
	data->miscdev.parent = &spi->dev;
	mutex_init(&data->lock);

	ret = icm20608_hw_init(data);
	if (ret < 0)
		return ret;

	spi_set_drvdata(spi, data);
	ret = misc_register(&data->miscdev);
	if (ret < 0)
		return ret;

	dev_info(&spi->dev, "ICM20608 ready on SPI\n");
	return 0;
}

static void icm20608_remove(struct spi_device *spi)
{
	struct icm20608_data *data = spi_get_drvdata(spi);

	misc_deregister(&data->miscdev);
	icm20608_write_reg(spi, ICM20608_PWR_MGMT_1, 0x40);
}

static const struct of_device_id icm20608_of_match[] = {
	{ .compatible = "alientek,icm20608" },
	{ }
};
MODULE_DEVICE_TABLE(of, icm20608_of_match);

static const struct spi_device_id icm20608_id[] = {
	{ "icm20608", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, icm20608_id);

static struct spi_driver icm20608_driver = {
	.driver = {
		.name = ICM20608_DRV_NAME,
		.of_match_table = icm20608_of_match,
	},
	.probe = icm20608_probe,
	.remove = icm20608_remove,
	.id_table = icm20608_id,
};
module_spi_driver(icm20608_driver);

MODULE_AUTHOR("Cursor Assistant");
MODULE_DESCRIPTION("ICM20608 SPI misc driver for Alientek i.MX6ULL Alpha");
MODULE_LICENSE("GPL");
