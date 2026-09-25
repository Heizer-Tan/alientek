/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AP3216C 驱动内部头文件：公共结构、常量与跨编译单元接口。
 */

#ifndef AP3216C_H
#define AP3216C_H

#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define AP3216C_DRV_NAME		"ap3216c"

/* 系统配置寄存器：复位 / 电源 / 模式 */
#define AP3216C_SYS_CFG			0x00
#define AP3216C_IR_DATA_LOW		0x0A /* IR/ALS/PS 数据区起始（共 6 字节） */

/* SYS_CFG 取值（手册：System Configuration） */
#define AP3216C_POWER_DOWN		0x00 /* 关机 */
#define AP3216C_ENABLE_ALL		0x03 /* ALS+PS+IR 同时工作 */
#define AP3216C_SW_RESET		0x04 /* 软件复位（写后需延时） */

#define AP3216C_DATA_REG_COUNT		6U
#define AP3216C_READ_BUF_SIZE		64U
/* 软复位后 datasheet 建议等待稳定；50ms 偏保守，板级实测足够 */
#define AP3216C_RESET_DELAY_MS		50
/*
 * ALS+PS+IR 连续模式典型转换周期约 112.5ms；读太快会撞上转换中，
 * IR/PS 无效位置位，旧逻辑会输出 0，表现为大幅跳动。
 */
#define AP3216C_CONV_DELAY_MS		120
/* 芯片寄存器地址空间很小，限制 regmap 访问上界 */
#define AP3216C_MAX_REGISTER		0x0F

/* 每设备私有数据：regmap + misc 节点 + 读锁 + 上次有效样本 */
struct ap3216c_data {
	struct i2c_client *client;
	struct regmap *map;
	struct miscdevice miscdev;
	struct mutex lock; /* 保护 hw 访问与并发 read */
	u16 last_ir;
	u16 last_als;
	u16 last_ps;
	bool have_sample;
	ktime_t last_sample_kt; /* 上次真正访问芯片的时间 */
};

/* ap3216c-hw.c：regmap、解析、初始化 / 关机 / 采样 */
int ap3216c_hw_init(struct ap3216c_data *data);
int ap3216c_power_down(struct ap3216c_data *data);
int ap3216c_read_values(struct ap3216c_data *data, u16 *ir, u16 *als,
			u16 *ps);

/* ap3216c-misc.c：/dev/ap3216c 的 file_operations */
extern const struct file_operations ap3216c_fops;

#endif /* AP3216C_H */
