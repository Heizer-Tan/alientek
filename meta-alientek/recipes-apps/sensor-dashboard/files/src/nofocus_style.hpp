/* SPDX-License-Identifier: MIT */
#pragma once

#include <QProxyStyle>
#include <QStyleOption>

/* 去掉 Qt 默认的虚线焦点框（stylesheet 无法彻底消除） */
class NoFocusStyle final : public QProxyStyle {
public:
	using QProxyStyle::QProxyStyle;

	void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
			   QPainter *painter, const QWidget *widget) const override
	{
		if (element == PE_FrameFocusRect)
			return;
		QProxyStyle::drawPrimitive(element, option, painter, widget);
	}

	void drawControl(ControlElement element, const QStyleOption *option,
			 QPainter *painter, const QWidget *widget) const override
	{
		if (element == CE_FocusFrame)
			return;
		/* 按钮/面板绘制时去掉 HasFocus，避免风格再画虚线框 */
		if (element == CE_PushButton || element == CE_PushButtonBevel) {
			if (const auto *btn =
				    qstyleoption_cast<const QStyleOptionButton *>(
					    option)) {
				QStyleOptionButton opt(*btn);
				opt.state &= ~QStyle::State_HasFocus;
				QProxyStyle::drawControl(element, &opt, painter,
							 widget);
				return;
			}
		}
		QProxyStyle::drawControl(element, option, painter, widget);
	}

	int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
		      const QWidget *widget = nullptr,
		      QStyleHintReturn *returnData = nullptr) const override
	{
		if (hint == SH_UnderlineShortcut)
			return 0;
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};
