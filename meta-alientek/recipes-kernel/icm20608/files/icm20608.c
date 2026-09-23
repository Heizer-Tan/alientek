/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ICM20608 SPI IIO 驱动：导出 accel / anglvel / temp sysfs（轮询 read_raw）。
 */

#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#define ICM20608_DRV_NAME		"icm20608"
#define ICM20608_WHO_AM_I_REG		0x75
#define ICM20608_WHO_AM_I_VAL_AF	0xAF
#define ICM20608_WHO_AM_I_VAL_AE	0xAE
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

/* ±2g：1 LSB = 1/16384 g */
#define ICM20608_ACCEL_SCALE_NUM	1
#define ICM20608_ACCEL_SCALE_DEN	16384
/* ±2000 dps：与旧演示一致，scale = 10/164（即 /16.4） */
#define ICM20608_GYRO_SCALE_NUM		10
#define ICM20608_GYRO_SCALE_DEN		164
/* temp_c = raw/326.8 + 25 → scale=10/3268；IIO:(raw+offset)*scale */
#define ICM20608_TEMP_SCALE_NUM		10
#define ICM20608_TEMP_SCALE_DEN		3268
#define ICM20608_TEMP_OFFSET_RAW	8170 /* 25 * 3268 / 10 */

struct icm20608_state {
	struct spi_device *spi;
	struct mutex lock;
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

	return spi_write(spi, tx, sizeof(tx));
}

static int icm20608_read_regs(struct spi_device *spi, u8 reg, u8 *buf,
			      size_t len)
{
	u8 cmd = reg | ICM20608_READ_BIT;

	return spi_write_then_read(spi, &cmd, 1, buf, len);
}

static int icm20608_read_reg(struct spi_device *spi, u8 reg, u8 *val)
{
	return icm20608_read_regs(spi, reg, val, 1);
}

static s16 icm20608_be16(const u8 *p)
{
	return (s16)(((u16)p[0] << 8) | p[1]);
}

/* 一次 burst 读满 14 字节并缓存到 state */
static int icm20608_refresh_sample(struct icm20608_state *st)
{
	u8 buf[ICM20608_DATA_BYTES];
	int ret;

	ret = icm20608_read_regs(st->spi, ICM20608_ACCEL_XOUT_H, buf,
				 sizeof(buf));
	if (ret < 0)
		return ret;

	st->ax = icm20608_be16(&buf[0]);
	st->ay = icm20608_be16(&buf[2]);
	st->az = icm20608_be16(&buf[4]);
	st->temp_raw = icm20608_be16(&buf[6]);
	st->gx = icm20608_be16(&buf[8]);
	st->gy = icm20608_be16(&buf[10]);
	st->gz = icm20608_be16(&buf[12]);
	return 0;
}

static int icm20608_raw_for_chan(struct icm20608_state *st,
				 const struct iio_chan_spec *chan, int *val)
{
	switch (chan->type) {
	case IIO_ACCEL:
		if (chan->channel2 == IIO_MOD_X)
			*val = st->ax;
		else if (chan->channel2 == IIO_MOD_Y)
			*val = st->ay;
		else
			*val = st->az;
		return IIO_VAL_INT;
	case IIO_ANGL_VEL:
		if (chan->channel2 == IIO_MOD_X)
			*val = st->gx;
		else if (chan->channel2 == IIO_MOD_Y)
			*val = st->gy;
		else
			*val = st->gz;
		return IIO_VAL_INT;
	case IIO_TEMP:
		*val = st->temp_raw;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int icm20608_read_scale(const struct iio_chan_spec *chan, int *val,
			       int *val2)
{
	switch (chan->type) {
	case IIO_ACCEL:
		*val = ICM20608_ACCEL_SCALE_NUM;
		*val2 = ICM20608_ACCEL_SCALE_DEN;
		return IIO_VAL_FRACTIONAL;
	case IIO_ANGL_VEL:
		*val = ICM20608_GYRO_SCALE_NUM;
		*val2 = ICM20608_GYRO_SCALE_DEN;
		return IIO_VAL_FRACTIONAL;
	case IIO_TEMP:
		*val = ICM20608_TEMP_SCALE_NUM;
		*val2 = ICM20608_TEMP_SCALE_DEN;
		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

static int icm20608_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	struct icm20608_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		ret = icm20608_refresh_sample(st);
		if (ret == 0)
			ret = icm20608_raw_for_chan(st, chan, val);
		mutex_unlock(&st->lock);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		return icm20608_read_scale(chan, val, val2);
	case IIO_CHAN_INFO_OFFSET:
		if (chan->type != IIO_TEMP)
			return -EINVAL;
		/* (raw + 8170) * (10/3268) ≡ raw/326.8 + 25 */
		*val = ICM20608_TEMP_OFFSET_RAW;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

#define ICM20608_ACCEL_CHAN(_mod) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = _mod,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

#define ICM20608_GYRO_CHAN(_mod) {				\
	.type = IIO_ANGL_VEL,					\
	.modified = 1,						\
	.channel2 = _mod,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec icm20608_channels[] = {
	ICM20608_ACCEL_CHAN(IIO_MOD_X),
	ICM20608_ACCEL_CHAN(IIO_MOD_Y),
	ICM20608_ACCEL_CHAN(IIO_MOD_Z),
	ICM20608_GYRO_CHAN(IIO_MOD_X),
	ICM20608_GYRO_CHAN(IIO_MOD_Y),
	ICM20608_GYRO_CHAN(IIO_MOD_Z),
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
	},
};

static const struct iio_info icm20608_info = {
	.read_raw = icm20608_read_raw,
};

static int icm20608_hw_init(struct icm20608_state *st)
{
	u8 who = 0;
	int ret;

	ret = icm20608_read_reg(st->spi, ICM20608_WHO_AM_I_REG, &who);
	if (ret < 0)
		return ret;
	/* 板上常见 0xAF（ICM-20608-G）或 0xAE */
	if (who != ICM20608_WHO_AM_I_VAL_AF && who != ICM20608_WHO_AM_I_VAL_AE) {
		dev_err(&st->spi->dev, "unexpected WHO_AM_I 0x%02x\n", who);
		return -ENODEV;
	}
	dev_info(&st->spi->dev, "WHO_AM_I 0x%02x\n", who);

	ret = icm20608_write_reg(st->spi, ICM20608_PWR_MGMT_1, 0x80);
	if (ret < 0)
		return ret;
	msleep(50);

	ret = icm20608_write_reg(st->spi, ICM20608_PWR_MGMT_1, 0x01);
	if (ret < 0)
		return ret;
	ret = icm20608_write_reg(st->spi, ICM20608_PWR_MGMT_2, 0x00);
	if (ret < 0)
		return ret;
	ret = icm20608_write_reg(st->spi, ICM20608_SMPLRT_DIV, 0x00);
	if (ret < 0)
		return ret;
	ret = icm20608_write_reg(st->spi, ICM20608_CONFIG, 0x04);
	if (ret < 0)
		return ret;
	/* Gyro ±2000dps */
	ret = icm20608_write_reg(st->spi, ICM20608_GYRO_CONFIG, 0x18);
	if (ret < 0)
		return ret;
	/* Accel ±2g */
	ret = icm20608_write_reg(st->spi, ICM20608_ACCEL_CONFIG, 0x00);
	if (ret < 0)
		return ret;
	return icm20608_write_reg(st->spi, ICM20608_ACCEL_CONFIG2, 0x04);
}

static int icm20608_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct icm20608_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	st->spi = spi;
	mutex_init(&st->lock);

	indio_dev->name = ICM20608_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &icm20608_info;
	indio_dev->channels = icm20608_channels;
	indio_dev->num_channels = ARRAY_SIZE(icm20608_channels);

	ret = icm20608_hw_init(st);
	if (ret < 0)
		return ret;

	spi_set_drvdata(spi, indio_dev);
	ret = iio_device_register(indio_dev);
	if (ret < 0)
		return ret;

	dev_info(&spi->dev, "ICM20608 IIO ready on SPI\n");
	return 0;
}

static void icm20608_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);
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
MODULE_DESCRIPTION("ICM20608 SPI IIO driver for Alientek i.MX6ULL Alpha");
MODULE_LICENSE("GPL");
