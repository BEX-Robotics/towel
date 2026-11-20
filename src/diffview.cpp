#include "diffview.h"
#include "theme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kNumW = 46;      // one line-number column
constexpr int kHunkH = 26;
constexpr int kPad = 2;        // vertical padding per line row
}

DiffArea::DiffArea(QWidget *parent) : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    m_font = theme::monoFont(13);
    m_bold = m_font;
    m_bold.setBold(true);
    m_italic = m_font;
    m_italic.setItalic(true);
    const QFontMetricsF fm(m_font);
    m_cw = fm.horizontalAdvance(QLatin1Char('M'));
    m_lh = int(std::ceil(fm.height()));
    verticalScrollBar()->setSingleStep(m_lh * 3);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
}

void DiffArea::setDoc(const DiffDoc *doc, bool loading)
{
    m_doc = doc;
    m_hoverUnit = -1;
    if (m_keepPlace) {
        // reload after staging: keep showing the old content until the new one
        // arrives, then stay at the same scroll offset and hunk index
        const int sv = verticalScrollBar()->value(), cur = m_current;
        m_loading = false;
        rebuildRows();
        verticalScrollBar()->setValue(sv);
        if (!loading) {
            m_keepPlace = false;
            setCurrent(m_units.empty() ? -1 : std::min(cur, int(m_units.size()) - 1), true);
        }
        return;
    }
    m_loading = loading;
    rebuildRows();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    m_current = m_units.empty() ? -1 : 0;
}

void DiffArea::setStaging(int mode)
{
    m_staging = mode;
    viewport()->update();
}

void DiffArea::buildUnits()
{
    m_units.clear();
    int lastChange = -1, ctx = 0;
    for (int i = 0; i < int(m_rows.size()); ++i) {
        const Row &r = m_rows[i];
        if (r.kind == 0) { // hunk header always starts a new unit
            lastChange = -1;
            continue;
        }
        if (r.kind == 1) {
            ++ctx;
            continue;
        }
        if (lastChange < 0 || ctx > 6) m_units.push_back({i, i, {}, {}});
        Unit &u = m_units.back();
        u.last = i;
        for (const DocLine *l : {r.a, r.b}) {
            if (!l) continue;
            if (l->t == '-') u.del.insert(l->o);
            else if (l->t == '+') u.add.insert(l->n);
        }
        lastChange = i;
        ctx = 0;
    }
}

int DiffArea::rowAtY(int y) const
{
    if (m_rows.empty()) return -1;
    auto it = std::upper_bound(m_y.begin(), m_y.end() - 1, y + verticalScrollBar()->value());
    const int r = int(it - m_y.begin()) - 1;
    return r >= 0 && r < int(m_rows.size()) ? r : -1;
}

int DiffArea::unitAtRow(int row) const
{
    if (row < 0) return -1;
    for (int u = 0; u < int(m_units.size()); ++u) {
        // a unit also owns the context right above it, so its button is reachable
        const int top = u > 0 ? std::max(m_units[u - 1].last + 1, m_units[u].first - 3) : 0;
        if (row >= top && row <= m_units[u].last) return u;
    }
    return -1;
}

QRect DiffArea::buttonRect(int u) const
{
    if (u < 0 || u >= int(m_units.size()) || !m_staging) return {};
    const QFontMetrics fm(theme::uiFont(11, true));
    const QString t = m_staging == 1 ? QStringLiteral("Stage hunk") : QStringLiteral("Unstage hunk");
    const int w = fm.horizontalAdvance(t) + 20, h = 20;
    // sit on the hunk header when this is the first unit after it, else on the first change
    int row = m_units[u].first;
    const int floor = u > 0 ? m_units[u - 1].last : -1;
    for (int r = row - 1; r > floor; --r)
        if (m_rows[r].kind == 0) {
            row = r;
            break;
        }
    const int top = m_y[row] - verticalScrollBar()->value();
    const int off = m_rows[row].kind == 0 ? (kHunkH - h) / 2 : 1;
    return QRect(viewport()->width() - w - 12, top + off, w, h);
}

void DiffArea::setCurrent(int u, bool scroll)
{
    m_current = u;
    if (scroll && u >= 0 && u < int(m_units.size())) {
        const int y0 = m_y[m_units[u].first], y1 = m_y[m_units[u].last + 1];
        const int sv = verticalScrollBar()->value(), vh = viewport()->height();
        const int margin = 3 * m_lh;
        if (y0 - margin < sv || y1 > sv + vh) // not fully in view: put it near the top
            verticalScrollBar()->setValue(y1 - y0 + 2 * margin > vh ? y0 - margin : std::max(0, y0 - vh / 3));
    }
    viewport()->update();
}

void DiffArea::applyCurrent()
{
    if (!m_staging || m_current < 0 || m_current >= int(m_units.size())) return;
    const Unit &u = m_units[m_current];
    emit applyHunk(u.del, u.add);
}

void DiffArea::keyPressEvent(QKeyEvent *e)
{
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return QAbstractScrollArea::keyPressEvent(e);
    switch (e->key()) {
    case Qt::Key_J:
    case Qt::Key_K: {
        if (m_units.empty()) return;
        const int d = e->key() == Qt::Key_J ? 1 : -1;
        setCurrent(std::clamp(m_current + d, 0, int(m_units.size()) - 1), true);
        return;
    }
    case Qt::Key_S:
        if (m_staging == 1) applyCurrent();
        return;
    case Qt::Key_U:
        if (m_staging == 2) applyCurrent();
        return;
    default:
        QAbstractScrollArea::keyPressEvent(e);
    }
}

void DiffArea::mousePressEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    const int u = unitAtRow(rowAtY(pos.y()));
    if (u >= 0 && buttonRect(u).contains(pos)) {
        setCurrent(u, false);
        return applyCurrent();
    }
    if (u >= 0) setCurrent(u, false);
    QAbstractScrollArea::mousePressEvent(e);
}

void DiffArea::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    const int u = m_staging ? unitAtRow(rowAtY(pos.y())) : -1;
    if (u != m_hoverUnit) {
        m_hoverUnit = u;
        viewport()->update();
    }
    viewport()->setCursor(u >= 0 && buttonRect(u).contains(pos) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QAbstractScrollArea::mouseMoveEvent(e);
}

void DiffArea::leaveEvent(QEvent *e)
{
    m_hoverUnit = -1;
    viewport()->update();
    QAbstractScrollArea::leaveEvent(e);
}

void DiffArea::setSplit(bool s)
{
    if (m_split == s) return;
    m_split = s;
    rebuildRows();
}

void DiffArea::setWrap(bool w)
{
    if (m_wrap == w) return;
    m_wrap = w;
    relayout();
}

void DiffArea::rebuildRows()
{
    m_rows.clear();
    if (m_doc && !m_loading) {
        for (const DocHunk &h : m_doc->hunks) {
            m_rows.push_back({0, nullptr, nullptr, &h.header});
            const size_t n = h.lines.size();
            if (!m_split) {
                for (const DocLine &l : h.lines) m_rows.push_back({l.t == ' ' ? 1 : 2, &l, nullptr, nullptr});
                continue;
            }
            size_t i = 0;
            while (i < n) {
                if (h.lines[i].t == ' ') {
                    m_rows.push_back({1, &h.lines[i], &h.lines[i], nullptr});
                    ++i;
                    continue;
                }
                size_t j = i;
                while (j < n && h.lines[j].t == '-') ++j;
                size_t k = j;
                while (k < n && h.lines[k].t == '+') ++k;
                const size_t nd = j - i, na = k - j;
                for (size_t p = 0; p < std::max(nd, na); ++p)
                    m_rows.push_back({2, p < nd ? &h.lines[i + p] : nullptr, p < na ? &h.lines[j + p] : nullptr, nullptr});
                i = std::max(k, i + 1);
            }
        }
    }
    buildUnits();
    relayout();
}

std::vector<int> DiffArea::breaks(const QString &text, int cols)
{
    // wrap at the last space within the line width, or hard-break long words
    std::vector<int> out{0};
    int start = 0;
    const int n = int(text.size());
    while (n - start > cols) {
        int cut = start + cols;
        for (int k = cut; k > start + cols / 2; --k)
            if (text.at(k - 1) == QLatin1Char(' ')) {
                cut = k;
                break;
            }
        out.push_back(cut);
        start = cut;
    }
    return out;
}

int DiffArea::visualLines(const DocLine *l, int cols) const
{
    if (!l || !(m_wrap || m_split) || l->text.size() <= cols) return 1;
    return int(breaks(l->text, cols).size());
}

void DiffArea::relayout()
{
    const int vw = viewport()->width();
    const bool wrapping = m_wrap || m_split;
    if (m_split) m_cols = std::max(10, int((vw / 2 - kNumW - 16) / m_cw));
    else m_cols = std::max(10, int((vw - 2 * kNumW - 34) / m_cw));

    m_y.assign(m_rows.size() + 1, 0);
    int y = 0;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        m_y[i] = y;
        const Row &r = m_rows[i];
        if (r.kind == 0) y += kHunkH;
        else if (m_split) y += std::max(visualLines(r.a, m_cols), visualLines(r.b, m_cols)) * m_lh + kPad;
        else y += visualLines(r.a, m_cols) * m_lh + kPad;
    }
    m_y.back() = y;

    const int vh = viewport()->height();
    verticalScrollBar()->setPageStep(vh);
    verticalScrollBar()->setRange(0, std::max(0, y - vh));
    const int contentW = (wrapping || !m_doc) ? vw : int(2 * kNumW + 34 + m_doc->maxCols * m_cw + 40);
    horizontalScrollBar()->setPageStep(vw);
    horizontalScrollBar()->setRange(0, std::max(0, contentW - vw));
    viewport()->update();
}

void DiffArea::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    relayout();
}

void DiffArea::paintText(QPainter &p, const DocLine &l, int from, int to, qreal x, qreal top, const QColor &markBg) const
{
    to = std::min(to, int(l.text.size()));
    if (from >= to) return;
    for (const auto &m : l.marks) {
        const int a = std::max(m.first, from), b = std::min(m.second, to);
        if (a < b) p.fillRect(QRectF(x + (a - from) * m_cw, top, (b - a) * m_cw, m_lh), markBg);
    }
    const qreal base = top + QFontMetricsF(m_font).ascent();
    int pos = from;
    auto draw = [&](int a, int b, const ColorSpan *s) {
        if (a >= b) return;
        p.setFont(s && s->bold ? m_bold : s && s->italic ? m_italic : m_font);
        p.setPen(s && s->color ? QColor(s->color) : theme::text);
        p.drawText(QPointF(x + (a - from) * m_cw, base), l.text.mid(a, b - a));
    };
    for (const ColorSpan &s : l.spans) {
        const int a = std::max(s.off, from), b = std::min(s.off + s.len, to);
        if (b <= pos) continue;
        if (a >= to) break;
        draw(pos, std::max(pos, a), nullptr);
        draw(std::max(pos, a), b, &s);
        pos = std::max(pos, b);
    }
    draw(pos, to, nullptr);
}

void DiffArea::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const int vw = viewport()->width(), vh = viewport()->height();
    p.fillRect(0, 0, vw, vh, theme::bg);

    auto centerText = [&](const QString &t) {
        p.setPen(theme::dim);
        p.setFont(theme::uiFont(14));
        p.drawText(QRect(0, 0, vw, vh), Qt::AlignCenter, t);
    };
    if (!m_doc) return;
    if (m_loading) return centerText(QStringLiteral("Loading…"));
    const DiffData &meta = m_doc->meta;
    if (!meta.error.isEmpty()) return centerText(meta.error);
    if (meta.image) {
        const int pad = 16, w = (vw - 3 * pad) / 2, h = vh - 2 * pad;
        const QImage *imgs[2] = {&meta.oldImage, &meta.newImage};
        const char *labels[2] = {"Before", "After"};
        const QColor cols[2] = {theme::red, theme::green};
        for (int i = 0; i < 2; ++i) {
            const QRect box(pad + i * (w + pad), pad, w, h);
            p.setPen(QColor(cols[i].red(), cols[i].green(), cols[i].blue(), 120));
            p.setBrush(QColor(0x14, 0x16, 0x1a));
            p.drawRoundedRect(box, 6, 6);
            p.setFont(theme::uiFont(12));
            p.setPen(theme::dim);
            const QImage &img = *imgs[i];
            p.drawText(box.adjusted(12, 8, 0, 0), Qt::AlignTop | Qt::AlignLeft,
                       QString::fromLatin1(labels[i]) + (img.isNull() ? QString() : QStringLiteral("  ·  %1 × %2").arg(img.width()).arg(img.height())));
            if (img.isNull()) {
                p.setPen(theme::faint);
                p.drawText(box, Qt::AlignCenter, QStringLiteral("(none)"));
                continue;
            }
            const QRect inner = box.adjusted(30, 34, -30, -30);
            QSize s = img.size();
            if (s.width() > inner.width() || s.height() > inner.height()) s.scale(inner.size(), Qt::KeepAspectRatio);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRect(inner.center() - QPoint(s.width() / 2, s.height() / 2), s), img);
        }
        return;
    }
    if (meta.binary) return centerText(QStringLiteral("Binary file, no text diff"));
    if (m_rows.empty()) return centerText(QStringLiteral("No content changes (mode or rename only)"));

    const int sv = verticalScrollBar()->value(), sh = horizontalScrollBar()->value();
    auto it = std::upper_bound(m_y.begin(), m_y.end() - 1, sv);
    size_t first = size_t(std::max<ptrdiff_t>(0, (it - m_y.begin()) - 1));
    const QFont numFont = theme::monoFont(12);

    auto bgFor = [](char t) { return t == '+' ? theme::addBg : t == '-' ? theme::delBg : theme::bg; };
    auto gutterFor = [](char t) { return t == '+' ? theme::addGutter : t == '-' ? theme::delGutter : theme::gutter; };
    auto markFor = [](char t) { return t == '+' ? theme::addMark : theme::delMark; };

    for (size_t i = first; i < m_rows.size(); ++i) {
        const int top = m_y[i] - sv;
        if (top > vh) break;
        const int h = m_y[i + 1] - m_y[i];
        const Row &r = m_rows[i];
        if (r.kind == 0) {
            p.fillRect(0, top, vw, h, theme::hunkBg);
            p.setFont(theme::monoFont(12));
            p.setPen(QColor(0x7f, 0x9c, 0xc4));
            p.drawText(QRect((m_split ? kNumW : 2 * kNumW) + 12 - (m_split || m_wrap ? 0 : sh), top, vw * 4, h), Qt::AlignVCenter, *r.header);
            continue;
        }
        auto paintSide = [&](const DocLine *l, int x0, int width, bool newSide, bool numbersBoth) {
            const char t = l ? l->t : 0;
            p.fillRect(x0, top, width, h, l ? bgFor(t) : theme::filler);
            const int gw = numbersBoth ? 2 * kNumW : kNumW;
            p.fillRect(x0, top, gw, h, l ? gutterFor(t) : theme::filler);
            if (!l) return;
            p.setFont(numFont);
            p.setPen(theme::faint);
            if (numbersBoth) {
                if (l->o >= 0) p.drawText(QRect(x0, top + 1, kNumW - 8, m_lh), Qt::AlignRight | Qt::AlignVCenter, QString::number(l->o));
                if (l->n >= 0) p.drawText(QRect(x0 + kNumW, top + 1, kNumW - 8, m_lh), Qt::AlignRight | Qt::AlignVCenter, QString::number(l->n));
            } else {
                const int no = newSide ? l->n : l->o;
                if (no >= 0) p.drawText(QRect(x0, top + 1, kNumW - 8, m_lh), Qt::AlignRight | Qt::AlignVCenter, QString::number(no));
            }
            int textX = x0 + gw + 8;
            if (numbersBoth) {
                p.setFont(m_font);
                if (t != ' ') {
                    p.setPen(t == '+' ? theme::green : theme::red);
                    p.drawText(QPointF(x0 + gw + 6, top + 1 + QFontMetricsF(m_font).ascent()), t == '+' ? QStringLiteral("+") : QStringLiteral("−"));
                }
                textX = x0 + gw + 22;
            }
            p.save();
            p.setClipRect(QRect(x0 + gw, top, width - gw, h));
            if (m_wrap || m_split) {
                const std::vector<int> b = breaks(l->text, m_cols);
                for (size_t v = 0; v < b.size(); ++v)
                    paintText(p, *l, b[v], v + 1 < b.size() ? b[v + 1] : int(l->text.size()), textX, top + 1 + int(v) * m_lh, markFor(t));
            } else {
                // horizontal scroll: start at the first visible column
                const int c0 = int(sh / m_cw);
                paintText(p, *l, c0, c0 + int(width / m_cw) + 2, textX - (sh - c0 * m_cw), top + 1, markFor(t));
            }
            p.restore();
        };
        if (m_split) {
            const int half = vw / 2;
            paintSide(r.a, 0, half, false, false);
            paintSide(r.b, half, vw - half, true, false);
            p.fillRect(half - 1, top, 1, h, theme::border);
        } else {
            paintSide(r.a, 0, vw, r.a->t != '-', true);
        }
    }

    // current hunk marker and the stage/unstage button
    if (m_current >= 0 && m_current < int(m_units.size()) && (m_staging || m_units.size() > 1)) {
        const Unit &u = m_units[m_current];
        p.fillRect(0, m_y[u.first] - sv, 3, m_y[u.last + 1] - m_y[u.first], theme::accent);
    }
    for (int u : {m_current, m_hoverUnit}) {
        const QRect br = buttonRect(u);
        if (br.isNull() || br.bottom() < 0 || br.top() > vh) continue;
        p.setRenderHint(QPainter::Antialiasing);
        const bool hot = u == m_hoverUnit;
        const QColor c = m_staging == 1 ? theme::green : theme::red;
        p.setPen(Qt::NoPen);
        p.setBrush(hot ? c : QColor(0x27, 0x2b, 0x33, 235));
        p.drawRoundedRect(QRectF(br).adjusted(0.5, 0.5, -0.5, -0.5), br.height() / 2.0, br.height() / 2.0);
        p.setFont(theme::uiFont(11, true));
        p.setPen(hot ? QColor(0x12, 0x14, 0x18) : c);
        p.drawText(br, Qt::AlignCenter, m_staging == 1 ? QStringLiteral("Stage hunk") : QStringLiteral("Unstage hunk"));
        p.setRenderHint(QPainter::Antialiasing, false);
    }
}

// ---------- DiffView ----------

DiffView::DiffView(QWidget *parent) : QWidget(parent)
{
    auto *bar = new QWidget(this);
    bar->setFixedHeight(40);
    bar->setAutoFillBackground(true);
    QPalette pal = bar->palette();
    pal.setColor(QPalette::Window, theme::panel);
    bar->setPalette(pal);
    auto *hl = new QHBoxLayout(bar);
    hl->setContentsMargins(8, 0, 10, 0);
    hl->setSpacing(6);

    auto mk = [&](const QString &text, const QString &tip, bool checkable) {
        auto *b = new QToolButton(bar);
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    auto *back = mk(QStringLiteral("←"), QStringLiteral("Back to graph (Esc)"), false);
    connect(back, &QToolButton::clicked, this, &DiffView::closeRequested);
    hl->addWidget(back);
    m_mode = new QLabel(bar);
    m_mode->setStyleSheet(QStringLiteral("background:#272b33; color:#8d94a1; border-radius:4px; padding:2px 8px; font-size:11px; font-weight:bold;"));
    m_mode->hide();
    hl->addWidget(m_mode);
    m_status = new QLabel(bar);
    m_status->setFixedSize(20, 20);
    m_status->setAlignment(Qt::AlignCenter);
    hl->addWidget(m_status);
    m_path = new QLabel(bar);
    m_path->setTextFormat(Qt::RichText);
    m_path->setMinimumWidth(50);
    m_path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hl->addWidget(m_path, 1);
    m_stats = new QLabel(bar);
    m_stats->setTextFormat(Qt::RichText);
    hl->addWidget(m_stats);
    hl->addSpacing(12);

    m_hunks = mk(QStringLiteral("Hunks"), QString(), true);
    m_fullBtn = mk(QStringLiteral("Full file"), QString(), true);
    m_unified = mk(QStringLiteral("Unified"), QString(), true);
    m_splitBtn = mk(QStringLiteral("Split"), QString(), true);
    m_wrapBtn = mk(QStringLiteral("Wrap"), QStringLiteral("Wrap long lines"), true);
    auto *g1 = new QButtonGroup(this);
    g1->addButton(m_hunks);
    g1->addButton(m_fullBtn);
    auto *g2 = new QButtonGroup(this);
    g2->addButton(m_unified);
    g2->addButton(m_splitBtn);
    for (auto *b : {m_hunks, m_fullBtn}) hl->addWidget(b);
    hl->addSpacing(8);
    for (auto *b : {m_unified, m_splitBtn}) hl->addWidget(b);
    hl->addSpacing(8);
    hl->addWidget(m_wrapBtn);
    hl->addSpacing(8);
    auto *prev = mk(QStringLiteral("↑"), QStringLiteral("Previous file (Alt+↑)"), false);
    auto *next = mk(QStringLiteral("↓"), QStringLiteral("Next file (Alt+↓)"), false);
    connect(prev, &QToolButton::clicked, this, [this] { emit stepFile(-1); });
    connect(next, &QToolButton::clicked, this, [this] { emit stepFile(1); });
    hl->addWidget(prev);
    hl->addWidget(next);

    auto emitOpts = [this] {
        m_full = m_fullBtn->isChecked();
        m_area->setSplit(m_splitBtn->isChecked());
        m_area->setWrap(m_wrapBtn->isChecked());
        m_wrapBtn->setEnabled(!m_splitBtn->isChecked());
        emit optionsChanged(m_full, m_area->split(), m_area->wrap());
    };
    for (auto *b : {m_hunks, m_fullBtn, m_unified, m_splitBtn, m_wrapBtn}) connect(b, &QToolButton::clicked, this, emitOpts);

    m_area = new DiffArea(this);
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    vl->addWidget(bar);
    auto *line = new QWidget(this);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#2b2f37"));
    vl->addWidget(line);
    vl->addWidget(m_area, 1);
    setOptions(false, false, false);
}

void DiffView::setOptions(bool full, bool split, bool wrap)
{
    m_full = full;
    m_fullBtn->setChecked(full);
    m_hunks->setChecked(!full);
    m_splitBtn->setChecked(split);
    m_unified->setChecked(!split);
    m_wrapBtn->setChecked(wrap);
    m_wrapBtn->setEnabled(!split);
    m_area->setSplit(split);
    m_area->setWrap(wrap);
}

void DiffView::setDoc(const DiffDoc *doc, bool loading)
{
    m_area->setDoc(doc, loading);
    if (!doc) return;
    const DiffData &m = doc->meta;
    const QColor sc = theme::statusColor(m.status);
    m_status->setText(m.status);
    m_status->setStyleSheet(QStringLiteral("background: rgba(%1,%2,%3,46); color: %4; border-radius: 4px; font-weight: bold; font-size: 11px;")
                                .arg(sc.red()).arg(sc.green()).arg(sc.blue()).arg(sc.name()));
    const int slash = m.path.lastIndexOf(QLatin1Char('/'));
    QString html = QStringLiteral("<span style='color:#7c8390'>%1</span><b>%2</b>")
                       .arg(m.path.left(slash + 1).toHtmlEscaped(), m.path.mid(slash + 1).toHtmlEscaped());
    if (!m.oldPath.isEmpty()) html += QStringLiteral("&nbsp;&nbsp;<span style='color:#5c6370'>← %1</span>").arg(m.oldPath.toHtmlEscaped());
    m_path->setText(html);
    m_stats->setText(loading ? QString()
                             : QStringLiteral("<span style='color:#4cc26b'>+%1</span>&nbsp;&nbsp;<span style='color:#e5534b'>−%2</span>")
                                   .arg(doc->additions).arg(doc->deletions));
}

void DiffView::setModeLabel(const QString &mode)
{
    m_mode->setText(mode.toUpper());
    m_mode->setVisible(!mode.isEmpty());
}
