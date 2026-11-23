#include "panels.h"
#include <QContextMenuEvent>
#include "avatars.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

namespace {
inline int kSideRowH() { return theme::z(26); }
inline int kSideHeadH() { return theme::z(30); }
inline int kFileRowH() { return theme::z(28); }

void setBg(QWidget *w, const QColor &c)
{
    w->setAutoFillBackground(true);
    QPalette p = w->palette();
    p.setColor(QPalette::Window, c);
    w->setPalette(p);
}

QLabel *label(QWidget *parent, int px, bool bold, const QColor &c)
{
    auto *l = new QLabel(parent);
    l->setFont(theme::uiFont(px, bold));
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, c);
    l->setPalette(p);
    return l;
}
} // namespace

// ---------- AvatarWidget ----------

AvatarWidget::AvatarWidget(int size, QWidget *parent) : QWidget(parent)
{
    setFixedSize(size, size);
    connect(AvatarCache::instance(), &AvatarCache::ready, this, qOverload<>(&QWidget::update));
}

void AvatarWidget::set(const QString &email, const QString &name, const QColor &ring)
{
    m_email = email;
    m_name = name;
    m_ring = ring;
    update();
}

void AvatarWidget::paintEvent(QPaintEvent *)
{
    if (m_email.isEmpty() && m_name.isEmpty()) return;
    QPainter p(this);
    AvatarCache::paint(&p, QRectF(rect()), m_email, m_name, m_ring, 2.0);
}

// ---------- SideList ----------

SideList::SideList(QWidget *parent) : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    connect(theme::Zoom::instance(), &theme::Zoom::changed, this, &SideList::rebuild);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void SideList::setData(const QVariantList &branches, const QVariantList &tags, const QVariantList &stashes)
{
    m_branches = branches;
    m_tags = tags;
    m_stashes = stashes;
    rebuild();
}

void SideList::setFilter(const QString &f)
{
    m_filter = f;
    rebuild();
}

void SideList::rebuild()
{
    m_entries.clear();
    const QString f = m_filter.toLower();
    auto add = [&](const QString &key, const QString &label, const QVariantList &items, const QString &kindFilter) {
        std::vector<Entry> shown;
        int total = 0;
        for (const QVariant &v : items) {
            const QVariantMap m = v.toMap();
            if (!kindFilter.isEmpty() && m.value("kind").toString() != kindFilter) continue;
            ++total;
            const QString name = m.value("name").toString();
            if (!f.isEmpty() && !name.toLower().contains(f)) continue;
            Entry e;
            e.name = name;
            e.id = m.value("id").toString();
            e.kind = m.value("kind").toString();
            e.current = m.value("current").toBool();
            e.ahead = m.value("ahead").toInt();
            e.behind = m.value("behind").toInt();
            e.index = m.value("index").toInt();
            shown.push_back(e);
        }
        if (total == 0) return;
        Entry h;
        h.header = true;
        h.key = key;
        h.label = label;
        h.count = int(shown.size());
        m_entries.push_back(h);
        if (m_collapsed.contains(key) && f.isEmpty()) return;
        m_entries.insert(m_entries.end(), shown.begin(), shown.end());
    };
    add(QStringLiteral("local"), QStringLiteral("LOCAL"), m_branches, QStringLiteral("local"));
    add(QStringLiteral("remote"), QStringLiteral("REMOTE"), m_branches, QStringLiteral("remote"));
    add(QStringLiteral("tag"), QStringLiteral("TAGS"), m_tags, QString());
    add(QStringLiteral("stash"), QStringLiteral("STASHES"), m_stashes, QString());
    int h = 0;
    for (const auto &e : m_entries) h += e.header ? kSideHeadH() : kSideRowH();
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setRange(0, std::max(0, h - viewport()->height()));
    viewport()->update();
}

void SideList::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    rebuild();
}

int SideList::rowAt(int y) const
{
    y += verticalScrollBar()->value();
    int top = 0;
    for (int i = 0; i < int(m_entries.size()); ++i) {
        const int h = m_entries[i].header ? kSideHeadH() : kSideRowH();
        if (y >= top && y < top + h) return i;
        top += h;
    }
    return -1;
}

void SideList::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const int w = viewport()->width();
    p.fillRect(viewport()->rect(), theme::sidebar);
    int y = -verticalScrollBar()->value();
    for (int i = 0; i < int(m_entries.size()); ++i) {
        const Entry &e = m_entries[i];
        const int h = e.header ? kSideHeadH() : kSideRowH();
        if (y + h < 0) { y += h; continue; }
        if (y > viewport()->height()) break;
        if (e.header) {
            QFont f = theme::uiFont(11, true);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
            p.setFont(f);
            p.setPen(theme::faint);
            const bool collapsed = m_collapsed.contains(e.key) && m_filter.isEmpty();
            p.drawText(QRect(12, y, w - 24, h), Qt::AlignVCenter, (collapsed ? QStringLiteral("▸  ") : QStringLiteral("▾  ")) + e.label);
            p.drawText(QRect(12, y, w - 26, h), Qt::AlignVCenter | Qt::AlignRight, QString::number(e.count));
        } else {
            if (e.current) p.fillRect(0, y, w, h, theme::selection);
            else if (i == m_hover) p.fillRect(0, y, w, h, theme::hover);
            const theme::Icon icon = e.kind == QLatin1String("local") ? theme::Icon::Laptop
                                   : e.kind == QLatin1String("remote") ? theme::Icon::Cloud
                                   : e.kind == QLatin1String("tag") ? theme::Icon::Tag
                                                                    : theme::Icon::Stash;
            p.setOpacity(0.7);
            theme::paintIcon(&p, icon, QRectF(16, y + (h - 14) / 2.0, 14, 14), theme::text);
            p.setOpacity(1);
            QString badges;
            int bw = 0;
            p.setFont(theme::uiFont(11));
            if (e.ahead) {
                const QString t = QStringLiteral("↑%1").arg(e.ahead);
                p.setPen(theme::green);
                bw += QFontMetrics(p.font()).horizontalAdvance(t) + 4;
                p.drawText(QRect(w - 12 - bw, y, bw, h), Qt::AlignVCenter | Qt::AlignLeft, t);
            }
            if (e.behind) {
                const QString t = QStringLiteral("↓%1").arg(e.behind);
                p.setPen(theme::yellow);
                const int tw = QFontMetrics(p.font()).horizontalAdvance(t) + 4;
                bw += tw;
                p.drawText(QRect(w - 12 - bw, y, tw, h), Qt::AlignVCenter | Qt::AlignLeft, t);
            }
            const QFont f = theme::uiFont(13, e.current);
            p.setFont(f);
            p.setPen(e.current ? QColor(Qt::white) : theme::text);
            const int avail = w - 38 - 12 - bw;
            p.drawText(QRect(38, y, avail, h), Qt::AlignVCenter, QFontMetrics(f).elidedText(e.name, Qt::ElideMiddle, avail));
        }
        y += h;
    }
}

QVariantMap SideList::Entry::map() const
{
    return {{"kind", kind}, {"name", name}, {"id", id}, {"index", index}, {"current", current}};
}

void SideList::contextMenuEvent(QContextMenuEvent *ev)
{
    const int i = rowAt(ev->pos().y());
    if (i < 0 || m_entries[i].header) return;
    emit jumpTo(m_entries[i].id);
    emit menuRequested(m_entries[i].map(), ev->globalPos());
}

void SideList::mouseDoubleClickEvent(QMouseEvent *ev)
{
    const int i = rowAt(int(ev->position().y()));
    if (i < 0 || m_entries[i].header || ev->button() != Qt::LeftButton) return;
    emit activated(m_entries[i].map());
}

void SideList::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton) return;
    const int i = rowAt(int(ev->position().y()));
    if (i < 0) return;
    const Entry &e = m_entries[i];
    if (e.header) {
        if (m_collapsed.contains(e.key)) m_collapsed.remove(e.key);
        else m_collapsed.insert(e.key);
        rebuild();
    } else {
        emit jumpTo(e.id);
    }
}

void SideList::mouseMoveEvent(QMouseEvent *ev)
{
    const int i = rowAt(int(ev->position().y()));
    if (i != m_hover) {
        m_hover = i;
        viewport()->setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        viewport()->update();
    }
}

void SideList::leaveEvent(QEvent *)
{
    m_hover = -1;
    viewport()->update();
}

Sidebar::Sidebar(QWidget *parent) : QWidget(parent)
{
    setBg(this, theme::sidebar);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 10, 0, 0);
    l->setSpacing(8);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Filter branches"));
    m_filter->setClearButtonEnabled(true);
    auto *wrap = new QHBoxLayout;
    wrap->setContentsMargins(10, 0, 10, 0);
    wrap->addWidget(m_filter);
    l->addLayout(wrap);
    m_list = new SideList(this);
    l->addWidget(m_list, 1);
    connect(m_filter, &QLineEdit::textChanged, m_list, &SideList::setFilter);
}

// ---------- FileList ----------

FileList::FileList(QWidget *parent) : QWidget(parent)
{
    connect(theme::Zoom::instance(), &theme::Zoom::changed, this, [this] {
        updateGeometry();
        update();
    });
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void FileList::setFiles(const QVariantList &files)
{
    m_files = files;
    m_hover = -1;
    setFixedHeight(int(m_files.size()) * kFileRowH() + 20);
    update();
}

void FileList::setOpenPath(const QString &p)
{
    m_open = p;
    update();
}

void FileList::setAction(const QString &glyph, const QString &tip)
{
    m_action = glyph;
    m_actionTip = tip;
    update();
}

QRect FileList::actionRect(int row) const
{
    return QRect(width() - 38, row * kFileRowH() + 3, 24, kFileRowH() - 6);
}

QSize FileList::sizeHint() const { return QSize(200, int(m_files.size()) * kFileRowH() + 20); }

void FileList::paintEvent(QPaintEvent *ev)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int w = width();
    const int first = std::max(0, ev->rect().top() / kFileRowH());
    const int last = std::min(int(m_files.size()) - 1, ev->rect().bottom() / kFileRowH());
    const QFont nameF = theme::uiFont(13), mono = theme::monoFont(12), badgeF = theme::uiFont(11, true);
    for (int i = first; i <= last; ++i) {
        const QVariantMap f = m_files[i].toMap();
        const int y = i * kFileRowH();
        if (f.value("path").toString() == m_open) p.fillRect(0, y, w, kFileRowH(), theme::selection);
        else if (i == m_hover) p.fillRect(0, y, w, kFileRowH(), theme::hover);
        const QString st = f.value("status").toString();
        QColor sc = theme::statusColor(st);
        QColor sbg = sc;
        sbg.setAlphaF(0.18);
        const QRectF badge(14, y + 5, 18, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(sbg);
        p.drawRoundedRect(badge, 4, 4);
        p.setPen(sc);
        p.setFont(badgeF);
        p.drawText(badge, Qt::AlignCenter, st);

        // stats on the right (leaving room for the action button)
        int sx = w - 16 - (m_action.isEmpty() ? 0 : 30);
        if (!m_action.isEmpty() && i == m_hover) {
            const QRect ar = actionRect(i);
            p.setPen(Qt::NoPen);
            p.setBrush(theme::field);
            p.drawRoundedRect(ar, 5, 5);
            p.setPen(m_action == QLatin1String("+") ? theme::green : theme::red);
            p.setFont(theme::uiFont(15, true));
            p.drawText(ar, Qt::AlignCenter, m_action);
        }
        p.setFont(mono);
        auto stat = [&](const QString &t, const QColor &c) {
            const int tw = QFontMetrics(mono).horizontalAdvance(t);
            sx -= tw;
            p.setPen(c);
            p.drawText(QRect(sx, y, tw, kFileRowH()), Qt::AlignVCenter, t);
            sx -= 6;
        };
        if (f.value("del").toInt() > 0) stat(QStringLiteral("−%1").arg(f.value("del").toInt()), theme::red);
        if (f.value("add").toInt() > 0) stat(QStringLiteral("+%1").arg(f.value("add").toInt()), theme::green);
        if (f.value("binary").toBool()) stat(QStringLiteral("bin"), theme::faint);

        // dir (dim) + file, eliding the directory first
        const QFontMetrics fm(nameF);
        const int avail = sx - 40 - 4;
        const QString file = f.value("file").toString();
        QString dir = f.value("dir").toString();
        const int fileW = fm.horizontalAdvance(file);
        p.setFont(nameF);
        int x = 40;
        if (fileW < avail) {
            dir = fm.elidedText(dir, Qt::ElideLeft, avail - fileW);
            p.setPen(QColor(0x7c, 0x83, 0x90));
            p.drawText(QRect(x, y, avail, kFileRowH()), Qt::AlignVCenter, dir);
            x += fm.horizontalAdvance(dir);
        }
        p.setPen(theme::text);
        p.drawText(QRect(x, y, avail - (x - 40), kFileRowH()), Qt::AlignVCenter, fm.elidedText(file, Qt::ElideLeft, avail - (x - 40)));
    }
}

void FileList::mousePressEvent(QMouseEvent *e)
{
    const int i = int(e->position().y()) / kFileRowH();
    if (i < 0 || i >= m_files.size()) return;
    if (!m_action.isEmpty() && actionRect(i).contains(e->position().toPoint())) emit actionClicked(m_files[i].toMap());
    else emit fileClicked(m_files[i].toMap());
}

void FileList::mouseDoubleClickEvent(QMouseEvent *e)
{
    const int i = int(e->position().y()) / kFileRowH();
    if (!m_action.isEmpty() && i >= 0 && i < m_files.size() && !actionRect(i).contains(e->position().toPoint()))
        emit actionClicked(m_files[i].toMap());
}

bool FileList::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip && !m_action.isEmpty()) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int i = he->pos().y() / kFileRowH();
        if (i >= 0 && i < m_files.size() && actionRect(i).contains(he->pos())) QToolTip::showText(he->globalPos(), m_actionTip, this);
        else QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void FileList::mouseMoveEvent(QMouseEvent *e)
{
    int i = int(e->position().y()) / kFileRowH();
    if (i >= m_files.size()) i = -1;
    if (i != m_hover) {
        m_hover = i;
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void FileList::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---------- DetailsPanel ----------

DetailsPanel::DetailsPanel(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget;
    setBg(m_content, theme::panel);
    setBg(viewport(), theme::panel);
    auto *v = new QVBoxLayout(m_content);
    v->setContentsMargins(16, 14, 16, 0);
    v->setSpacing(8);

    m_placeholder = label(m_content, 13, false, theme::faint);
    m_placeholder->setText(QStringLiteral("Select a commit"));
    m_placeholder->setAlignment(Qt::AlignCenter);
    v->addWidget(m_placeholder);

    // the message, with its edit button beside the title (a long body must not push it out of
    // sight); double-clicking the message edits it too
    m_summary = label(m_content, 16, true, theme::text);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_edit = new QPushButton(QStringLiteral("✎ Edit"), m_content);
    m_edit->setProperty("chip", true);
    m_edit->setCursor(Qt::PointingHandCursor);
    m_edit->setToolTip(QStringLiteral("Edit the commit message (or double-click it)"));
    connect(m_edit, &QPushButton::clicked, this, &DetailsPanel::editMessage);
    auto *sr = new QHBoxLayout;
    sr->setSpacing(10);
    sr->addWidget(m_summary, 1);
    sr->addWidget(m_edit, 0, Qt::AlignTop);
    v->addLayout(sr);
    m_body = label(m_content, 13, false, theme::dim);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_body);
    m_more = label(m_content, 12, false, theme::accent);
    m_more->setCursor(Qt::PointingHandCursor);
    v->addWidget(m_more);
    for (QLabel *l : {m_summary, m_body, m_more}) l->installEventFilter(this);

    m_authorRow = new QWidget(m_content);
    auto *ar = new QHBoxLayout(m_authorRow);
    ar->setContentsMargins(0, 6, 0, 0);
    ar->setSpacing(10);
    m_avatar = new AvatarWidget(36, m_authorRow);
    ar->addWidget(m_avatar);
    auto *names = new QVBoxLayout;
    names->setSpacing(2);
    m_author = label(m_authorRow, 13, true, theme::text);
    m_when = label(m_authorRow, 12, false, theme::dim);
    names->addWidget(m_author);
    names->addWidget(m_when);
    ar->addLayout(names, 1);
    m_sha = new QPushButton(m_authorRow);
    m_sha->setProperty("chip", true);
    m_sha->setToolTip(QStringLiteral("Copy full SHA"));
    m_sha->setFont(theme::monoFont(12));
    m_sha->setCursor(Qt::PointingHandCursor);
    ar->addWidget(m_sha);
    connect(m_sha, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_id);
        m_sha->setText(QStringLiteral("copied ✓"));
        QTimer::singleShot(1200, this, [this] { m_sha->setText(m_id.left(7)); });
    });
    v->addWidget(m_authorRow);

    m_committer = label(m_content, 12, false, theme::faint);
    v->addWidget(m_committer);

    m_parentsRow = new QWidget(m_content);
    m_parentsLayout = new QHBoxLayout(m_parentsRow);
    m_parentsLayout->setContentsMargins(0, 0, 0, 0);
    m_parentsLayout->setSpacing(6);
    v->addWidget(m_parentsRow);

    v->addSpacing(8);
    auto *sep = new QWidget(m_content);
    sep->setFixedHeight(theme::z(1));
    setBg(sep, theme::border);
    v->addWidget(sep);
    auto *fh = new QHBoxLayout;
    m_filesHead = label(m_content, 12, true, theme::dim);
    m_stats = label(m_content, 12, false, theme::dim);
    m_stats->setTextFormat(Qt::RichText);
    fh->addWidget(m_filesHead);
    fh->addStretch();
    fh->addWidget(m_stats);
    v->addLayout(fh);

    m_files = new FileList(m_content);
    v->addWidget(m_files);
    v->addStretch(1);
    connect(m_files, &FileList::fileClicked, this, &DetailsPanel::openFile);

    setWidget(m_content);
    setDetails({}, 0, 0);
}

// A long body is cut to its first lines with "Show all", so the author and the
// changed files stay in view.
void DetailsPanel::showBody()
{
    constexpr int kLines = 8;
    const QStringList lines = m_fullBody.split(QLatin1Char('\n'));
    const bool cut = !m_bodyOpen && (lines.size() > kLines || m_fullBody.size() > 700);
    QString shown = cut ? lines.mid(0, kLines).join(QLatin1Char('\n')).left(700).trimmed() + QStringLiteral(" …") : m_fullBody;
    m_body->setText(shown);
    m_body->setVisible(!m_fullBody.isEmpty());
    m_more->setText(cut ? QStringLiteral("Show all ▾") : QStringLiteral("Show less ▴"));
    m_more->setVisible(cut || m_bodyOpen);
}

bool DetailsPanel::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_more && e->type() == QEvent::MouseButtonPress) {
        m_bodyOpen = !m_bodyOpen;
        showBody();
        return true;
    }
    if ((o == m_summary || o == m_body) && e->type() == QEvent::MouseButtonDblClick && m_edit->isVisible()) {
        emit editMessage();
        return true;
    }
    return QScrollArea::eventFilter(o, e);
}

void DetailsPanel::setDetails(const QVariantMap &d, int laneColor, int rewordState)
{
    const bool has = d.contains("id") && !d.contains("error");
    m_id = d.value("id").toString();
    m_placeholder->setVisible(!has);
    for (QWidget *w : std::initializer_list<QWidget *>{m_summary, m_body, m_filesHead, m_stats, m_files}) w->setVisible(has);
    m_more->setVisible(false);
    const bool wip = d.value("wip").toBool();
    m_authorRow->setVisible(has && !wip);
    m_edit->setVisible(has && rewordState > 0);
    if (!has) {
        m_committer->hide();
        m_parentsRow->hide();
        if (d.contains("error")) m_placeholder->setText(d.value("error").toString());
        return;
    }
    m_summary->setText(d.value("summary").toString());
    m_fullBody = d.value("body").toString();
    m_bodyOpen = false;
    showBody();
    m_avatar->set(d.value("email").toString(), d.value("author").toString(), theme::lane(laneColor));
    m_author->setText(d.value("author").toString());
    const QString fmt = QStringLiteral("ddd d MMM yyyy, HH:mm");
    m_when->setText(QStringLiteral("authored ") + QLocale().toString(QDateTime::fromMSecsSinceEpoch(d.value("time").toLongLong()), fmt));
    m_sha->setText(m_id.left(7));
    const QString committer = d.value("committer").toString();
    m_committer->setVisible(!wip && !committer.isEmpty() && committer != d.value("author").toString());
    m_committer->setText(QStringLiteral("committed by %1 · %2")
                             .arg(committer, QLocale().toString(QDateTime::fromMSecsSinceEpoch(d.value("ctime").toLongLong()), fmt)));

    // parents
    while (QLayoutItem *it = m_parentsLayout->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    const QStringList parents = d.value("parents").toStringList();
    m_parentsRow->setVisible(!parents.isEmpty());
    auto *pl = label(m_parentsRow, 12, false, theme::faint);
    pl->setText(parents.size() > 1 ? QStringLiteral("parents") : QStringLiteral("parent"));
    m_parentsLayout->addWidget(pl);
    for (const QString &pid : parents) {
        auto *b = new QPushButton(pid.left(7), m_parentsRow);
        b->setProperty("chip", true);
        b->setFont(theme::monoFont(12));
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(QStringLiteral("Go to parent"));
        connect(b, &QPushButton::clicked, this, [this, pid] { emit jumpTo(pid); });
        m_parentsLayout->addWidget(b);
    }
    m_parentsLayout->addStretch();

    const QVariantList files = d.value("files").toList();
    m_filesHead->setText(QStringLiteral("%1 %2").arg(files.size()).arg(files.size() == 1 ? QStringLiteral("file changed") : QStringLiteral("files changed")));
    m_stats->setText(QStringLiteral("<span style='color:#4cc26b'>+%1</span>&nbsp;&nbsp;<span style='color:#e5534b'>−%2</span>")
                         .arg(d.value("add").toInt()).arg(d.value("del").toInt()));
    m_files->setFiles(files);
    verticalScrollBar()->setValue(0);
}
