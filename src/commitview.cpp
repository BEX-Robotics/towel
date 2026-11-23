#include "commitview.h"
#include <QContextMenuEvent>
#include "avatars.h"
#include "commitmodel.h"
#include "theme.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QToolTip>

namespace {

QString relativeTime(qint64 t) {
    const qint64 d = QDateTime::currentSecsSinceEpoch() - t;
    if (d < 60) return QStringLiteral("just now");
    if (d < 3600) return QStringLiteral("%1 min ago").arg(d / 60);
    if (d < 86400) return QStringLiteral("%1 h ago").arg(d / 3600);
    if (d < 86400 * 7) return d < 2 * 86400 ? QStringLiteral("yesterday") : QStringLiteral("%1 days ago").arg(d / 86400);
    const QDate date = QDateTime::fromSecsSinceEpoch(t).date();
    return QLocale().toString(date, date.year() == QDate::currentDate().year() ? QStringLiteral("d MMM") : QStringLiteral("d MMM yyyy"));
}

QColor withAlpha(QColor c, qreal a) {
    c.setAlphaF(a);
    return c;
}

} // namespace

CommitView::CommitView(CommitModel *model, QWidget *parent) : QAbstractScrollArea(parent), m_model(model) {
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    connect(theme::Zoom::instance(), &theme::Zoom::changed, this, [this] {
        const int top = verticalScrollBar()->value() / std::max(1, m_rowHAtZoom); // keep the same top row
        relayout();
        verticalScrollBar()->setValue(top * rowHeight());
        m_rowHAtZoom = rowHeight();
    });
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    verticalScrollBar()->setSingleStep(rowHeight());
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_model, &CommitModel::reset, this, [this] {
        relayout();
        viewport()->update();
    });
    connect(m_model, &CommitModel::searchFinished, viewport(), qOverload<>(&QWidget::update));
    connect(AvatarCache::instance(), &AvatarCache::ready, viewport(), qOverload<>(&QWidget::update));
}

// As wide as the lanes on screen (plus a page either side, so it doesn't
// twitch on every scroll step), not the widest point anywhere in history.
int CommitView::graphWidth() const {
    const int sv = verticalScrollBar()->value(), h = viewport()->height();
    const int first = std::max(0, sv / rowHeight() - h / rowHeight()), last = std::min(m_model->count() - 1, (sv + 2 * h) / rowHeight());
    int lanes = 1;
    for (int r = first; r <= last; ++r) {
        const CommitRow *row = m_model->row(r);
        lanes = std::max(lanes, int(row->col) + 1);
        const Edge *e = m_model->edgesOf(row);
        for (int k = 0; k < row->edgeCount; ++k) lanes = std::max({lanes, int(e[k].a) + 1, int(e[k].b) + 1});
    }
    return std::min(std::max(lanes, 2) * laneWidth() + 16, 400);
}

int CommitView::refsWidth() const { return viewport()->width() > 760 ? 176 : 0; }
int CommitView::authorWidth() const { return viewport()->width() > 1000 ? 150 : 0; }
int CommitView::dateWidth() const { return viewport()->width() > 700 ? 96 : 0; }

void CommitView::relayout() {
    const int visible = viewport()->height();
    verticalScrollBar()->setPageStep(visible);
    verticalScrollBar()->setRange(0, std::max(0, m_model->count() * rowHeight() - visible));
}

void CommitView::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    relayout();
}

void CommitView::scrollContentsBy(int, int) { viewport()->update(); }

int CommitView::rowAt(int y) const {
    const int r = (y + verticalScrollBar()->value()) / rowHeight();
    return (r >= 0 && r < m_model->count()) ? r : -1;
}

void CommitView::setCurrent(int row, bool ensureVisible) {
    if (m_model->count() == 0) return;
    row = std::clamp(row, 0, m_model->count() - 1);
    if (ensureVisible) {
        const int top = row * rowHeight(), sv = verticalScrollBar()->value(), h = viewport()->height();
        if (top < sv)
            verticalScrollBar()->setValue(top);
        else if (top + rowHeight() > sv + h)
            verticalScrollBar()->setValue(top + rowHeight() - h);
    }
    if (row == m_current) return;
    m_current = row;
    viewport()->update();
    emit currentChanged(row);
}

// ---------- painting ----------

void CommitView::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    const int h = viewport()->height();
    p.fillRect(viewport()->rect(), theme::bg);
    const int sv = verticalScrollBar()->value();
    const int first = sv / rowHeight();
    const int last = std::min(m_model->count() - 1, (sv + h) / rowHeight());
    m_gw = graphWidth();
    for (int r = first; r <= last; ++r) paintRow(p, r, r * rowHeight() - sv);
}

// Ref chip: fully rounded, icon first. Current branch is filled with its lane
// color; others are outlined. Returns the chip width.
qreal CommitView::paintChip(QPainter &p, const RefGroup &g, const QColor &lane, qreal x, qreal cy, qreal maxW) {
    const QFont f = theme::uiFont(12, g.current || g.head);
    const QFontMetricsF fm(f);
    const int nIcons = (g.tag ? 1 : 0) + (g.local ? 1 : 0) + (g.remote ? 1 : 0) + (g.head ? 1 : 0);
    const qreal iconsW = nIcons * 14.0;
    const qreal full = std::ceil(fm.horizontalAdvance(g.name)) + 2;
    const qreal textW = std::min<qreal>(full, maxW - iconsW - 22);
    if (textW < 24) return 0;
    const qreal w = 10 + iconsW + (nIcons ? 5 : 0) + textW + 10;
    const QRectF r(x, cy - 10, w, 20);

    const QColor accent = g.tag ? theme::dim : g.head ? theme::yellow
                                                      : lane;
    const bool filled = g.current;
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(filled ? accent : withAlpha(accent, 0.75), 1));
    p.setBrush(filled ? accent : withAlpha(accent, 0.12));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    const QColor fg = filled ? theme::bg : accent.lighter(125);
    qreal cx = x + 10;
    auto icon = [&](theme::Icon i) {
        theme::paintIcon(&p, i, QRectF(cx - 1, cy - 6.5, 13, 13), fg);
        cx += 14;
    };
    if (g.tag) icon(theme::Icon::Tag);
    if (g.head) icon(theme::Icon::Branch);
    if (g.local) icon(theme::Icon::Laptop);
    if (g.remote) icon(theme::Icon::Cloud);
    if (nIcons) cx += 5;
    p.setFont(f);
    p.setPen(fg);
    p.drawText(QRectF(cx, r.top(), textW + 1, r.height()), Qt::AlignVCenter,
        textW >= full ? g.name : fm.elidedText(g.name, Qt::ElideMiddle, textW - 2));

    return w;
}

void CommitView::paintGraph(QPainter &p, int row, const QRectF &cell) {
    const CommitRow *r = m_model->row(row);
    const qreal H = cell.height(), top = cell.top(), bottom = top + H, mid = top + H / 2;
    auto X = [&](int lane) { return cell.left() + 10 + laneWidth() * lane + laneWidth() / 2.0; };
    p.save();
    p.setClipRect(cell);
    p.setRenderHint(QPainter::Antialiasing);
    const Edge *edges = m_model->edgesOf(r);
    for (int i = 0; i < r->edgeCount; ++i) {
        const Edge &e = edges[i];
        QPen pen(theme::lane(e.c), 2.0);
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal xa = X(e.a), xb = X(e.b);
        if (e.k == 0)
            p.drawLine(QPointF(xa, top), QPointF(xa, bottom));
        else if (e.k == 1) {
            // into the node: smooth S-curve over the upper half
            QPainterPath path(QPointF(xa, top));
            path.cubicTo(QPointF(xa, top + H * 0.35), QPointF(xb, mid - H * 0.25), QPointF(xb, mid));
            p.drawPath(path);
        } else {
            // out of the node: smooth S-curve over the lower half
            QPainterPath path(QPointF(xa, mid));
            path.cubicTo(QPointF(xa, mid + H * 0.25), QPointF(xb, bottom - H * 0.35), QPointF(xb, bottom));
            p.drawPath(path);
        }
    }

    const QColor c = theme::lane(r->color);
    const qreal x = X(r->col);
    const qreal R = nodeSize() / 2.0;
    if (r->wip) {
        QPen pen(c, 1.6, Qt::CustomDashLine);
        pen.setDashPattern({2.5, 2.0});
        p.setPen(pen);
        p.setBrush(theme::bg);
        p.drawEllipse(QPointF(x, mid), R - 2, R - 2);
    } else if (r->merge) {
        p.setPen(QPen(c, 2.2));
        p.setBrush(theme::bg);
        p.drawEllipse(QPointF(x, mid), 5, 5);
    } else {
        // avatar with a thin lane ring and a gap of background between them
        p.setPen(Qt::NoPen);
        p.setBrush(theme::bg);
        p.drawEllipse(QPointF(x, mid), R + 1.5, R + 1.5);
        p.setPen(QPen(c, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(x, mid), R + 0.5, R + 0.5);
        AvatarCache::paint(&p, QRectF(x - R + 2, mid - R + 2, nodeSize() - 4, nodeSize() - 4), m_model->emailOf(r), m_model->authorOf(r), theme::bg, 0);
    }
    p.restore();
}

void CommitView::paintRow(QPainter &p, int row, int y) {
    const CommitRow *r = m_model->row(row);
    const int w = viewport()->width();
    const int gw = m_gw;
    const QColor lane = theme::lane(r->color);
    const bool cur = row == m_current;

    if (cur) {
        p.fillRect(0, y, w, rowHeight(), theme::selection);
        p.fillRect(0, y, 3, rowHeight(), lane);
    } else if (row == m_hover)
        p.fillRect(0, y, w, rowHeight(), theme::hover);

    p.save();
    if (!m_model->isHit(row)) p.setOpacity(0.3);
    const qreal cy = y + rowHeight() / 2.0;

    const int rw = refsWidth();
    const auto &refs = m_model->refs(row);
    const int dw = dateWidth(), aw = authorWidth();
    const qreal right = w - dw - aw - 12;
    qreal x = rw + gw + 6;
    if (rw && !refs.isEmpty()) {
        // left column: the most relevant ref as a chip, "+N" for the rest, and a
        // thin line in the lane color from the chip to its commit
        int best = 0;
        for (int i = 0; i < refs.size(); ++i) {
            const auto &g = refs[i];
            const int score = g.current * 8 + g.head * 4 + g.local * 2 + !g.tag;
            const auto &b = refs[best];
            if (score > b.current * 8 + b.head * 4 + b.local * 2 + !b.tag) best = i;
        }
        const QFont mf = theme::uiFont(11, true);
        const QString more = refs.size() > 1 ? QStringLiteral("+%1").arg(refs.size() - 1) : QString();
        const int moreW = more.isEmpty() ? 0 : QFontMetrics(mf).horizontalAdvance(more) + 12;
        RefGroup g = refs[best];
        if (g.remote && !g.local && g.name.contains(QLatin1Char('/'))) g.name = g.name.section(QLatin1Char('/'), 1); // the cloud icon says "remote"
        const qreal cw = paintChip(p, g, lane, 10, cy, rw - 22 - moreW);
        qreal lx = 10 + cw;
        if (!more.isEmpty()) {
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF mr(lx + 4, cy - 9, moreW - 4, 18);
            p.setPen(Qt::NoPen);
            p.setBrush(theme::field);
            p.drawRoundedRect(mr, 9, 9);
            p.setFont(mf);
            p.setPen(theme::dim);
            p.drawText(mr, Qt::AlignCenter, more);
            lx = mr.right();
        }
        const qreal nodeX = rw + 10 + laneWidth() * r->col + laneWidth() / 2.0 - nodeSize() / 2.0;
        QColor lc = lane;
        lc.setAlpha(170);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRectF(lx + 3, cy - 0.75, std::max<qreal>(0, nodeX - lx - 3), 1.5), lc);
    }

    paintGraph(p, row, QRectF(rw, y, gw, rowHeight()));

    // narrow window: refs inline before the message
    for (int i = 0; !rw && i < refs.size(); ++i) {
        const qreal room = right - x - 140; // always leave room for the message
        if (room < 60) break;
        const bool last = i == refs.size() - 1;
        const qreal cw = paintChip(p, refs[i], lane, x, cy, last ? room : std::min<qreal>(room, 220));
        if (cw == 0) break;
        x += cw + 6;
        if (!last && x > gw + 6 + 2 * 220) {
            p.setFont(theme::uiFont(11));
            p.setPen(theme::dim);
            const QString more = QStringLiteral("+%1").arg(refs.size() - i - 1);
            p.drawText(QRectF(x, y, 30, rowHeight()), Qt::AlignVCenter, more);
            x += QFontMetrics(p.font()).horizontalAdvance(more) + 8;
            break;
        }
    }
    if (!rw && !refs.isEmpty()) x += 4;

    const QFont f = theme::uiFont(13);
    const QFontMetrics fm(f);
    const int avail = int(right - x);
    if (r->wip) {
        QFont it = f;
        it.setItalic(true);
        p.setFont(it);
        p.setPen(theme::yellow);
        const int n = m_model->wipCount();
        p.drawText(QRect(int(x), y, avail, rowHeight()), Qt::AlignVCenter,
            QStringLiteral("Uncommitted changes · %1 %2").arg(n).arg(n == 1 ? QStringLiteral("file") : QStringLiteral("files")));
    } else if (avail > 20) {
        const QString summary = m_model->summary(row);
        const QString s = fm.elidedText(summary, Qt::ElideRight, avail);
        p.setFont(f);
        p.setPen(r->merge ? theme::dim : theme::text);
        p.drawText(QRect(int(x), y, avail, rowHeight()), Qt::AlignVCenter, s);
        const int used = fm.horizontalAdvance(s);
        const QString body = m_model->body(row);
        if (!body.isEmpty() && s == summary && used + 40 < avail) {
            p.setPen(theme::faint);
            const int bx = int(x) + used + 12;
            p.drawText(QRect(bx, y, avail - used - 12, rowHeight()), Qt::AlignVCenter, fm.elidedText(body, Qt::ElideRight, avail - used - 12));
        }
    }

    // author, date
    p.setFont(theme::uiFont(12));
    p.setPen(theme::dim);
    if (aw && !r->wip)
        p.drawText(QRect(w - dw - aw - 4, y, aw - 8, rowHeight()), Qt::AlignVCenter,
            QFontMetrics(p.font()).elidedText(m_model->authorOf(r), Qt::ElideRight, aw - 8));
    if (dw && !r->wip) p.drawText(QRect(w - dw - 4, y, dw - 10, rowHeight()), Qt::AlignVCenter | Qt::AlignRight, relativeTime(r->time));
    p.restore();
}

// ---------- input ----------

void CommitView::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    const int r = rowAt(int(e->position().y()));
    if (r >= 0) setCurrent(r, false);
}

void CommitView::contextMenuEvent(QContextMenuEvent *e) {
    const int r = rowAt(e->pos().y());
    if (r < 0) return;
    setCurrent(r, false);
    emit menuRequested(r, e->globalPos());
}

void CommitView::mouseMoveEvent(QMouseEvent *e) {
    const int r = rowAt(int(e->position().y()));
    if (r != m_hover) {
        m_hover = r;
        viewport()->update();
    }
}

void CommitView::leaveEvent(QEvent *) {
    m_hover = -1;
    viewport()->update();
}

void CommitView::keyPressEvent(QKeyEvent *e) {
    const int page = std::max(1, viewport()->height() / rowHeight());
    switch (e->key()) {
    case Qt::Key_Down:
    case Qt::Key_J: setCurrent(m_current + 1); break;
    case Qt::Key_Up:
    case Qt::Key_K: setCurrent(std::max(0, m_current - 1)); break;
    case Qt::Key_PageDown: setCurrent(m_current + page); break;
    case Qt::Key_PageUp: setCurrent(std::max(0, m_current - page)); break;
    case Qt::Key_Home: setCurrent(0); break;
    case Qt::Key_End: setCurrent(m_model->count() - 1); break;
    default: QAbstractScrollArea::keyPressEvent(e); return;
    }
}

bool CommitView::viewportEvent(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int r = rowAt(he->pos().y());
        const CommitRow *row = m_model->row(r);
        QString tip;
        if (row && !row->wip) {
            const int x = he->pos().x();
            if (dateWidth() && x > viewport()->width() - dateWidth() - 4)
                tip = QLocale().toString(QDateTime::fromSecsSinceEpoch(row->time), QLocale::LongFormat);
            else if (x < refsWidth()) {
                QStringList refs;
                for (const auto &g : m_model->refs(r)) refs << g.name + (g.local && g.remote ? QStringLiteral("  (local + remote)") : g.remote ? QStringLiteral("  (remote)")
                                                                                                                                  : g.tag      ? QStringLiteral("  (tag)")
                                                                                                                                               : QString());
                tip = refs.join(QLatin1Char('\n'));
            } else if (x > refsWidth() + m_gw) {
                QStringList refs;
                for (const auto &g : m_model->refs(r)) refs << g.name;
                tip = m_model->summary(r);
                if (!refs.isEmpty()) tip = refs.join(QStringLiteral(", ")) + QStringLiteral("\n") + tip;
            } else
                tip = m_model->authorOf(row);
        }
        if (!tip.isEmpty())
            QToolTip::showText(he->globalPos(), tip, viewport());
        else
            QToolTip::hideText();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(e);
}
