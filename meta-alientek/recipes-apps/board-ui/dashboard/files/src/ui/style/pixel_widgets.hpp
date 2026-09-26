/* SPDX-License-Identifier: MIT */
#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QWidget>

class QLabel;
class QVBoxLayout;

/* 16×16 像素精灵编号 */
enum class PixelGlyph : int {
	Light = 0,
	Imu,
	Sys,
	Led,
	Key,
	Ota,
};

/* 放大绘制的像素图标 */
class PixelIcon final : public QWidget {
	Q_OBJECT
public:
	explicit PixelIcon(PixelGlyph glyph, QColor tone, QWidget *parent = nullptr);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	PixelGlyph glyph_;
	QColor tone_;
	int scale_ = 2; /* 16→32px */
};

/*
 * 像素壳：阶梯角 + tone 粗边 + 右下纯黑硬投影（无 blur）。
 * 用作主页卡片 / 详情标题容器。
 */
class PixelShell final : public QAbstractButton {
	Q_OBJECT
public:
	explicit PixelShell(QColor tone, QWidget *parent = nullptr);
	/* 内容区布局（已扣除边框与投影留白） */
	QVBoxLayout *body() const { return body_; }
	void setTone(QColor tone);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QColor tone_;
	QVBoxLayout *body_ = nullptr;
};

/* ████░░░░ 分段进度条 */
class PixelProgressBar final : public QWidget {
	Q_OBJECT
public:
	explicit PixelProgressBar(QWidget *parent = nullptr);
	void setValue(int percent);
	int value() const { return value_; }

protected:
	void paintEvent(QPaintEvent *event) override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

private:
	int value_ = 0;
	static constexpr int kSegments = 20;
};
