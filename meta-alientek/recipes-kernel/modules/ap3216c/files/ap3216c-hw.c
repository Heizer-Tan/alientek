/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AP3216C 硬件层：regmap 访问、数据解析、初始化与关机。
 */

#include "ap3216c.h"

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/ktime.h>

/* 8 位地址 / 8 位数据；强制单字节访问（本芯片 bulk/块读易错位） */
static const struct regmap_config ap3216c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AP3216C_MAX_REGISTER,
	.use_single_read = true,
	.use_single_write = true,
};

/* ---------- 数据解析（与用户态约定一致） ---------- */

/*
 * IR：低字节 bit7=无效/溢出。
 * 返回 true 表示本帧有效并写入 *out；无效时不改 *out（由调用方保留旧值）。
 */
static bool ap3216c_parse_ir(const u8 *buf, u16 *out)
{
	if (buf[0] & BIT(7))
		return false;
	*out = (u16)(((u16)buf[1] << 2) | (buf[0] & 0x03U));
	return true;
}

/* ALS：小端 16 位（无独立无效位） */
static u16 ap3216c_parse_als(const u8 *buf)
{
	return (u16)(((u16)buf[3] << 8) | buf[2]);
}

/*
 * PS：低字节 bit6=无效。
 * 返回 true 表示本帧有效；无效时不改 *out。
 */
static bool ap3216c_parse_ps(const u8 *buf, u16 *out)
{
	if (buf[4] & BIT(6))
		return false;
	*out = (u16)((((u16)buf[5] & 0x3FU) << 4) | (buf[4] & 0x0FU));
	return true;
}

/*
 * 连续读 IR/ALS/PS 数据区（6 字节）。
 * 必须用逐寄存器单字节读：与拆分前一致。
 * regmap_bulk_read / I2C block 在本芯片上会出现 0/1/256 这类错位跳变。
 */
static int ap3216c_read_data_regs(struct ap3216c_data *data, u8 *buf)
{
	u8 reg = AP3216C_IR_DATA_LOW;
	size_t i;
	unsigned int val;
	int ret;

	for (i = 0; i < AP3216C_DATA_REG_COUNT; ++i, ++reg) {
		ret = regmap_read(data->map, reg, &val);
		if (ret < 0) {
			dev_err_ratelimited(&data->client->dev,
					    "读寄存器 0x%02x 失败: %d\n",
					    reg, ret);
			return ret;
		}
		buf[i] = (u8)val;
	}
	return 0;
}

/* 用缓存样本填输出 */
static void ap3216c_copy_cached(struct ap3216c_data *data, u16 *ir, u16 *als,
				u16 *ps)
{
	*ir = data->last_ir;
	*als = data->last_als;
	*ps = data->last_ps;
}

/*
 * 采样策略：
 * 1) 距上次读芯片不足一个转换周期 → 直接返回缓存（避免读到转换中的脏数据）
 * 2) IR/PS 无效位为 1 时保留上次有效值，避免 0↔大数跳变
 * 3) ALS 每帧更新（手册无同等无效位）
 */
int ap3216c_read_values(struct ap3216c_data *data, u16 *ir, u16 *als, u16 *ps)
{
	u8 buf[AP3216C_DATA_REG_COUNT];
	u16 new_ir = data->last_ir;
	u16 new_ps = data->last_ps;
	ktime_t now;
	int ret;

	now = ktime_get();
	if (data->have_sample &&
	    ktime_to_ms(ktime_sub(now, data->last_sample_kt)) <
		    AP3216C_CONV_DELAY_MS) {
		ap3216c_copy_cached(data, ir, als, ps);
		return 0;
	}

	ret = ap3216c_read_data_regs(data, buf);
	if (ret < 0)
		return ret;

	(void)ap3216c_parse_ir(buf, &new_ir);
	data->last_als = ap3216c_parse_als(buf);
	(void)ap3216c_parse_ps(buf, &new_ps);
	data->last_ir = new_ir;
	data->last_ps = new_ps;
	data->have_sample = true;
	data->last_sample_kt = now;

	ap3216c_copy_cached(data, ir, als, ps);
	return 0;
}

int ap3216c_power_down(struct ap3216c_data *data)
{
	return regmap_write(data->map, AP3216C_SYS_CFG, AP3216C_POWER_DOWN);
}

/*
 * 软复位 → 延时 → 打开全部传感 → 读回校验 → 试读一帧。
 * 任一失败则 probe 失败，避免挂一个「假活」的 /dev 节点。
 */
int ap3216c_hw_init(struct ap3216c_data *data)
{
	struct device *dev = &data->client->dev;
	unsigned int cfg = 0;
	int ret;

	/* probe 阶段建立托管 regmap；失败则整次 probe 失败 */
	data->map = devm_regmap_init_i2c(data->client, &ap3216c_regmap_config);
	if (IS_ERR(data->map)) {
		ret = PTR_ERR(data->map);
		dev_err(dev, "regmap 初始化失败: %d\n", ret);
		return ret;
	}

	ret = regmap_write(data->map, AP3216C_SYS_CFG, AP3216C_SW_RESET);
	if (ret < 0) {
		dev_err(dev, "软件复位失败: %d\n", ret);
		return ret;
	}
	msleep(AP3216C_RESET_DELAY_MS);

	ret = regmap_write(data->map, AP3216C_SYS_CFG, AP3216C_ENABLE_ALL);
	if (ret < 0) {
		dev_err(dev, "使能传感失败: %d\n", ret);
		return ret;
	}

	ret = regmap_read(data->map, AP3216C_SYS_CFG, &cfg);
	if (ret < 0) {
		dev_err(dev, "读回 SYS_CFG 失败: %d\n", ret);
		return ret;
	}
	if (cfg != AP3216C_ENABLE_ALL) {
		dev_err(dev, "SYS_CFG 期望 0x%02x，实际 0x%02x\n",
			AP3216C_ENABLE_ALL, cfg);
		return -EIO;
	}

	/* 等满一个 ALS+PS+IR 周期再取首帧，避免启动阶段无效位狂跳 */
	msleep(AP3216C_CONV_DELAY_MS);

	/* 试读并填入缓存；确认总线可用 */
	{
		u16 ir = 0, als = 0, ps = 0;

		ret = ap3216c_read_values(data, &ir, &als, &ps);
		if (ret < 0) {
			dev_err(dev, "初始化后试读数据失败: %d\n", ret);
			return ret;
		}
	}

	return 0;
}
