/* SPDX-License-Identifier: MIT */
#include "ui/style/pixel_widgets.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

constexpr int kShadow = 4;
constexpr int kBorder = 3;
constexpr int kStair = 4;

/* 按行 inset 填充阶梯矩形（关闭抗锯齿时呈像素台阶） */
void fillStairRect(QPainter &p, QRect r, int step, const QColor &c)
{
	if (!r.isValid() || r.width() <= 0 || r.height() <= 0)
		return;
	for (int y = r.top(); y <= r.bottom(); ++y) {
		const int dy = qMin(y - r.top(), r.bottom() - y);
		const int inset = dy < step ? (step - dy) : 0;
		const int w = r.width() - 2 * inset;
		if (w > 0)
			p.fillRect(r.left() + inset, y, w, 1, c);
	}
}

/* 精灵：'.'=空 '#'=主色 '@'=高光 'X'=描边；16×16
 * 主页顶栏上 tone=墨色，故轮廓靠 #/@ 对比；描边 X 与 # 接近，少用。 */
void blitGlyph(QPainter &p, PixelGlyph g, const QColor &tone, int ox, int oy,
	       int scale)
{
	static const char *const kMaps[] = {
		/* Light：太阳 + 八角射线 */
		"................"
		".......@@......."
		"...#...@@...#..."
		"....#......#...."
		".....#.####.#..."
		"......#@@@@#...."
		"..##..#@@@@#..##"
		".@@@@##@@@@##@@@"
		"..##..#@@@@#..##"
		"......#@@@@#...."
		".....#.####.#..."
		"....#......#...."
		"...#...@@...#..."
		".......@@......."
		"................"
		"................",
		/* Imu：三轴（X 横 / Y 竖 / Z 斜） */
		"................"
		"........##......"
		".......#@@#....."
		".......#@@#....."
		".......#@@#....."
		".##....#@@#....."
		"#@@#####@@#####."
		"#@@@@@@@@@@@@@@#"
		"#@@#####@@#####."
		".##....#@@#..##."
		".......#@@#.#@@#"
		".......#@@#..##."
		"........##......"
		"..........##...."
		"................"
		"................",
		/* Sys：显示器 + 底座 */
		"................"
		".##############."
		".#@@@@@@@@@@@@#."
		".#@..........@#."
		".#@..######..@#."
		".#@..#@@@@#..@#."
		".#@..#@@@@#..@#."
		".#@..######..@#."
		".#@..........@#."
		".#@@@@@@@@@@@@#."
		".##############."
		"......####......"
		"....########...."
		"...##########..."
		"................"
		"................",
		/* Led：灯泡 + 灯丝 + 螺口 */
		"................"
		"......####......"
		"....##@@@@##...."
		"...#@@@@@@@@#..."
		"...#@@#..#@@#..."
		"...#@@####@@#..."
		"...#@@@@@@@@#..."
		"....##@@@@##...."
		".....#@@@@#....."
		"......#@@#......"
		"......####......"
		"......#XX#......"
		"......#XX#......"
		"......####......"
		"................"
		"................",
		/* Key：圆形按键 + 高光 */
		"................"
		"....########...."
		"...##@@@@@@##..."
		"..#@@@@@@@@@@#.."
		"..#@@######@@#.."
		".#@@#......#@@#."
		".#@@#..@@..#@@#."
		".#@@#..@@..#@@#."
		".#@@#......#@@#."
		"..#@@######@@#.."
		"..#@@@@@@@@@@#.."
		"...##@@@@@@##..."
		"....########...."
		"......####......"
		"......####......"
		"................",
		/* Ota：上行箭头 + 底座托盘 */
		"................"
		".......@@......."
		"......@@@@......"
		".....@@@@@@....."
		"....@@@@@@@@...."
		"...@@@####@@@..."
		"..@@@..##..@@@.."
		"..###..##..###.."
		".......##......."
		".......##......."
		".......##......."
		"...##########..."
		"..##@@@@@@@@##.."
		"...##########..."
		"................"
		"................",
	};
	const int idx = static_cast<int>(g);
	if (idx < 0 || idx >= 6)
		return;
	const char *map = kMaps[idx];
	const QColor ink(0x02, 0x04, 0x06);
	const QColor hi(0xF4, 0xF8, 0xFF);
	for (int row = 0; row < 16; ++row) {
		for (int col = 0; col < 16; ++col) {
			const char ch = map[row * 16 + col];
			QColor c;
			if (ch == '.')
				continue;
			else if (ch == '#')
				c = tone;
			else if (ch == '@')
				c = hi;
			else
				c = ink;
			p.fillRect(ox + col * scale, oy + row * scale, scale,
				    scale, c);
		}
	}
}

} // namespace

PixelIcon::PixelIcon(PixelGlyph glyph, QColor tone, QWidget *parent)
	: QWidget(parent), glyph_(glyph), tone_(std::move(tone))
{
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_StyledBackground, false);
	setStyleSheet(QStringLiteral("background: transparent; border: none;"));
	setFixedSize(16 * scale_, 16 * scale_);
}

QSize PixelIcon::sizeHint() const
{
	return QSize(16 * scale_, 16 * scale_);
}

QSize PixelIcon::minimumSizeHint() const
{
	return sizeHint();
}

void PixelIcon::paintEvent(QPaintEvent * /*event*/)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);
	blitGlyph(p, glyph_, tone_, 0, 0, scale_);
}

PixelShell::PixelShell(QColor tone, QWidget *parent)
	: QAbstractButton(parent), tone_(std::move(tone))
{
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::NoFocus);
	setAttribute(Qt::WA_MacShowFocusRect, false);
	setAttribute(Qt::WA_StyledBackground, false);
	setStyleSheet(QStringLiteral("background: transparent; border: none;"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	body_ = new QVBoxLayout(this);
	/* 左边/上边框 + 右边/下边框与硬投影 */
	body_->setContentsMargins(kBorder + 2, kBorder + 2,
				  kBorder + 2 + kShadow, kBorder + 2 + kShadow);
	body_->setSpacing(0);
}

void PixelShell::setTone(QColor tone)
{
	tone_ = std::move(tone);
	update();
}

void PixelShell::paintEvent(QPaintEvent * /*event*/)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);

	const bool down = isDown();
	const int dx = down ? 2 : 0;
	const int dy = down ? 2 : 0;

	QRect full(0, 0, width() - kShadow, height() - kShadow);
	QRect shadow(kShadow, kShadow, full.width(), full.height());
	QRect body = full.translated(dx, dy);

	/* 硬投影（按下时缩短，呈下沉感） */
	if (!down)
		fillStairRect(p, shadow, kStair, QColor(0x02, 0x04, 0x06));
	else
		fillStairRect(p, shadow.adjusted(0, 0, -2, -2), kStair,
			      QColor(0x02, 0x04, 0x06));

	/* 面板填充 */
	fillStairRect(p, body, kStair, QColor(0x0B, 0x12, 0x20));

	/* tone 粗描边：外阶梯减内阶梯 */
	fillStairRect(p, body, kStair, tone_);
	QRect inner = body.adjusted(kBorder, kBorder, -kBorder, -kBorder);
	fillStairRect(p, inner, qMax(1, kStair - kBorder),
		      QColor(0x0B, 0x12, 0x20));

	/* 顶边高光条（1px，增强块面） */
	if (inner.height() > 8) {
		const int y = inner.top();
		const int dy2 = 0;
		const int inset = kStair - kBorder;
		p.fillRect(inner.left() + inset, y + dy2,
			    inner.width() - 2 * inset, 1,
			    tone_.lighter(130));
	}
}

PixelProgressBar::PixelProgressBar(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_StyledBackground, false);
	setStyleSheet(QStringLiteral("background: transparent; border: none;"));
	setFixedHeight(28 + kShadow);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PixelProgressBar::setValue(int percent)
{
	int v = percent;
	if (v < 0)
		v = 0;
	if (v > 100)
		v = 100;
	if (v == value_)
		return;
	value_ = v;
	update();
}

QSize PixelProgressBar::sizeHint() const
{
	return QSize(200, 28 + kShadow);
}

QSize PixelProgressBar::minimumSizeHint() const
{
	return QSize(80, 28 + kShadow);
}

void PixelProgressBar::paintEvent(QPaintEvent * /*event*/)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);

	QRect outer = rect().adjusted(0, 0, -kShadow, -kShadow);
	fillStairRect(p, QRect(kShadow, kShadow, outer.width(), outer.height()),
		      3, QColor(0x02, 0x04, 0x06));
	fillStairRect(p, outer, 3, QColor(0x22, 0xC5, 0x5E));
	QRect inner = outer.adjusted(2, 2, -2, -2);
	fillStairRect(p, inner, 2, QColor(0x02, 0x04, 0x06));

	const int filled = (value_ * kSegments + 99) / 100;
	const int gap = 2;
	const int usable = inner.width() - gap * (kSegments + 1);
	const int segW = qMax(2, usable / kSegments);
	const int segH = inner.height() - 4;
	int x = inner.left() + gap;
	const int y = inner.top() + 2;
	for (int i = 0; i < kSegments; ++i) {
		const QColor c = i < filled ? QColor(0x22, 0xC5, 0x5E)
					    : QColor(0x1A, 0x2A, 0x22);
		p.fillRect(x, y, segW, segH, c);
		x += segW + gap;
	}
}
