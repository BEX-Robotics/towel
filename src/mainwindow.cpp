#include "mainwindow.h"
#include "commitmodel.h"
#include "commitpanel.h"
#include "commitview.h"
#include "diffview.h"
#include "panels.h"
#include "rebaseview.h"
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
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
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
#include <cmath>
#include <QToolTip>
#include <QWheelEvent>

namespace {

void setBg(QWidget *w, const QColor &c) {
    w->setAutoFillBackground(true);
    QPalette p = w->palette();
    p.setColor(QPalette::Window, c);
    w->setPalette(p);
}

// The app logo, painted (no SVG plugin needed).
class Logo : public QWidget {
public:
    explicit Logo(int size, QWidget *parent = 0) : QWidget(parent) { setFixedSize(theme::z(size), theme::z(size)); }
    static void draw(QPainter &p, const QRectF &r) {
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
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        draw(p, rect());
    }
};

QPushButton *button(const QString &text, const QString &tip, QWidget *parent) {
    auto *b = new QPushButton(text, parent);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);

    return b;
}

QLabel *label(QWidget *parent, int px, bool bold, const QColor &c) {
    auto *l = new QLabel(parent);
    l->setFont(theme::uiFont(px, bold));
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, c);
    l->setPalette(p);

    return l;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_repo(Repo::instance()) {
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
    m_sidebar->setMinimumWidth(theme::z(150));
    m_center = new QStackedWidget(m_split);
    m_center->setMinimumWidth(theme::z(400));
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
    m_rebase = new RebaseView(m_center);
    m_center->addWidget(m_rebase);
    connect(m_rebase, &RebaseView::cancel, this, [this] {
        m_center->setCurrentWidget(m_commits);
        m_commits->setFocus();
    });
    connect(m_rebase, &RebaseView::run, this, [this](const QString &base, const QVariantList &items) {
        m_center->setCurrentWidget(m_commits);
        m_commits->setFocus();
        m_repo->rebase(base, items);
    });
    m_center->setCurrentWidget(m_empty);
    m_right = new QStackedWidget(m_split);
    m_right->setMinimumWidth(theme::z(260));
    m_details = new DetailsPanel(m_right);
    m_commitPanel = new CommitPanel(m_right);
    m_right->addWidget(m_details);
    m_right->addWidget(m_commitPanel);
    m_split->addWidget(m_sidebar);
    m_split->addWidget(m_center);
    m_split->addWidget(m_right);
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({230, 900, 420});
    v->addWidget(m_split, 1);
    v->addWidget(buildStatusStrip());
    setCentralWidget(central);

    // settings
    QSettings s;
    if (s.contains("splitter")) m_split->restoreState(s.value("splitter").toByteArray());
    if (s.contains("geometry"))
        restoreGeometry(s.value("geometry").toByteArray());
    else
        resize(1560, 960);
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
    connect(m_commits, &CommitView::menuRequested, this, &MainWindow::commitMenu);
    connect(m_commitPanel, &CommitPanel::stashRequested, this, &MainWindow::stashChanges);
    connect(m_sidebar->list(), &SideList::menuRequested, this, &MainWindow::refMenu);
    connect(m_sidebar->list(), &SideList::activated, this, [this](const QVariantMap &e) {
        const QString k = e.value("kind").toString();
        if (k == QLatin1String("local") || k == QLatin1String("remote")) checkout(e);
    });
    connect(m_repo, &Repo::undoChanged, this, &MainWindow::updateUndo);
    updateUndo();
    connect(m_repo, &Repo::restoreMessage, this, [this](const QString &msg) { m_commitPanel->setAmendInfo(msg, false); });
    connect(m_repo, &Repo::opFinished, this, [this](bool ok) {
        m_opLog->setVisible(!ok && !m_repo->opOutput().isEmpty());
        if (ok) QTimer::singleShot(3500, this, [this] {
            if (!m_repo->opRunning() && !m_repo->opFailed()) m_repo->dismissOp();
        });
    });

    auto *detailsTimer = new QTimer(this);
    detailsTimer->setSingleShot(true);
    detailsTimer->setInterval(35);
    connect(detailsTimer, &QTimer::timeout, this, [this] { m_repo->loadDetails(m_selectedID); });
    connect(m_commits, &CommitView::currentChanged, this, [this, detailsTimer](int row) {
        const QString id = m_repo->commits()->idAt(row);
        if (id.isEmpty() || id == m_selectedID) return;
        m_selectedID = id;
        if (!m_openFile.isEmpty()) closeDiff();
        if (id == QLatin1String("WIP")) {
            m_right->setCurrentWidget(m_commitPanel);
            m_repo->loadStatus();
        } else {
            m_right->setCurrentWidget(m_details);
            detailsTimer->start();
        }
    });

    // commit panel
    connect(m_repo, &Repo::statusChanged, this, &MainWindow::onStatus);
    connect(m_commitPanel, &CommitPanel::openFile, this, [this](const QVariantMap &f, const QString &mode) { showFile(f, mode); });
    connect(m_commitPanel, &CommitPanel::stage, m_repo, &Repo::stage);
    connect(m_diff->area(), &DiffArea::applyHunk, this, [this](const QSet<int> &del, const QSet<int> &add) {
        if (m_openMode.isEmpty() || m_openFile.isEmpty()) return;
        QStringList paths{m_openFile.value("path").toString()};
        if (!m_openFile.value("oldPath").toString().isEmpty()) paths << m_openFile.value("oldPath").toString();
        m_repo->applyLines(paths, m_openMode == QLatin1String("staged"), del, add);
    });
    connect(m_commitPanel, &CommitPanel::unstage, m_repo, &Repo::unstage);
    connect(m_commitPanel, &CommitPanel::stageAll, m_repo, &Repo::stageAll);
    connect(m_commitPanel, &CommitPanel::unstageAll, m_repo, &Repo::unstageAll);
    connect(m_commitPanel, &CommitPanel::commit, this, [this](const QString &msg, bool amend) {
        closeDiff();
        m_repo->commit(msg, amend);
    });
    connect(m_repo, &Repo::committed, m_commitPanel, &CommitPanel::clearMessage);
    connect(m_commitPanel, &CommitPanel::amendToggled, this, [this](bool on) {
        if (on) m_commitPanel->setAmendInfo(m_repo->headMessage(), m_repo->rewordState(m_repo->headID()) == 2);
    });
    connect(m_sidebar->list(), &SideList::jumpTo, this, [this](const QString &id) {
        const int i = m_repo->commits()->indexOf(id);
        if (i >= 0) {
            closeDiff();
            m_commits->setCurrent(i);
            m_commits->setFocus();
        }
    });
    connect(m_details, &DetailsPanel::openFile, this, [this](const QVariantMap &f) { showFile(f); });
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
        if (reload && !m_openFile.isEmpty()) showFile(m_openFile, m_openMode);
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
    sc(QKeySequence::Undo, [this] { m_repo->undo(); });

    // zoom, for anyone who wants it bigger: Ctrl+scroll, Ctrl+= / Ctrl+-, Ctrl+0
    sc(QKeySequence::ZoomIn, [this] { zoomBy(1); });
    sc(QKeySequence(QStringLiteral("Ctrl+=")), [this] { zoomBy(1); });
    sc(QKeySequence::ZoomOut, [this] { zoomBy(-1); });
    sc(QKeySequence(QStringLiteral("Ctrl+0")), [this] { zoomBy(0); });
    qApp->installEventFilter(this);
    sc(QKeySequence::Redo, [this] { m_repo->redo(); });
    sc(QKeySequence(QStringLiteral("Ctrl+F")), [this] { m_search->setFocus(); m_search->selectAll(); });
    sc(QKeySequence(QStringLiteral("Ctrl+O")), [this] { openDialog(); });
    sc(QKeySequence(QStringLiteral("F5")), [this] { m_repo->refresh(); });
    sc(QKeySequence(QStringLiteral("Ctrl+R")), [this] { m_repo->refresh(); });
    sc(QKeySequence(QStringLiteral("Ctrl+Q")), [] { qApp->quit(); });
    sc(QKeySequence(QStringLiteral("Esc")), [this] {
        if (!m_openFile.isEmpty())
            closeDiff();
        else if (m_search->hasFocus())
            m_search->clear();
    });
    sc(QKeySequence(QStringLiteral("Alt+Down")), [this] { stepFile(1); });
    sc(QKeySequence(QStringLiteral("Ctrl+Return")), [this] {
        if (m_right->currentWidget() == m_commitPanel) m_commitPanel->triggerCommit();
    });
    sc(QKeySequence(QStringLiteral("Alt+Up")), [this] { stepFile(-1); });

    // re-check the repo while the window is active (commits from a terminal etc.)
    auto *poll = new QTimer(this);
    poll->setInterval(4000);
    connect(poll, &QTimer::timeout, this, [this] {
        // active: every 4 s; in the background the .git watcher catches commits, pushes
        // and checkouts, and this slower poll catches plain file edits
        if (isActiveWindow() || ++m_idleTicks % 8 == 0)
            m_repo->checkChanged();
    });
    poll->start();
    updateOp();
}

bool MainWindow::event(QEvent *e) {
    if (e->type() == QEvent::WindowActivate) m_repo->checkChanged();
    return QMainWindow::event(e);
}

void MainWindow::closeEvent(QCloseEvent *e) {
    QSettings s;
    s.setValue("splitter", m_split->saveState());
    s.setValue("geometry", saveGeometry());
    QMainWindow::closeEvent(e);
}

// Recent repositories, then "Open other…".
void MainWindow::recentMenu() {
    QMenu menu(this);
    const QStringList recent = QSettings().value("recentRepos").toStringList();
    for (const QString &path : recent) {
        if (path == m_repo->path()) continue;
        QAction *a = menu.addAction(QDir(path).dirName() + QStringLiteral("    ") + QDir::toNativeSeparators(path), this, [this, path] {
            m_selectedID.clear();
            closeDiff();
            m_repo->open(path);
        });
        a->setEnabled(QDir(path).exists());
    }
    if (!menu.isEmpty()) menu.addSeparator();
    menu.addAction(QStringLiteral("Open other…\tCtrl+O"), this, &MainWindow::openDialog);
    menu.exec(m_repoBtn->mapToGlobal(QPoint(0, m_repoBtn->height())));
}

QWidget *MainWindow::buildTopBar() {
    auto *bar = new QWidget(this);
    bar->setFixedHeight(theme::z(46));
    setBg(bar, theme::topbar);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(8);
    h->addWidget(new Logo(24, bar));
    m_repoBtn = button(QStringLiteral("Open repository…"), QStringLiteral("Ctrl+O"), bar);
    connect(m_repoBtn, &QPushButton::clicked, this, &MainWindow::recentMenu);
    h->addWidget(m_repoBtn);
    m_branch = new QLabel(bar);
    m_branch->setTextFormat(Qt::RichText);
    m_branch->setStyleSheet(QStringLiteral("background:#272b33; border-radius:13px; padding:4px 12px;"));
    m_branch->hide();
    h->addWidget(m_branch);
    h->addStretch(1);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(QStringLiteral("⌕  Search message, author, SHA, branch   ·   Ctrl+F"));
    m_search->setFixedWidth(theme::z(440));
    m_search->setClearButtonEnabled(true);
    h->addWidget(m_search);
    m_hitLabel = new QLabel(bar);
    m_hitLabel->setFont(theme::uiFont(12));
    m_hitLabel->setMinimumWidth(theme::z(70));
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

    m_undoBtn = button(QStringLiteral("↶ Undo"), QString(), bar);
    connect(m_undoBtn, &QPushButton::clicked, m_repo, &Repo::undo);
    h->addWidget(m_undoBtn);
    h->addSpacing(6);
    m_fetch = button(QStringLiteral("Fetch"), QStringLiteral("git fetch --all --prune"), bar);
    m_pull = button(QStringLiteral("↓ Pull"), QStringLiteral("git pull --rebase --autostash"), bar);
    m_push = button(QStringLiteral("↑ Push"), QStringLiteral("git push"), bar);
    auto *refresh = button(QStringLiteral("⟳"), QStringLiteral("Refresh (F5)"), bar);
    connect(m_fetch, &QPushButton::clicked, m_repo, &Repo::fetch);
    autoFetch(m_fetch);
    askBeforePull();
    connect(m_pull, &QPushButton::clicked, m_repo, &Repo::pull);
    connect(m_push, &QPushButton::clicked, this, [this] { m_repo->push(false); });
    connect(refresh, &QPushButton::clicked, m_repo, &Repo::refresh);
    for (auto *b : {m_fetch, m_pull, m_push, refresh}) h->addWidget(b);
    return bar;
}

QWidget *MainWindow::buildStatusStrip() {
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
    m_opLog->setFixedHeight(theme::z(140));
    m_opLog->hide();
    v->addWidget(m_opLog);

    return m_strip;
}

void MainWindow::updateOp() {
    const bool active = m_repo->opRunning() || !m_repo->opLabel().isEmpty();
    m_strip->setVisible(active);
    if (!active) {
        m_opLog->hide();
        return;
    }
    const bool running = m_repo->opRunning(), failed = m_repo->opFailed();
    setBg(m_strip, failed ? QColor(0x3a, 0x22, 0x26) : running ? theme::panel
                                                               : QColor(0x1e, 0x33, 0x26));
    m_opIcon->setText(running ? QStringLiteral("⟳") : failed ? QStringLiteral("✕")
                                                             : QStringLiteral("✓"));
    QPalette pal = m_opIcon->palette();
    pal.setColor(QPalette::WindowText, running ? theme::accent : failed ? theme::red
                                                                        : theme::green);
    m_opIcon->setPalette(pal);
    m_opText->setText(m_repo->opLabel() + (running ? QStringLiteral("…") : failed ? QStringLiteral(" failed")
                                                                                  : QStringLiteral(" done")));
    const QStringList lines = m_repo->opOutput().trimmed().split(QLatin1Char('\n'));
    m_opLast->setText(lines.isEmpty() ? QString() : lines.last());
    m_opLog->setPlainText(m_repo->opOutput());
    m_opForce->setVisible(m_repo->opCanForce() && !running);
    m_opToggle->setVisible(!running && !m_repo->opOutput().isEmpty());
    m_opToggle->setText(m_opLog->isVisible() ? QStringLiteral("Hide output") : QStringLiteral("Show output"));
    m_opClose->setVisible(!running);
    for (auto *b : {m_fetch, m_pull, m_push}) b->setEnabled(!running);
}

void MainWindow::updateBranchChip() {
    QString html = QStringLiteral("<b>%1</b>").arg(m_repo->head().toHtmlEscaped());
    if (m_repo->ahead()) html += QStringLiteral("&nbsp;&nbsp;<span style='color:#4cc26b'>↑%1</span>").arg(m_repo->ahead());
    if (m_repo->behind()) html += QStringLiteral("&nbsp;<span style='color:#e0a43a'>↓%1</span>").arg(m_repo->behind());
    m_branch->setText(QStringLiteral("⑂&nbsp;") + html);
    m_branch->setVisible(m_repo->isOpen());
    m_pull->setText(m_repo->behind() ? QStringLiteral("↓ Pull  %1").arg(m_repo->behind()) : QStringLiteral("↓ Pull"));
    m_push->setText(m_repo->ahead() ? QStringLiteral("↑ Push  %1").arg(m_repo->ahead()) : QStringLiteral("↑ Push"));
    m_push->setToolTip(m_repo->upstream().isEmpty() ? QStringLiteral("git push (sets upstream)") : QStringLiteral("git push → ") + m_repo->upstream());
}

void MainWindow::onLoaded() {
    setWindowTitle(m_repo->name() + QStringLiteral(" — Towel"));
    m_repoBtn->setText(m_repo->name());
    m_repoBtn->setToolTip(m_repo->path() + QStringLiteral("\nClick for recent repositories · Ctrl+O to open another"));
    {
        QSettings st;
        QStringList recent = st.value("recentRepos").toStringList();
        if (recent.value(0) != m_repo->path()) {
            recent.removeAll(m_repo->path());
            recent.prepend(m_repo->path());
            st.setValue("recentRepos", recent.mid(0, 12));
        }
    }
    updateBranchChip();
    m_sidebar->list()->setData(m_repo->branches(), m_repo->tags(), m_repo->stashes());
    if (m_openFile.isEmpty()) m_center->setCurrentWidget(m_commits);
    int i = m_selectedID.isEmpty() ? -1 : m_repo->commits()->indexOf(m_selectedID);
    if (i < 0) i = 0;
    m_selectedID = m_repo->commits()->idAt(i);
    m_commits->setCurrent(i);
    m_commitPanel->setRepoKey(m_repo->path());
    m_commitPanel->setBranch(m_repo->detached() ? QString() : m_repo->head());
    const bool wip = m_selectedID == QLatin1String("WIP");
    m_right->setCurrentWidget(wip ? static_cast<QWidget *>(m_commitPanel) : m_details);
    if (wip)
        m_repo->loadStatus();
    else
        m_repo->loadDetails(m_selectedID);
    if (!m_search->text().trimmed().isEmpty()) m_repo->commits()->search(m_search->text());
    if (!m_openFile.isEmpty() && m_openMode.isEmpty())
        m_repo->loadDiff(m_selectedID, m_openFile.value("path").toString(), m_openFile.value("oldPath").toString(), m_full);
    if (m_center->currentWidget() == m_commits) m_commits->setFocus();
}

void MainWindow::showFile(const QVariantMap &f, const QString &mode) {
    const bool reload = !m_openFile.isEmpty() && mode == m_openMode && f.value("path") == m_openFile.value("path");
    if (reload) m_diff->area()->keepPlace();
    m_diff->area()->setStaging(mode == QLatin1String("unstaged") ? 1 : mode == QLatin1String("staged") ? 2
                                                                                                       : 0);
    if (!mode.isEmpty() && !reload) {
        QWidget *fw = QApplication::focusWidget();
        if (!qobject_cast<QLineEdit *>(fw) && !qobject_cast<QPlainTextEdit *>(fw)) m_diff->area()->setFocus();
    }
    m_openFile = f;
    m_openMode = mode;
    if (mode.isEmpty())
        m_details->setOpenPath(f.value("path").toString());
    else
        m_commitPanel->setOpenFile(mode, f.value("path").toString());
    m_diff->setModeLabel(mode == QLatin1String("staged") ? QStringLiteral("Staged") : mode == QLatin1String("unstaged") ? QStringLiteral("Unstaged")
                                                                                                                        : QString());
    m_center->setCurrentWidget(m_diff);
    const QString id = mode.isEmpty() ? m_selectedID : QStringLiteral("WIP:") + mode;
    m_repo->loadDiff(id, f.value("path").toString(), f.value("oldPath").toString(), m_full);
}

// Status reloaded (after staging, committing, or selecting WIP): update the
// panel and keep the open diff sensible.
void MainWindow::onStatus() {
    m_commitPanel->setStatus(m_repo->status());
    if (m_openMode.isEmpty() || m_openFile.isEmpty()) return;
    const QVariantList files = m_commitPanel->files(m_openMode);
    const QString path = m_openFile.value("path").toString();
    for (const QVariant &v : files)
        if (v.toMap().value("path").toString() == path) return showFile(v.toMap(), m_openMode); // still there: reload its diff
    // the file left this list: move on to the next one in the same list
    if (files.isEmpty()) return closeDiff();
    int next = 0;
    while (next < files.size() && files[next].toMap().value("path").toString() < path) ++next;
    showFile(files[std::min(next, int(files.size()) - 1)].toMap(), m_openMode);
}

// ---------- branches, tags, stashes ----------

void MainWindow::gitThen(const QString &label, const QStringList &args, const QString &retryIf, const QString &question,
    const QStringList &retry, const QString &undoLabel, const QString &undoMode) {
    if (m_repo->opRunning()) return;
    if (!retryIf.isEmpty()) {
        auto *once = new QObject(this);
        connect(m_repo, &Repo::opFinished, once, [=, this](bool ok) {
            once->deleteLater();
            if (ok || !m_repo->opOutput().contains(retryIf)) return;
            if (QMessageBox::question(this, label, question) == QMessageBox::Yes) m_repo->run(label, retry);
        });
    }
    m_repo->run(label, args, undoLabel, undoMode);
}

void MainWindow::checkout(const QVariantMap &e) {
    const QString kind = e.value("kind").toString(), name = e.value("name").toString();
    if (kind == QLatin1String("local")) {
        if (!e.value("current").toBool()) gitThen(QStringLiteral("Checkout ") + name, {QStringLiteral("switch"), name});
        return;
    }
    if (kind == QLatin1String("remote")) {
        // origin/feature: switch to the local "feature", creating it to track the remote if needed
        const QString local = name.section(QLatin1Char('/'), 1);
        for (const QVariant &v : m_repo->branches()) {
            const QVariantMap b = v.toMap();
            if (b.value("kind") == QLatin1String("local") && b.value("name") == local)
                return gitThen(QStringLiteral("Checkout ") + local, {QStringLiteral("switch"), local});
        }
        return gitThen(QStringLiteral("Checkout ") + local, {QStringLiteral("switch"), QStringLiteral("-c"), local, QStringLiteral("--track"), name});
    }

    // a commit or tag: detached HEAD
    gitThen(QStringLiteral("Checkout ") + e.value("id").toString().left(8),
        {QStringLiteral("switch"), QStringLiteral("--detach"), e.value("id").toString()});
}

void MainWindow::createBranch(const QString &at) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New branch"), QStringLiteral("Branch name (created at %1, then checked out):").arg(at.left(8)),
        QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) return;
    gitThen(QStringLiteral("Create branch ") + name, {QStringLiteral("switch"), QStringLiteral("-c"), name, at});
}

void MainWindow::deleteBranch(const QString &name) {
    if (QMessageBox::question(this, QStringLiteral("Delete branch"), QStringLiteral("Delete the local branch \"%1\"?").arg(name)) != QMessageBox::Yes)
        return;
    gitThen(QStringLiteral("Delete branch ") + name, {QStringLiteral("branch"), QStringLiteral("-d"), name}, QStringLiteral("not fully merged"),
        QStringLiteral("\"%1\" has commits that are not merged anywhere. Delete it anyway? (git reflog keeps them for a while.)").arg(name),
        {QStringLiteral("branch"), QStringLiteral("-D"), name});
}

void MainWindow::stashChanges() {
    bool ok = false;
    const QString msg = QInputDialog::getText(this, QStringLiteral("Stash changes"), QStringLiteral("Stash all changes, including untracked files. Message (optional):"),
        QLineEdit::Normal, QString(), &ok)
                            .trimmed();
    if (!ok) return;
    QStringList args{QStringLiteral("stash"), QStringLiteral("push"), QStringLiteral("--include-untracked")};
    if (!msg.isEmpty()) args << QStringLiteral("-m") << msg;
    gitThen(QStringLiteral("Stash"), args);
}

void MainWindow::startRebase(const QString &from) {
    QString err, base;
    const QVariantList items = m_repo->rebasePlanFrom(from, &err, &base);
    if (!err.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Interactive rebase"), err);
        return;
    }
    closeDiff();
    m_rebase->start(base, m_repo->branch(), items);
    m_center->setCurrentWidget(m_rebase);
}

// Before a push rebases onto a remote branch that moved on, ask.
void MainWindow::askBeforePull() {
    m_repo->setAskPull([this](const QString &branch, int commits, bool canForce) {
        if (qEnvironmentVariableIsSet("TOWEL_DEMO_SCRIPT")) // a scripted run pulls, or forces with TOWEL_DEMO_FORCE
            return qEnvironmentVariableIsSet("TOWEL_DEMO_FORCE") && canForce ? Repo::PullAnswer::Force : Repo::PullAnswer::Pull;
        QMessageBox box(QMessageBox::Question, QStringLiteral("Remote moved on"),
            QStringLiteral("%1 has %2 new commit%3 this branch doesn't have.").arg(branch).arg(commits).arg(commits == 1 ? QString() : QStringLiteral("s")),
            QMessageBox::Cancel, this);
        box.setInformativeText(QStringLiteral("Pull them first, moving your unpushed commits on top, then push?"));
        QPushButton *pull = box.addButton(QStringLiteral("Pull, then push"), QMessageBox::AcceptRole);
        QPushButton *force = canForce ? box.addButton(QStringLiteral("Overwrite remote (force)"), QMessageBox::DestructiveRole) : 0;
        if (force) force->setToolTip(QStringLiteral("For when you rewrote commits you had already pushed. Their %1 new commits are dropped from %2.").arg(commits).arg(branch));
        box.setDefaultButton(pull);
        box.exec();
        if (box.clickedButton() == pull) return Repo::PullAnswer::Pull;

        return force && box.clickedButton() == force ? Repo::PullAnswer::Force : Repo::PullAnswer::Cancel;
    });
}

// Fetch quietly every 5 minutes, unless turned off in the Fetch button's right-click menu.
void MainWindow::autoFetch(QPushButton *fetch) {
    const auto on = [] { return QSettings().value("autoFetch", true).toBool(); };
    fetch->setToolTip(QStringLiteral("git fetch --all --prune\nRight-click: fetch automatically every 5 minutes"));
    fetch->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fetch, &QWidget::customContextMenuRequested, this, [fetch, on](const QPoint &pos) {
        QMenu menu;
        QAction *a = menu.addAction(QStringLiteral("Fetch automatically every 5 minutes"));
        a->setCheckable(true);
        a->setChecked(on());
        if (menu.exec(fetch->mapToGlobal(pos)) == a) QSettings().setValue("autoFetch", a->isChecked());
    });
    auto *timer = new QTimer(this);
    timer->setInterval(5 * 60 * 1000);
    connect(timer, &QTimer::timeout, this, [this, on] {
        if (on()) m_repo->fetchQuietly();
    });
    timer->start();
}

void MainWindow::commitMenu(int row, const QPoint &pos) {
    const QString id = m_repo->commits()->idAt(row);
    if (id.isEmpty()) return;
    QMenu menu(this);
    if (id == QLatin1String("WIP")) {
        if (const QString seq = m_repo->sequencerState(); !seq.isEmpty()) {
            if (seq != QLatin1String("merge"))
                menu.addAction(QStringLiteral("Continue %1 (after staging the resolved files)").arg(seq), this, [this, seq] {
                    gitThen(QStringLiteral("Continue ") + seq, {seq, QStringLiteral("--continue")});
                });
            menu.addAction(QStringLiteral("Abort %1").arg(seq), this, [this, seq] { gitThen(QStringLiteral("Abort ") + seq, {seq, QStringLiteral("--abort")}); });
            menu.addSeparator();
        }
        menu.addAction(QStringLiteral("Stash all changes…"), this, &MainWindow::stashChanges);
        menu.exec(pos);
        return;
    }

    // branches pointing here
    QList<QVariantMap> here;
    for (const QVariant &v : m_repo->branches())
        if (v.toMap().value("id") == id) here << v.toMap();
    for (const QVariantMap &b : here) {
        if (b.value("current").toBool()) continue;
        menu.addAction(QStringLiteral("Checkout %1").arg(b.value("name").toString()), this, [this, b] { checkout(b); });
    }
    menu.addAction(QStringLiteral("Checkout this commit (detached)"), this, [this, id] { checkout({{"kind", "commit"}, {"id", id}}); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("New branch here…"), this, [this, id] { createBranch(id); });
    menu.addAction(QStringLiteral("New tag here…"), this, [this, id] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("New tag"), QStringLiteral("Tag name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (ok && !name.isEmpty()) gitThen(QStringLiteral("Create tag ") + name, {QStringLiteral("tag"), name, id});
    });
    menu.addSeparator();
    const QString shortID = id.left(8);
    const bool onHead = m_repo->commits()->indexOf(id) >= 0 && id == m_repo->headID();
    if (!onHead)
        menu.addAction(QStringLiteral("Cherry-pick onto current branch"), this, [this, id, shortID] {
            gitThen(QStringLiteral("Cherry-pick ") + shortID, {QStringLiteral("cherry-pick"), id}, {}, {}, {}, QStringLiteral("cherry-pick"));
        });
    menu.addAction(QStringLiteral("Revert (new commit that undoes it)"), this, [this, id, shortID] {
        gitThen(QStringLiteral("Revert ") + shortID, {QStringLiteral("revert"), QStringLiteral("--no-edit"), id}, {}, {}, {}, QStringLiteral("revert"));
    });
    if (!onHead && !m_repo->branch().isEmpty()) addResetMenu(&menu, id);
    const int reword = m_repo->rewordState(id);
    if (reword == 1 && !m_repo->upstream().isEmpty()) addPushActions(&menu, id);
    if (reword) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Interactive rebase from here…"), this, [this, id] { startRebase(id); });
        menu.addAction(QStringLiteral("Edit message…"), this, &MainWindow::editMessage);
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("Copy short SHA"), this, [this, id] { m_repo->copy(id.left(7)); });
    menu.addAction(QStringLiteral("Copy full SHA"), this, [this, id] { m_repo->copy(id); });
    menu.exec(pos);
}

// Soft/mixed/hard reset of the current branch to commit `id`.
void MainWindow::addResetMenu(QMenu *menu, const QString &id) {
    QMenu *reset = menu->addMenu(QStringLiteral("Reset %1 to here").arg(m_repo->branch()));
    reset->addAction(QStringLiteral("Soft: keep all changes staged"), this, [this, id] {
        gitThen(QStringLiteral("Reset (soft)"), {QStringLiteral("reset"), QStringLiteral("--soft"), id}, {}, {}, {}, QStringLiteral("reset"), QStringLiteral("--soft"));
    });
    reset->addAction(QStringLiteral("Mixed: keep changes, unstaged"), this, [this, id] {
        gitThen(QStringLiteral("Reset (mixed)"), {QStringLiteral("reset"), QStringLiteral("--mixed"), id}, {}, {}, {}, QStringLiteral("reset"), QStringLiteral("--mixed"));
    });
    reset->addAction(QStringLiteral("Hard: discard all changes…"), this, [this, id] {
        if (QMessageBox::warning(this, QStringLiteral("Hard reset"),
                QStringLiteral("Reset %1 to %2 and DISCARD all uncommitted changes? Uncommitted work cannot be recovered.")
                    .arg(m_repo->branch(), id.left(8)),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
            gitThen(QStringLiteral("Reset (hard)"), {QStringLiteral("reset"), QStringLiteral("--hard"), id}, {}, {}, {}, QStringLiteral("reset"));
    });
}

// Push actions for an unpushed commit of the current branch.
void MainWindow::addPushActions(QMenu *menu, const QString &id) {
    menu->addSeparator();
    menu->addAction(QStringLiteral("Push up to here"), this, [this, id] { m_repo->pushPart(id, false); });
    menu->addAction(QStringLiteral("Push only this commit"), this, [this, id] { m_repo->pushPart(id, true); });
}

void MainWindow::refMenu(const QVariantMap &e, const QPoint &pos) {
    const QString kind = e.value("kind").toString(), name = e.value("name").toString(), id = e.value("id").toString();
    QMenu menu(this);
    if (kind == QLatin1String("local") || kind == QLatin1String("remote")) {
        if (!e.value("current").toBool()) menu.addAction(QStringLiteral("Checkout"), this, [this, e] { checkout(e); });
        menu.addAction(QStringLiteral("New branch from here…"), this, [this, id] { createBranch(id); });
        if (kind == QLatin1String("local")) {
            menu.addAction(QStringLiteral("Rename…"), this, [this, name] {
                bool ok = false;
                const QString to = QInputDialog::getText(this, QStringLiteral("Rename branch"), QStringLiteral("New name:"), QLineEdit::Normal, name, &ok).trimmed();
                if (ok && !to.isEmpty() && to != name) gitThen(QStringLiteral("Rename branch"), {QStringLiteral("branch"), QStringLiteral("-m"), name, to});
            });
            if (!e.value("current").toBool()) {
                menu.addSeparator();
                menu.addAction(QStringLiteral("Delete…"), this, [this, name] { deleteBranch(name); });
            }
        }
    } else if (kind == QLatin1String("tag")) {
        menu.addAction(QStringLiteral("Checkout (detached)"), this, [this, id] { checkout({{"kind", "commit"}, {"id", id}}); });
        menu.addAction(QStringLiteral("Delete tag…"), this, [this, name] {
            if (QMessageBox::question(this, QStringLiteral("Delete tag"), QStringLiteral("Delete the local tag \"%1\"?").arg(name)) == QMessageBox::Yes)
                gitThen(QStringLiteral("Delete tag ") + name, {QStringLiteral("tag"), QStringLiteral("-d"), name});
        });
    } else if (kind == QLatin1String("stash")) {
        const QString ref = QStringLiteral("stash@{%1}").arg(e.value("index").toInt());
        menu.addAction(QStringLiteral("Pop (apply and drop)"), this, [this, ref] { gitThen(QStringLiteral("Pop stash"), {QStringLiteral("stash"), QStringLiteral("pop"), ref}); });
        menu.addAction(QStringLiteral("Apply (keep stash)"), this, [this, ref] { gitThen(QStringLiteral("Apply stash"), {QStringLiteral("stash"), QStringLiteral("apply"), ref}); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Drop…"), this, [this, ref, name] {
            if (QMessageBox::question(this, QStringLiteral("Drop stash"), QStringLiteral("Drop \"%1\"? Its changes are lost.").arg(name)) == QMessageBox::Yes)
                gitThen(QStringLiteral("Drop stash"), {QStringLiteral("stash"), QStringLiteral("drop"), ref});
        });
    }
    menu.addSeparator();
    menu.addAction(kind == QLatin1String("stash") ? QStringLiteral("Copy SHA") : QStringLiteral("Copy name"), this,
        [this, kind, name, id] { m_repo->copy(kind == QLatin1String("stash") ? id : name); });
    menu.exec(pos);
}

void MainWindow::zoomBy(int steps) {
    // 10% steps on a log scale, so in and out are symmetric; 0 resets
    const double z = steps == 0 ? 1.0 : theme::zoom() * std::pow(1.1, steps);
    theme::setZoom(std::abs(z - 1.0) < 0.03 ? 1.0 : z);
    QSettings().setValue("zoom", theme::zoom());
    QToolTip::showText(mapToGlobal(rect().center()), QStringLiteral("Zoom %1%").arg(qRound(theme::zoom() * 100)), this, {}, 1200);
}

bool MainWindow::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent *>(e);
        if (we->modifiers() & Qt::ControlModifier) {
            m_zoomWheel += we->angleDelta().y();
            const int steps = m_zoomWheel / 120; // one notch = one step; touchpads accumulate
            if (steps) {
                m_zoomWheel -= steps * 120;
                zoomBy(steps);
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::updateUndo() {
    const QString u = m_repo->undoLabel(), r = m_repo->redoLabel();
    m_undoBtn->setEnabled(!u.isEmpty());
    m_undoBtn->setToolTip(u.isEmpty() ? QStringLiteral("Nothing to undo")
                                      : QStringLiteral("Undo %1 (Ctrl+Z)%2").arg(u, r.isEmpty() ? QString() : QStringLiteral("\nRedo %1: Ctrl+Shift+Z").arg(r)));
}

void MainWindow::closeDiff() {
    if (m_openFile.isEmpty()) return;
    m_openFile.clear();
    m_openMode.clear();
    m_details->setOpenPath(QString());
    m_commitPanel->setOpenFile(QString(), QString());
    m_repo->clearDiff();
    m_center->setCurrentWidget(m_commits);
    m_commits->setFocus();
}

void MainWindow::stepFile(int delta) {
    const QVariantList files = m_openMode.isEmpty() ? m_repo->details().value("files").toList() : m_commitPanel->files(m_openMode);
    if (m_openFile.isEmpty() || files.isEmpty()) return;
    int i = 0;
    for (int k = 0; k < files.size(); ++k)
        if (files[k].toMap().value("path") == m_openFile.value("path")) i = k;
    i = std::clamp(i + delta, 0, int(files.size()) - 1);
    showFile(files[i].toMap(), m_openMode);
}

void MainWindow::openDialog() {
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Open a git repository"), m_repo->path());
    if (!dir.isEmpty()) {
        m_selectedID.clear();
        closeDiff();
        m_repo->open(dir);
    }
}

void MainWindow::searchNext(bool back) {
    const auto &hits = m_repo->commits()->hits();
    if (hits.isEmpty()) return;
    m_hitPos = (m_hitPos + (back ? -1 : 1) + int(hits.size())) % int(hits.size());
    m_commits->setCurrent(hits[m_hitPos]);
    m_hitLabel->setText(QStringLiteral("%1 / %2").arg(m_hitPos + 1).arg(hits.size()));
}

void MainWindow::editMessage() {
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

// Dev aid: letters as plain keys, '<' and '>' as Alt+Up / Alt+Down, into the
// diff or the rebase list, whichever is showing.
void MainWindow::sendDemoKeys(const QString &keys) {
    QWidget *target = m_center->currentWidget() == m_rebase ? static_cast<QWidget *>(m_rebase->findChild<RebaseList *>()) : m_diff->area();
    if (!target) return;
    for (QChar c : keys) {
        const bool move = c == QLatin1Char('<') || c == QLatin1Char('>');
        const int key = move ? (c == QLatin1Char('<') ? Qt::Key_Up : Qt::Key_Down) : Qt::Key_A + (c.toUpper().unicode() - 'A');
        QKeyEvent ev(QEvent::KeyPress, key, move ? Qt::AltModifier : Qt::NoModifier, move ? QString() : QString(c));
        QApplication::sendEvent(target, &ev);
    }
}

void MainWindow::runDemo() {
    // TOWEL_DEMO_SCRIPT="stage:a.txt;open:staged:a.txt;key:jjs;commit:message" runs steps 900 ms apart
    const QStringList steps = qEnvironmentVariable("TOWEL_DEMO_SCRIPT").split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (int k = 0; k < steps.size(); ++k) {
        QTimer::singleShot(1500 + 900 * k, this, [this, step = steps[k]] {
            const QString arg = step.section(QLatin1Char(':'), 1);
            if (step.startsWith(QLatin1String("stage:")))
                m_repo->stage({arg});
            else if (step.startsWith(QLatin1String("unstage:")))
                m_repo->unstage({arg});
            else if (step == QLatin1String("stageall"))
                m_repo->stageAll();
            else if (step == QLatin1String("undo"))
                m_repo->undo();
            else if (step.startsWith(QLatin1String("rebase:")))
                startRebase(arg); // then key: steps go to the list
            else if (step == QLatin1String("rebasego"))
                m_rebase->findChildren<QPushButton *>().last()->click();
            else if (step.startsWith(QLatin1String("pushto:"))) m_repo->pushPart(arg, false);
            else if (step.startsWith(QLatin1String("pushonly:"))) m_repo->pushPart(arg, true);
            else if (step.startsWith(QLatin1String("pick:")))
                gitThen(QStringLiteral("Cherry-pick"), {QStringLiteral("cherry-pick"), arg}, {}, {}, {}, QStringLiteral("cherry-pick"));
            else if (step.startsWith(QLatin1String("mixed:")))
                gitThen(QStringLiteral("Reset (mixed)"), {QStringLiteral("reset"), QStringLiteral("--mixed"), arg}, {}, {}, {}, QStringLiteral("reset"), QStringLiteral("--mixed"));
            else if (step.startsWith(QLatin1String("checkout:"))) // checkout:remote:origin/x
                checkout({{"kind", arg.section(QLatin1Char(':'), 0, 0)}, {"name", arg.section(QLatin1Char(':'), 1)}});
            else if (step == QLatin1String("redo"))
                m_repo->redo();
            else if (step.startsWith(QLatin1String("amend:")))
                m_repo->commit(arg, true);
            else if (step == QLatin1String("unstageall"))
                m_repo->unstageAll();
            else if (step.startsWith(QLatin1String("commit:")))
                m_repo->commit(arg, false);
            else if (step.startsWith(QLatin1String("rows:"))) // rows:4-9 selects diff rows (header rows count)
                m_diff->area()->selectRows(arg.section(QLatin1Char('-'), 0, 0).toInt(), arg.section(QLatin1Char('-'), 1).toInt());
            else if (step.startsWith(QLatin1String("zoom:")))
                zoomBy(arg.toInt());                         // zoom:3 = three Ctrl+scroll notches
            else if (step.startsWith(QLatin1String("key:"))) // e.g. key:jjs: keystrokes into the diff
                sendDemoKeys(arg);
            else if (step.startsWith(QLatin1String("open:"))) {
                const QString mode = arg.section(QLatin1Char(':'), 0, 0), path = arg.section(QLatin1Char(':'), 1);
                for (const QVariant &v : m_commitPanel->files(mode))
                    if (v.toMap().value("path").toString() == path) showFile(v.toMap(), mode);
            }
        });
    }
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
