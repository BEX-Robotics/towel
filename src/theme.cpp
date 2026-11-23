#include "theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace theme {

// Towel's lane palette: ten hues at matched brightness, so no branch shouts.
QColor lane(int i) {
    static const QColor pal[] = {
        QColor(0x5a, 0xa9, 0xe6), // sky
        QColor(0xe0, 0x71, 0x9c), // rose
        QColor(0x7b, 0xc9, 0x6f), // leaf
        QColor(0xf0, 0xa3, 0x5e), // amber
        QColor(0xa5, 0x8c, 0xf0), // violet
        QColor(0x4f, 0xc1, 0xb5), // teal
        QColor(0xe8, 0xd1, 0x6a), // sand
        QColor(0xef, 0x7a, 0x6d), // coral
        QColor(0x6f, 0x8f, 0xf2), // indigo
        QColor(0xc5, 0x8f, 0xd1), // orchid
    };
    return pal[((i % 10) + 10) % 10];
}

QColor statusColor(const QString &s) {
    if (s == QLatin1String("A")) return green;
    if (s == QLatin1String("D")) return red;
    if (s == QLatin1String("R")) return blue;
    return yellow;
}

namespace {
double g_zoom = 1.0;
}

double zoom() { return g_zoom; }

Zoom *Zoom::instance() {
    static Zoom z;
    return &z;
}

int zp(int px) { return int(px * std::sqrt(zoom()) + 0.5); }

// Cached per pixel size and zoom: views call this for every row they paint.
int lineHeight(int px) {
    static QHash<int, int> cache;
    static double cachedZoom = 0;
    if (cachedZoom != g_zoom) {
        cache.clear();
        cachedZoom = g_zoom;
    }

    auto it = cache.constFind(px);
    if (it != cache.constEnd()) return *it;
    return *cache.insert(px, QFontMetrics(uiFont(px)).height());
}

namespace {
// Scales one size of a widget, keeping its unzoomed value in a property so repeated steps
// never drift. If the widget changed the size itself since we last set it, that new value
// becomes the base.
int rescaled(QWidget *w, const char *name, int current, double oldZoom) {
    const QVariant stored = w->property(name);
    double base = current / oldZoom;
    if (stored.isValid()) {
        const QPointF p = stored.toPointF(); // x: base, y: the value we last applied
        if (int(p.y()) == current) base = p.x();
    }

    const int now = std::max(1, int(base * g_zoom + 0.5));
    w->setProperty(name, QPointF(base, now));
    return now;
}
} // namespace

void setZoom(double factor) {
    factor = std::clamp(factor, 0.6, 3.0);
    if (qFuzzyCompare(factor, g_zoom)) return;
    const double old = g_zoom;
    g_zoom = factor;
    QApplication::setFont(uiFont(13));

    for (QWidget *w : QApplication::allWidgets()) {
        // A styled widget does not pick up a new application font by itself (and re-applying
        // the style sheet would restore each font to its first-styled size), so widgets that
        // follow the application font get it set directly, every time.
        if (!w->testAttribute(Qt::WA_SetFont) || w->property("towelAppFont").toBool()) {
            w->setFont(QApplication::font(w));
            w->setProperty("towelAppFont", true);
        } else if (w->font().pixelSize() > 0) {
            QFont f = w->font();
            f.setPixelSize(std::max(6, rescaled(w, "towelPx", f.pixelSize(), old)));
            w->setFont(f);
        }

        if (w->minimumHeight() == w->maximumHeight() && w->minimumHeight() > 0) w->setFixedHeight(rescaled(w, "towelH", w->minimumHeight(), old));
        if (w->minimumWidth() == w->maximumWidth() && w->minimumWidth() > 0) w->setFixedWidth(rescaled(w, "towelW", w->minimumWidth(), old));
    }

    emit Zoom::instance() -> changed();
}

QFont uiFont(int px, bool bold) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    f.setPixelSize(z(px));
    f.setBold(bold);

    return f;
}

QFont monoFont(int px) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPixelSize(z(px));
    f.setStyleHint(QFont::Monospace);

    return f;
}

QString styleSheet() {
    return QStringLiteral(R"(
QWidget { color: #d8dce3; }
QToolTip { background: #0f1114; color: #d8dce3; border: 1px solid #2b2f37; padding: 4px; }
QPushButton, QToolButton {
  background: transparent; border: 1px solid transparent; border-radius: 5px;
  padding: 4px 10px; color: #d8dce3;
}
QPushButton:hover, QToolButton:hover { background: #2a2e37; }
QPushButton:pressed, QToolButton:pressed { background: #30353f; }
QPushButton:checked, QToolButton:checked { background: #28374b; border-color: #3fa2f7; }
QPushButton:disabled, QToolButton:disabled { color: #5c6370; }
QPushButton[chip="true"] { background: #272b33; }
QPushButton[chip="true"]:hover { background: #30353f; }
QPushButton[primary="true"] { background: #2f7cc4; color: white; }
QPushButton[primary="true"]:hover { background: #3a8fdc; }
QPushButton[primary="true"]:disabled { background: #263241; color: #5c6370; }
QCheckBox { spacing: 8px; }
QLineEdit, QPlainTextEdit {
  background: #272b33; border: 1px solid #2b2f37; border-radius: 6px; padding: 4px 8px;
  selection-background-color: #28374b; color: #d8dce3;
}
QLineEdit:focus, QPlainTextEdit:focus { border-color: #3fa2f7; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: #3a3f49; border-radius: 4px; min-height: 30px; min-width: 30px; }
QScrollBar::handle:hover { background: #4a505b; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QSplitter::handle { background: #2b2f37; }
QSplitter::handle:hover { background: #3fa2f7; }
QScrollArea { border: none; }
QDialog { background: #21242b; }
)");
}

void paintIcon(QPainter *p, Icon icon, const QRectF &r, const QColor &c) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->translate(r.topLeft());
    p->scale(r.width() / 16.0, r.height() / 16.0);
    QPen pen(c, 1.4);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    switch (icon) {
    case Icon::Laptop:
        p->drawRoundedRect(QRectF(3, 3.5, 10, 7), 1, 1);
        p->drawLine(QPointF(1.5, 12.5), QPointF(14.5, 12.5));
        break;

    case Icon::Cloud: {
        QPainterPath path(QPointF(4.5, 12.5));
        path.lineTo(11.7, 12.5);
        path.cubicTo(15.2, 12.5, 15.4, 7.2, 12.0, 6.9);
        path.cubicTo(11.2, 3.0, 5.6, 2.8, 4.3, 6.0);
        path.cubicTo(0.6, 6.4, 1.0, 12.5, 4.5, 12.5);
        p->drawPath(path);
        break;
    }

    case Icon::Tag: {
        QPainterPath path(QPointF(2, 2.5));
        path.lineTo(7.5, 2.5);
        path.lineTo(14, 9);
        path.lineTo(9, 14);
        path.lineTo(2.5, 7.5);
        path.closeSubpath();
        p->drawPath(path);
        p->setPen(Qt::NoPen);
        p->setBrush(c);
        p->drawEllipse(QPointF(5, 5.5), 1, 1);
        break;
    }

    case Icon::Branch:
        p->drawEllipse(QPointF(4.5, 3.5), 1.6, 1.6);
        p->drawEllipse(QPointF(4.5, 12.5), 1.6, 1.6);
        p->drawEllipse(QPointF(11.5, 5), 1.6, 1.6);
        p->drawLine(QPointF(4.5, 5.1), QPointF(4.5, 10.9));
        {
            QPainterPath path(QPointF(11.5, 6.6));
            path.cubicTo(11.5, 9.6, 4.5, 8.8, 4.5, 10.9);
            p->drawPath(path);
        }
        break;

    case Icon::Stash: {
        QPainterPath path(QPointF(2, 9.5));
        path.lineTo(3.5, 3.5);
        path.lineTo(12.5, 3.5);
        path.lineTo(14, 9.5);
        path.lineTo(14, 13);
        path.lineTo(2, 13);
        path.closeSubpath();
        p->drawPath(path);
        p->drawPolyline(QPolygonF({QPointF(2, 9.5), QPointF(5.5, 9.5), QPointF(6.5, 11), QPointF(9.5, 11), QPointF(10.5, 9.5), QPointF(14, 9.5)}));
        break;
    }
    }
    p->restore();
}

} // namespace theme
