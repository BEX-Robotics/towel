#include "rebaseview.h"
#include "avatars.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {
constexpr int kRowH = 40;

QString label(const QVariantMap &m)
{
    const QString a = m.value("action").toString();
    if (a == QLatin1String("pick"))
        return m.value("message").toString().trimmed() != m.value("original").toString().trimmed() ? QStringLiteral("Reword") : QStringLiteral("Pick");
    if (a == QLatin1String("squash")) return QStringLiteral("Squash");
    if (a == QLatin1String("fixup")) return QStringLiteral("Fixup");
    return QStringLiteral("Drop");
}

QColor color(const QString &label)
{
    if (label == QLatin1String("Reword")) return theme::yellow;
    if (label == QLatin1String("Squash")) return theme::lane(4);
    if (label == QLatin1String("Fixup")) return theme::lane(5);
    if (label == QLatin1String("Drop")) return theme::red;
    return theme::dim;
}
} // namespace

RebaseList::RebaseList(QWidget *parent) : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    connect(AvatarCache::instance(), &AvatarCache::ready, viewport(), qOverload<>(&QWidget::update));
}

void RebaseList::setItems(const QVariantList &items)
{
    m_items = items;
    for (int i = 0; i < m_items.size(); ++i) {
        QVariant &v = m_items[i];
        QVariantMap m = v.toMap();
        m.insert("action", QStringLiteral("pick"));
        m.insert("index", i);
        v = m;
    }
    m_cur = 0;
    relayout();
    emit currentChanged(m_cur);
    emit planChanged();
}

void RebaseList::relayout()
{
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setSingleStep(kRowH);
    verticalScrollBar()->setRange(0, std::max(0, int(m_items.size()) * kRowH - viewport()->height()));
    viewport()->update();
}

void RebaseList::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    relayout();
}

void RebaseList::changed()
{
    viewport()->update();
    emit planChanged();
}

int RebaseList::rowAt(int y) const
{
    const int r = (y + verticalScrollBar()->value()) / kRowH;
    return r >= 0 && r < int(m_items.size()) ? r : -1;
}

QRect RebaseList::pillRect(int row) const
{
    return QRect(36, row * kRowH - verticalScrollBar()->value() + (kRowH - 22) / 2, 70, 22);
}

void RebaseList::ensureVisible(int row)
{
    const int y = row * kRowH, sv = verticalScrollBar()->value(), vh = viewport()->height();
    if (y < sv) verticalScrollBar()->setValue(y);
    else if (y + kRowH > sv + vh) verticalScrollBar()->setValue(y + kRowH - vh);
}

void RebaseList::setCurrent(int i)
{
    if (m_items.isEmpty()) return;
    i = std::clamp(i, 0, int(m_items.size()) - 1);
    m_cur = i;
    ensureVisible(i);
    viewport()->update();
    emit currentChanged(i);
}

void RebaseList::setAction(const QString &action)
{
    if (m_cur < 0 || m_cur >= m_items.size()) return;
    QVariantMap m = m_items[m_cur].toMap();
    m.insert("action", action);
    m_items[m_cur] = m;
    changed();
    emit currentChanged(m_cur);
}

void RebaseList::move(int delta)
{
    const int to = m_cur + delta;
    if (to < 0 || to >= m_items.size()) return;
    m_items.move(m_cur, to);
    setCurrent(to);
    changed();
}

bool RebaseList::event(QEvent *e)
{
    // Alt+↑/↓ move rows here; don't let the window's "previous/next file" shortcut take them
    if (e->type() == QEvent::ShortcutOverride) {
        auto *k = static_cast<QKeyEvent *>(e);
        if ((k->modifiers() & Qt::AltModifier) && (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down)) {
            e->accept();
            return true;
        }
    }
    return QAbstractScrollArea::event(e);
}

void RebaseList::keyPressEvent(QKeyEvent *e)
{
    const bool alt = e->modifiers() & Qt::AltModifier;
    switch (e->key()) {
    case Qt::Key_Up:
    case Qt::Key_K: return alt ? move(-1) : setCurrent(m_cur - 1);
    case Qt::Key_Down:
    case Qt::Key_J: return alt ? move(1) : setCurrent(m_cur + 1);
    case Qt::Key_P: return setAction(QStringLiteral("pick"));
    case Qt::Key_S: return setAction(QStringLiteral("squash"));
    case Qt::Key_F: return setAction(QStringLiteral("fixup"));
    case Qt::Key_D:
    case Qt::Key_Delete: return setAction(QStringLiteral("drop"));
    default: QAbstractScrollArea::keyPressEvent(e);
    }
}

void RebaseList::mousePressEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    const int r = rowAt(pos.y());
    if (r < 0 || e->button() != Qt::LeftButton) return;
    setCurrent(r);
    if (pillRect(r).contains(pos)) {
        QMenu menu(this);
        const QList<QPair<QString, QString>> acts{{QStringLiteral("pick"), QStringLiteral("Pick\tP")},
                                                  {QStringLiteral("squash"), QStringLiteral("Squash into the commit below\tS")},
                                                  {QStringLiteral("fixup"), QStringLiteral("Fixup into below, drop this message\tF")},
                                                  {QStringLiteral("drop"), QStringLiteral("Drop\tD")}};
        for (const auto &[a, text] : acts) menu.addAction(text, this, [this, a] { setAction(a); });
        menu.exec(viewport()->mapToGlobal(pillRect(r).bottomLeft()));
        return;
    }
    m_drag = r;
    m_pressY = pos.y();
    m_dragY = pos.y();
    m_dragging = false;
}

void RebaseList::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (m_drag < 0) {
        viewport()->setCursor(rowAt(pos.y()) >= 0 && (pos.x() < 32 || pillRect(rowAt(pos.y())).contains(pos)) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        return;
    }
    if (!m_dragging && std::abs(pos.y() - m_pressY) < 4) return;
    m_dragging = true;
    viewport()->setCursor(Qt::ClosedHandCursor);
    m_dragY = pos.y();
    const int vh = viewport()->height();
    if (pos.y() < 10) verticalScrollBar()->setValue(verticalScrollBar()->value() - kRowH / 2);
    else if (pos.y() > vh - 10) verticalScrollBar()->setValue(verticalScrollBar()->value() + kRowH / 2);
    // move the dragged row as soon as the pointer crosses into another row
    const int target = std::clamp((m_dragY + verticalScrollBar()->value()) / kRowH, 0, int(m_items.size()) - 1);
    if (target != m_drag) {
        m_items.move(m_drag, target);
        m_drag = target;
        m_cur = target;
        emit currentChanged(target);
        emit planChanged();
    }
    viewport()->update();
}

void RebaseList::mouseReleaseEvent(QMouseEvent *)
{
    m_drag = -1;
    m_dragging = false;
    viewport()->unsetCursor();
    viewport()->update();
}

void RebaseList::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const int vw = viewport()->width(), vh = viewport()->height(), sv = verticalScrollBar()->value();
    p.fillRect(0, 0, vw, vh, theme::graphBg);
    const QFont ui = theme::uiFont(13), small = theme::uiFont(11, true), mono = theme::monoFont(12);
    const int first = sv / kRowH;
    for (int i = first; i < m_items.size(); ++i) {
        const int y = i * kRowH - sv;
        if (y > vh) break;
        const QVariantMap m = m_items[i].toMap();
        const QString a = m.value("action").toString(), lab = label(m);
        const bool folded = a == QLatin1String("squash") || a == QLatin1String("fixup"), drop = a == QLatin1String("drop");
        const QRect row(0, y, vw, kRowH);
        if (i == m_cur) {
            p.fillRect(row, m_dragging ? theme::field : theme::selection);
            p.fillRect(0, y, 3, kRowH, theme::accent);
        }
        p.fillRect(0, y + kRowH - 1, vw, 1, theme::border);
        // grip
        p.setPen(Qt::NoPen);
        p.setBrush(theme::faint);
        for (int dx : {0, 5})
            for (int dy : {-5, 0, 5}) p.drawEllipse(QPointF(16 + dx, y + kRowH / 2.0 + dy), 1.4, 1.4);
        // action pill
        p.setRenderHint(QPainter::Antialiasing);
        const QRect pr = pillRect(i);
        const QColor c = color(lab);
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 40));
        p.setPen(QColor(c.red(), c.green(), c.blue(), 150));
        p.drawRoundedRect(QRectF(pr).adjusted(0.5, 0.5, -0.5, -0.5), 11, 11);
        p.setFont(small);
        p.setPen(c);
        p.drawText(pr, Qt::AlignCenter, lab + QStringLiteral(" ▾"));
        // avatar + sha on the right
        const int ax = vw - 108;
        AvatarCache::paint(&p, QRectF(ax, y + 10, 20, 20), m.value("email").toString(), m.value("author").toString(), QColor(), 0);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setFont(mono);
        p.setPen(theme::faint);
        p.drawText(QRect(ax + 28, y, 70, kRowH), Qt::AlignVCenter, m.value("id").toString().left(7));
        // summary; folded rows are indented with an arrow into the commit below
        int tx = pr.right() + 14;
        if (folded) {
            p.setPen(c);
            p.setFont(ui);
            p.drawText(QRect(tx, y, 20, kRowH), Qt::AlignVCenter, QStringLiteral("↓"));
            tx += 20;
        }
        QFont f = ui;
        f.setStrikeOut(drop);
        p.setFont(f);
        p.setPen(drop ? theme::faint : a == QLatin1String("fixup") ? theme::dim : theme::text);
        const QString summary = m.value("message").toString().section(QLatin1Char('\n'), 0, 0);
        p.drawText(QRect(tx, y, ax - tx - 12, kRowH), Qt::AlignVCenter, QFontMetrics(f).elidedText(summary, Qt::ElideRight, ax - tx - 12));
    }
}

// ---------- RebaseView ----------

RebaseView::RebaseView(QWidget *parent) : QWidget(parent)
{
    auto *bar = new QWidget(this);
    bar->setFixedHeight(44);
    bar->setAutoFillBackground(true);
    QPalette pal = bar->palette();
    pal.setColor(QPalette::Window, theme::panel);
    bar->setPalette(pal);
    auto *hl = new QHBoxLayout(bar);
    hl->setContentsMargins(14, 0, 12, 0);
    m_title = new QLabel(bar);
    m_title->setFont(theme::uiFont(14, true));
    hl->addWidget(m_title, 1);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), bar);
    m_start = new QPushButton(bar);
    m_start->setProperty("primary", true);
    for (auto *b : {cancel, m_start}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        hl->addWidget(b);
    }
    connect(cancel, &QPushButton::clicked, this, &RebaseView::cancel);
    connect(m_start, &QPushButton::clicked, this, [this] { emit run(m_base, m_list->items()); });

    m_list = new RebaseList(this);

    auto *bottom = new QWidget(this);
    bottom->setAutoFillBackground(true);
    bottom->setPalette(pal);
    auto *bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(14, 10, 14, 12);
    bl->setSpacing(6);
    m_summary = new QLabel(bottom);
    m_summary->setFont(theme::uiFont(12));
    m_summary->setWordWrap(true);
    m_msgHint = new QLabel(bottom);
    m_msgHint->setFont(theme::uiFont(11));
    QPalette hp = m_msgHint->palette();
    hp.setColor(QPalette::WindowText, theme::faint);
    m_msgHint->setPalette(hp);
    m_msg = new QPlainTextEdit(bottom);
    m_msg->setFont(theme::uiFont(13));
    m_msg->setFixedHeight(120);
    m_msg->setTabChangesFocus(true);
    bl->addWidget(m_summary);
    bl->addWidget(m_msgHint);
    bl->addWidget(m_msg);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    vl->addWidget(bar);
    vl->addWidget(m_list, 1);
    vl->addWidget(bottom);

    connect(m_list, &RebaseList::currentChanged, this, &RebaseView::onCurrent);
    connect(m_list, &RebaseList::planChanged, this, &RebaseView::updateSummary);
    connect(m_msg, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loadingMsg) return;
        const int r = m_list->current();
        QVariantMap m = m_list->items()[r].toMap();
        m.insert("message", m_msg->toPlainText());
        m_list->items()[r] = m;
        m_list->changed();
    });
}

void RebaseView::start(const QString &base, const QString &branch, const QVariantList &items)
{
    m_base = base;
    m_title->setText(QStringLiteral("Rebase %1 · %2 commits onto %3")
                         .arg(branch)
                         .arg(items.size())
                         .arg(base.isEmpty() ? QStringLiteral("the root") : base.left(7)));
    m_list->setItems(items);
    m_list->setFocus();
}

void RebaseView::onCurrent(int row)
{
    if (row < 0 || row >= m_list->items().size()) return;
    const QVariantMap m = m_list->items()[row].toMap();
    const QString a = m.value("action").toString();
    const bool editable = a == QLatin1String("pick");
    m_loadingMsg = true;
    m_msg->setPlainText(m.value("message").toString());
    m_loadingMsg = false;
    m_msg->setReadOnly(!editable);
    m_msgHint->setText(editable ? QStringLiteral("Message (edit to reword)")
                       : a == QLatin1String("squash") ? QStringLiteral("This message is appended to the commit below.")
                       : a == QLatin1String("fixup")  ? QStringLiteral("This message is discarded; the changes go into the commit below.")
                                                      : QStringLiteral("This commit and its changes are dropped."));
}

void RebaseView::updateSummary()
{
    const QVariantList &items = m_list->items();
    int drops = 0, folds = 0, rewords = 0;
    bool moved = false, bad = false;
    QString lastKept; // walking newest → oldest, the oldest kept row must be a pick
    for (int i = 0; i < items.size(); ++i) {
        const QVariantMap m = items[i].toMap();
        const QString a = m.value("action").toString();
        if (a == QLatin1String("drop")) ++drops;
        else {
            lastKept = a;
            if (a != QLatin1String("pick")) ++folds;
            else if (label(m) == QLatin1String("Reword")) ++rewords;
        }
        if (m.value("index", i).toInt() != i) moved = true;
    }
    bad = !lastKept.isEmpty() && lastKept != QLatin1String("pick");
    // order changed?
    QStringList parts;
    if (moved) parts << QStringLiteral("reordered");
    if (rewords) parts << QStringLiteral("%1 reworded").arg(rewords);
    if (folds) parts << QStringLiteral("%1 squashed").arg(folds);
    if (drops) parts << QStringLiteral("%1 dropped").arg(drops);
    QPalette p = m_summary->palette();
    if (bad) {
        m_summary->setText(QStringLiteral("The oldest commit can't be squashed: there is nothing below it to fold into."));
        p.setColor(QPalette::WindowText, theme::red);
    } else {
        m_summary->setText(parts.isEmpty() ? QStringLiteral("No changes yet. Drag rows to reorder (or Alt+↑/↓); P S F D set the action.")
                                           : QStringLiteral("Plan: ") + parts.join(QStringLiteral(", ")) + QStringLiteral("."));
        p.setColor(QPalette::WindowText, parts.isEmpty() ? theme::dim : theme::text);
    }
    m_summary->setPalette(p);
    m_start->setEnabled(!bad && !parts.isEmpty() && drops < items.size());
    m_start->setText(QStringLiteral("Start rebase"));
}
