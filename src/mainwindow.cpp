#include "mainwindow.h"
#include "commitmodel.h"
#include "commitview.h"
#include "diffview.h"
#include "panels.h"
#include "repo.h"
#include "theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

void setBg(QWidget *w, const QColor &c)
{
    w->setAutoFillBackground(true);
    QPalette p = w->palette();
    p.setColor(QPalette::Window, c);
    w->setPalette(p);
}

// The app logo, painted (no SVG plugin needed).
class Logo : public QWidget
{
public:
    explicit Logo(int size, QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(size, size); }
    static void draw(QPainter &p, const QRectF &r)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(r.topLeft());
        p.scale(r.width() / 64.0, r.height() / 64.0);
        const QColor a(0x5a, 0xa9, 0xe6), b(0xe0, 0x71, 0x9c);
        QPen pen(a, 5, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(QPointF(22, 12), QPointF(22, 52));
        pen.setColor(b);
        p.setPen(pen);
        QPainterPath path(QPointF(22, 42));
        path.cubicTo(22, 32, 42, 34, 42, 24);
        path.lineTo(42, 18);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(a);
        p.drawEllipse(QPointF(22, 52), 7, 7);
        p.drawEllipse(QPointF(22, 12), 7, 7);
        p.setBrush(b);
        p.drawEllipse(QPointF(42, 18), 7, 7);
        p.restore();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        draw(p, rect());
    }
};

QPushButton *button(const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_repo(Repo::instance())
{
    setWindowTitle(QStringLiteral("Towel"));
    QPixmap icon(128, 128);
    icon.fill(Qt::transparent);
    {
        QPainter p(&icon);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(theme::bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(0, 0, 128, 128), 28, 28);
        Logo::draw(p, QRectF(0, 0, 128, 128));
    }
    setWindowIcon(icon);

    auto *central = new QWidget(this);
    setBg(central, theme::bg);
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(buildTopBar());

    m_split = new QSplitter(Qt::Horizontal, central);
    m_split->setHandleWidth(1);
    m_split->setChildrenCollapsible(false);
    m_sidebar = new Sidebar(m_split);
    m_sidebar->setMinimumWidth(150);
    m_center = new QStackedWidget(m_split);
    m_center->setMinimumWidth(400);
    m_commits = new CommitView(m_repo->commits(), m_center);
    m_diff = new DiffView(m_center);
    m_empty = new QWidget(m_center);
    setBg(m_empty, theme::bg);
    {
        auto *el = new QVBoxLayout(m_empty);
        el->addStretch();
        auto *logo = new Logo(80, m_empty);
        el->addWidget(logo, 0, Qt::AlignHCenter);
        auto *t = label(m_empty, 20, true, theme::text);
        t->setText(QStringLiteral("Don't panic."));
        el->addWidget(t, 0, Qt::AlignHCenter);
        m_emptyMsg = label(m_empty, 13, false, theme::dim);
        m_emptyMsg->setText(QStringLiteral("Reading history…"));
        el->addWidget(m_emptyMsg, 0, Qt::AlignHCenter);
        auto *open = button(QStringLiteral("Open repository…"), QString(), m_empty);
        open->setProperty("primary", true);
        connect(open, &QPushButton::clicked, this, &MainWindow::openDialog);
        el->addWidget(open, 0, Qt::AlignHCenter);
        el->addStretch();
    }
    m_center->addWidget(m_commits);
    m_center->addWidget(m_diff);
    m_center->addWidget(m_empty);
    m_center->setCurrentWidget(m_empty);
    m_details = new DetailsPanel(m_split);
    m_details->setMinimumWidth(260);
    m_split->addWidget(m_sidebar);
    m_split->addWidget(m_center);
    m_split->addWidget(m_details);
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({230, 900, 420});
    v->addWidget(m_split, 1);
    v->addWidget(buildStatusStrip());
    setCentralWidget(central);

    // settings
    QSettings s;
    if (s.contains("splitter")) m_split->restoreState(s.value("splitter").toByteArray());
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());
    else resize(1560, 960);
    m_full = s.value("diffFull", false).toBool();
    m_splitDiff = s.value("diffSplit", false).toBool();
    m_wrap = s.value("diffWrap", false).toBool();
    m_diff->setOptions(m_full, m_splitDiff, m_wrap);

    // wiring
    connect(m_repo, &Repo::loadedChanged, this, &MainWindow::onLoaded);
    connect(m_repo, &Repo::loadingChanged, this, [this] {
        if (m_repo->loading() && !m_repo->isOpen()) m_emptyMsg->setText(QStringLiteral("Reading history…"));
    });
    connect(m_repo, &Repo::errorChanged, this, [this] {
        if (!m_repo->error().isEmpty() && !m_repo->isOpen()) {
            m_emptyMsg->setText(m_repo->error());
            m_center->setCurrentWidget(m_empty);
        }
    });
    connect(m_repo, &Repo::detailsChanged, this, [this] {
        const QVariantMap d = m_repo->details();
        const int row = m_repo->commits()->indexOf(d.value("id").toString());
        const CommitRow *r = m_repo->commits()->row(row);
        const int state = d.value("wip").toBool() ? 0 : m_repo->rewordState(d.value("id").toString());
        m_details->setDetails(d, r ? r->color : 0, state);
    });
    connect(m_repo, &Repo::diffChanged, this, [this] { m_diff->setDoc(&m_repo->diff(), m_repo->diffLoading()); });
    connect(m_repo, &Repo::diffLoadingChanged, this, [this] { m_diff->setDoc(&m_repo->diff(), m_repo->diffLoading()); });
    connect(m_repo, &Repo::opChanged, this, &MainWindow::updateOp);
    connect(m_repo, &Repo::opFinished, this, [this](bool ok) {
        m_opLog->setVisible(!ok && !m_repo->opOutput().isEmpty());
        if (ok) QTimer::singleShot(3500, this, [this] {
            if (!m_repo->opRunning() && !m_repo->opFailed()) m_repo->dismissOp();
        });
    });

    auto *detailsTimer = new QTimer(this);
    detailsTimer->setSingleShot(true);
    detailsTimer->setInterval(35);
    connect(detailsTimer, &QTimer::timeout, this, [this] { m_repo->loadDetails(m_selectedId); });
    connect(m_commits, &CommitView::currentChanged, this, [this, detailsTimer](int row) {
        const QString id = m_repo->commits()->idAt(row);
        if (id.isEmpty() || id == m_selectedId) return;
        m_selectedId = id;
        if (!m_openFile.isEmpty()) closeDiff();
        detailsTimer->start();
    });
    connect(m_sidebar->list(), &SideList::jumpTo, this, [this](const QString &id) {
        const int i = m_repo->commits()->indexOf(id);
        if (i >= 0) {
            closeDiff();
            m_commits->setCurrent(i);
            m_commits->setFocus();
        }
    });
    connect(m_details, &DetailsPanel::openFile, this, &MainWindow::showFile);
    connect(m_details, &DetailsPanel::jumpTo, this, [this](const QString &id) {
        const int i = m_repo->commits()->indexOf(id);
        if (i >= 0) {
            closeDiff();
            m_commits->setCurrent(i);
        }
    });
    connect(m_details, &DetailsPanel::editMessage, this, &MainWindow::editMessage);
    connect(m_diff, &DiffView::closeRequested, this, &MainWindow::closeDiff);
    connect(m_diff, &DiffView::stepFile, this, &MainWindow::stepFile);
    connect(m_diff, &DiffView::optionsChanged, this, [this](bool full, bool split, bool wrap) {
        const bool reload = full != m_full;
        m_full = full;
        m_splitDiff = split;
        m_wrap = wrap;
        QSettings s;
        s.setValue("diffFull", full);
        s.setValue("diffSplit", split);
        s.setValue("diffWrap", wrap);
        if (reload && !m_openFile.isEmpty()) showFile(m_openFile);
    });
    connect(m_repo->commits(), &CommitModel::searchFinished, this, [this] {
        const auto &hits = m_repo->commits()->hits();
        m_hitPos = -1;
        if (m_jumpOnHits && !hits.isEmpty()) {
            m_hitPos = 0;
            m_commits->setCurrent(hits.first());
        }
        m_jumpOnHits = false;
        const bool active = !m_search->text().trimmed().isEmpty();
        m_hitLabel->setVisible(active);
        m_hitLabel->setText(hits.isEmpty() ? QStringLiteral("no matches")
                                           : (m_hitPos >= 0 ? QStringLiteral("%1 / ").arg(m_hitPos + 1) : QString()) + QString::number(hits.size()));
        m_hitLabel->setStyleSheet(hits.isEmpty() ? QStringLiteral("color:#e5534b") : QStringLiteral("color:#8d94a1"));
    });

    // shortcuts
    auto sc = [this](const QKeySequence &k, auto fn) { connect(new QShortcut(k, this), &QShortcut::activated, this, fn); };
    sc(QKeySequence(QStringLiteral("Ctrl+F")), [this] { m_search->setFocus(); m_search->selectAll(); });
    sc(QKeySequence(QStringLiteral("Ctrl+O")), [this] { openDialog(); });
    sc(QKeySequence(QStringLiteral("F5")), [this] { m_repo->refresh(); });
    sc(QKeySequence(QStringLiteral("Ctrl+R")), [this] { m_repo->refresh(); });
    sc(QKeySequence(QStringLiteral("Ctrl+Q")), [] { qApp->quit(); });
    sc(QKeySequence(QStringLiteral("Esc")), [this] {
        if (!m_openFile.isEmpty()) closeDiff();
        else if (m_search->hasFocus()) m_search->clear();
    });
    sc(QKeySequence(QStringLiteral("Alt+Down")), [this] { stepFile(1); });
    sc(QKeySequence(QStringLiteral("Alt+Up")), [this] { stepFile(-1); });

    // re-check the repo while the window is active (commits from a terminal etc.)
    auto *poll = new QTimer(this);
    poll->setInterval(4000);
    connect(poll, &QTimer::timeout, this, [this] {
        if (isActiveWindow()) m_repo->checkChanged();
    });
    poll->start();
    updateOp();
}

bool MainWindow::event(QEvent *e)
{
    if (e->type() == QEvent::WindowActivate) m_repo->checkChanged();
    return QMainWindow::event(e);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings s;
    s.setValue("splitter", m_split->saveState());
    s.setValue("geometry", saveGeometry());
    QMainWindow::closeEvent(e);
}

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    bar->setFixedHeight(46);
    setBg(bar, theme::topbar);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(8);
    h->addWidget(new Logo(24, bar));
    m_repoBtn = button(QStringLiteral("Open repository…"), QStringLiteral("Ctrl+O"), bar);
    connect(m_repoBtn, &QPushButton::clicked, this, &MainWindow::openDialog);
    h->addWidget(m_repoBtn);
    m_branch = new QLabel(bar);
    m_branch->setTextFormat(Qt::RichText);
    m_branch->setStyleSheet(QStringLiteral("background:#272b33; border-radius:13px; padding:4px 12px;"));
    m_branch->hide();
    h->addWidget(m_branch);
    h->addStretch(1);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(QStringLiteral("⌕  Search message, author, SHA, branch   ·   Ctrl+F"));
    m_search->setFixedWidth(440);
    m_search->setClearButtonEnabled(true);
    h->addWidget(m_search);
    m_hitLabel = new QLabel(bar);
    m_hitLabel->setFont(theme::uiFont(12));
    m_hitLabel->setMinimumWidth(70);
    m_hitLabel->hide();
    h->addWidget(m_hitLabel);
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(140);
    connect(m_search, &QLineEdit::textChanged, debounce, qOverload<>(&QTimer::start));
    connect(debounce, &QTimer::timeout, this, [this] {
        m_jumpOnHits = true;
        m_repo->commits()->search(m_search->text());
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] { searchNext(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier); });
    h->addStretch(1);

    m_fetch = button(QStringLiteral("Fetch"), QStringLiteral("git fetch --all --prune"), bar);
    m_pull = button(QStringLiteral("↓ Pull"), QStringLiteral("git pull --rebase --autostash"), bar);
    m_push = button(QStringLiteral("↑ Push"), QStringLiteral("git push"), bar);
    auto *refresh = button(QStringLiteral("⟳"), QStringLiteral("Refresh (F5)"), bar);
    connect(m_fetch, &QPushButton::clicked, m_repo, &Repo::fetch);
    connect(m_pull, &QPushButton::clicked, m_repo, &Repo::pull);
    connect(m_push, &QPushButton::clicked, this, [this] { m_repo->push(false); });
    connect(refresh, &QPushButton::clicked, m_repo, &Repo::refresh);
    for (auto *b : {m_fetch, m_pull, m_push, refresh}) h->addWidget(b);
    return bar;
}

QWidget *MainWindow::buildStatusStrip()
{
    m_strip = new QWidget(this);
    auto *v = new QVBoxLayout(m_strip);
    v->setContentsMargins(14, 4, 10, 6);
    v->setSpacing(4);
    auto *h = new QHBoxLayout;
    h->setSpacing(10);
    m_opIcon = label(m_strip, 14, true, theme::accent);
    m_opText = label(m_strip, 12, true, theme::text);
    m_opLast = label(m_strip, 12, false, theme::dim);
    m_opLast->setFont(theme::monoFont(12));
    m_opLast->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_opForce = button(QStringLiteral("Force push (with lease)"), QString(), m_strip);
    m_opForce->setProperty("primary", true);
    m_opToggle = button(QStringLiteral("Show output"), QString(), m_strip);
    m_opClose = button(QStringLiteral("✕"), QStringLiteral("Dismiss"), m_strip);
    connect(m_opForce, &QPushButton::clicked, this, [this] { m_repo->push(true); });
    connect(m_opToggle, &QPushButton::clicked, this, [this] { m_opLog->setVisible(!m_opLog->isVisible()); updateOp(); });
    connect(m_opClose, &QPushButton::clicked, m_repo, &Repo::dismissOp);
    h->addWidget(m_opIcon);
    h->addWidget(m_opText);
    h->addWidget(m_opLast, 1);
    h->addWidget(m_opForce);
    h->addWidget(m_opToggle);
    h->addWidget(m_opClose);
    v->addLayout(h);
    m_opLog = new QPlainTextEdit(m_strip);
    m_opLog->setReadOnly(true);
    m_opLog->setFont(theme::monoFont(12));
    m_opLog->setFixedHeight(140);
    m_opLog->hide();
    v->addWidget(m_opLog);
    return m_strip;
}

void MainWindow::updateOp()
{
    const bool active = m_repo->opRunning() || !m_repo->opLabel().isEmpty();
    m_strip->setVisible(active);
    if (!active) {
        m_opLog->hide();
        return;
    }
    const bool running = m_repo->opRunning(), failed = m_repo->opFailed();
    setBg(m_strip, failed ? QColor(0x3a, 0x22, 0x26) : running ? theme::panel : QColor(0x1e, 0x33, 0x26));
    m_opIcon->setText(running ? QStringLiteral("⟳") : failed ? QStringLiteral("✕") : QStringLiteral("✓"));
    QPalette pal = m_opIcon->palette();
    pal.setColor(QPalette::WindowText, running ? theme::accent : failed ? theme::red : theme::green);
    m_opIcon->setPalette(pal);
    m_opText->setText(m_repo->opLabel() + (running ? QStringLiteral("…") : failed ? QStringLiteral(" failed") : QStringLiteral(" done")));
    const QStringList lines = m_repo->opOutput().trimmed().split(QLatin1Char('\n'));
    m_opLast->setText(lines.isEmpty() ? QString() : lines.last());
    m_opLog->setPlainText(m_repo->opOutput());
    m_opForce->setVisible(m_repo->opCanForce() && !running);
    m_opToggle->setVisible(!running && !m_repo->opOutput().isEmpty());
    m_opToggle->setText(m_opLog->isVisible() ? QStringLiteral("Hide output") : QStringLiteral("Show output"));
    m_opClose->setVisible(!running);
    for (auto *b : {m_fetch, m_pull, m_push}) b->setEnabled(!running);
}

void MainWindow::updateBranchChip()
{
    QString html = QStringLiteral("<b>%1</b>").arg(m_repo->head().toHtmlEscaped());
    if (m_repo->ahead()) html += QStringLiteral("&nbsp;&nbsp;<span style='color:#4cc26b'>↑%1</span>").arg(m_repo->ahead());
    if (m_repo->behind()) html += QStringLiteral("&nbsp;<span style='color:#e0a43a'>↓%1</span>").arg(m_repo->behind());
    m_branch->setText(QStringLiteral("⑂&nbsp;") + html);
    m_branch->setVisible(m_repo->isOpen());
    m_pull->setText(m_repo->behind() ? QStringLiteral("↓ Pull  %1").arg(m_repo->behind()) : QStringLiteral("↓ Pull"));
    m_push->setText(m_repo->ahead() ? QStringLiteral("↑ Push  %1").arg(m_repo->ahead()) : QStringLiteral("↑ Push"));
    m_push->setToolTip(m_repo->upstream().isEmpty() ? QStringLiteral("git push (sets upstream)") : QStringLiteral("git push → ") + m_repo->upstream());
}

void MainWindow::onLoaded()
{
    setWindowTitle(m_repo->name() + QStringLiteral(" — Towel"));
    m_repoBtn->setText(m_repo->name());
    m_repoBtn->setToolTip(m_repo->path() + QStringLiteral("\nCtrl+O to open another"));
    updateBranchChip();
    m_sidebar->list()->setData(m_repo->branches(), m_repo->tags(), m_repo->stashes());
    if (m_openFile.isEmpty()) m_center->setCurrentWidget(m_commits);
    int i = m_selectedId.isEmpty() ? -1 : m_repo->commits()->indexOf(m_selectedId);
    if (i < 0) i = 0;
    m_selectedId = m_repo->commits()->idAt(i);
    m_commits->setCurrent(i);
    m_repo->loadDetails(m_selectedId);
    if (!m_search->text().trimmed().isEmpty()) m_repo->commits()->search(m_search->text());
    if (!m_openFile.isEmpty()) m_repo->loadDiff(m_selectedId, m_openFile.value("path").toString(), m_openFile.value("oldPath").toString(), m_full);
    if (m_center->currentWidget() == m_commits) m_commits->setFocus();
}

void MainWindow::showFile(const QVariantMap &f)
{
    m_openFile = f;
    m_details->setOpenPath(f.value("path").toString());
    m_center->setCurrentWidget(m_diff);
    m_repo->loadDiff(m_selectedId, f.value("path").toString(), f.value("oldPath").toString(), m_full);
}

void MainWindow::closeDiff()
{
    if (m_openFile.isEmpty()) return;
    m_openFile.clear();
    m_details->setOpenPath(QString());
    m_repo->clearDiff();
    m_center->setCurrentWidget(m_commits);
    m_commits->setFocus();
}

void MainWindow::stepFile(int delta)
{
    const QVariantList files = m_repo->details().value("files").toList();
    if (m_openFile.isEmpty() || files.isEmpty()) return;
    int i = 0;
    for (int k = 0; k < files.size(); ++k)
        if (files[k].toMap().value("path") == m_openFile.value("path")) i = k;
    i = std::clamp(i + delta, 0, int(files.size()) - 1);
    showFile(files[i].toMap());
}

void MainWindow::openDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Open a git repository"), m_repo->path());
    if (!dir.isEmpty()) {
        m_selectedId.clear();
        closeDiff();
        m_repo->open(dir);
    }
}

void MainWindow::searchNext(bool back)
{
    const auto &hits = m_repo->commits()->hits();
    if (hits.isEmpty()) return;
    m_hitPos = (m_hitPos + (back ? -1 : 1) + int(hits.size())) % int(hits.size());
    m_commits->setCurrent(hits[m_hitPos]);
    m_hitLabel->setText(QStringLiteral("%1 / %2").arg(m_hitPos + 1).arg(hits.size()));
}

void MainWindow::editMessage()
{
    const QVariantMap d = m_repo->details();
    const QString id = d.value("id").toString();
    const int state = m_repo->rewordState(id);
    if (state == 0) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Edit commit message"));
    dlg.resize(680, 380);
    auto *v = new QVBoxLayout(&dlg);
    v->setContentsMargins(18, 18, 18, 18);
    v->setSpacing(10);
    auto *t = label(&dlg, 16, true, theme::text);
    t->setText(QStringLiteral("Edit commit message"));
    v->addWidget(t);
    auto *sha = label(&dlg, 12, false, theme::faint);
    sha->setFont(theme::monoFont(12));
    sha->setText(id.left(10));
    v->addWidget(sha);
    if (state == 2) {
        auto *warn = label(&dlg, 12, false, theme::yellow);
        warn->setWordWrap(true);
        warn->setStyleSheet(QStringLiteral("background:#3a3220; border-radius:5px; padding:8px;"));
        warn->setText(QStringLiteral("This commit is already on a remote. Saving rewrites history, so the next push must be a force push. "
                                     "Towel will offer --force-with-lease, which refuses to overwrite commits you haven't seen."));
        v->addWidget(warn);
    }
    auto *edit = new QPlainTextEdit(&dlg);
    edit->setFont(theme::monoFont(13));
    edit->setPlainText(d.value("message").toString().trimmed());
    v->addWidget(edit, 1);
    auto *row = new QHBoxLayout;
    auto *hint = label(&dlg, 11, false, theme::faint);
    hint->setText(QStringLiteral("Ctrl+Enter to save"));
    row->addWidget(hint);
    row->addStretch();
    auto *cancel = button(QStringLiteral("Cancel"), QString(), &dlg);
    cancel->setProperty("chip", true);
    auto *save = button(QStringLiteral("Save"), QString(), &dlg);
    save->setProperty("primary", true);
    row->addWidget(cancel);
    row->addWidget(save);
    v->addLayout(row);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dlg, [&] {
        if (!edit->toPlainText().trimmed().isEmpty()) dlg.accept();
    });
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), &dlg), &QShortcut::activated, save, &QPushButton::click);
    edit->setFocus();
    if (dlg.exec() == QDialog::Accepted) m_repo->reword(id, edit->toPlainText());
}

void MainWindow::runDemo()
{
    const QString row = qEnvironmentVariable("TOWEL_DEMO_ROW");
    const QString file = qEnvironmentVariable("TOWEL_DEMO_FILE");
    const QString search = qEnvironmentVariable("TOWEL_DEMO_SEARCH");
    QTimer::singleShot(1200, this, [=, this] {
        if (!search.isEmpty()) m_search->setText(search);
        if (!row.isEmpty()) m_commits->setCurrent(row.toInt());
        if (!file.isEmpty())
            QTimer::singleShot(600, this, [=, this] {
                const QVariantList files = m_repo->details().value("files").toList();
                if (files.size() > file.toInt()) showFile(files[file.toInt()].toMap());
            });
    });
}
