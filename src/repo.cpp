#include "repo.h"
#include "gitload.h"
#include "gitutil.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMutex>
#include <QProcess>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#ifdef __GLIBC__
#include <malloc.h>
#include <utility>
#endif

namespace {
QMutex g_pathMutex;
QString g_path;
Repo *g_instance = nullptr;

QProcessEnvironment gitEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_EDITOR"), QStringLiteral("true"));
    env.insert(QStringLiteral("GIT_SEQUENCE_EDITOR"), QStringLiteral("true"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));

    return env;
}

// Synchronous git call, for worker threads.
int gitSync(const QString &root, const QStringList &args, QString *out, const QByteArray &input = {}, const QProcessEnvironment &env = gitEnv()) {
    QProcess p;
    p.setWorkingDirectory(root);
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("git"), args);
    if (!p.waitForStarted(5000)) {
        if (out) *out = QStringLiteral("could not start git");
        return -1;
    }
    if (!input.isEmpty()) p.write(input);
    p.closeWriteChannel();
    p.waitForFinished(-1);
    if (out) *out = QString::fromUtf8(p.readAll()).trimmed();
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

// The commits of `range`, newest first, as rebase plan rows. Linear history only.
QVariantList planRange(const QString &root, const QString &range, QString *err) {
    QString out;
    gitSync(root, {QStringLiteral("rev-list"), QStringLiteral("--merges"), QStringLiteral("--count"), range}, &out);
    if (out.trimmed() != QLatin1String("0")) {
        *err = QStringLiteral("There are merge commits in this range; Towel's rebase editor handles linear history only.");
        return {};
    }
    if (gitSync(root, {QStringLiteral("log"), QStringLiteral("--format=%H%x00%an%x00%ae%x00%B%x1e"), range}, &out) != 0) {
        *err = out;
        return {};
    }
    QVariantList items;
    for (const QString &rec : out.split(QChar(0x1e), Qt::SkipEmptyParts)) {
        const QStringList f = rec.trimmed().split(QChar(0));
        if (f.size() < 4) continue;
        const QString msg = f[3].trimmed();
        items << QVariantMap{{"id", f[0]}, {"author", f[1]}, {"email", f[2]}, {"message", msg}, {"original", msg}, {"summary", msg.section(QLatin1Char('\n'), 0, 0)}};
    }
    if (items.size() > 500) {
        *err = QStringLiteral("More than 500 commits; pick a newer starting point.");
        return {};
    }
    return items;
}

struct PartialPush {
    QString remote, ref, rev, err; // push `rev` to `ref` on `remote`
    QString head;                  // HEAD's oid when checked: nothing runs if it moved since
    QString tracking;              // the remote-tracking ref of the push target
    QVariantList items;            // only: the reordered plan, newest first (empty: no reorder)
    bool behind = false;           // the remote has commits HEAD lacks: rebase onto it first
    int depth = 0;                 // behind: rev sits at HEAD~depth
    int remoteAhead = 0;           // behind: how many commits the remote has that HEAD lacks
    QString fingerprint;           // rev's author, date and message, to find it again after a rebase
};

// The ref on `remote` that `tracking` mirrors, under the default fetch refspec only.
QString remoteRefOf(const QString &root, const QString &remote, const QString &tracking) {
    QString out;
    gitSync(root, {QStringLiteral("config"), QStringLiteral("--get-all"), QStringLiteral("remote.%1.fetch").arg(remote)}, &out);
    const QString prefix = QStringLiteral("refs/remotes/") + remote + QLatin1Char('/');
    const bool standard = out.split(QLatin1Char('\n')).contains(QStringLiteral("+refs/heads/*:") + prefix + QLatin1Char('*'));

    return standard && tracking.startsWith(prefix) ? QStringLiteral("refs/heads/") + tracking.mid(prefix.size()) : QString();
}

// Where `branch` pushes (as plain git push would): remote, ref and tracking ref. Fills err when nowhere.
void pushTarget(const QString &root, const QString &branch, PartialPush *p) {
    QString out;
    gitSync(root, {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(push:remotename)%00%(push:remoteref)%00%(push)"), QStringLiteral("refs/heads/") + branch}, &out);
    const QStringList f = out.split(QChar(0));
    p->remote = f.value(0);
    p->ref = f.value(1);
    p->tracking = f.value(2);
    if (p->ref.isEmpty() && !p->remote.isEmpty()) p->ref = remoteRefOf(root, p->remote, p->tracking); // push.default simple/current
    if (p->remote.isEmpty() || p->remote == QLatin1String(".") || p->ref.isEmpty() || p->tracking.isEmpty())
        p->err = QStringLiteral("This branch has no remote branch to push to. Use Push.");
}

// Identifies a commit across a rebase: author, date and message, plus its patch-id (the change
// itself, which a clean rebase keeps).
QString fingerprintOf(const QString &root, const QString &rev) {
    QString meta, diff, id;
    gitSync(root, {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%an%x00%ae%x00%at%x00%B"), rev}, &meta);
    gitSync(root, {QStringLiteral("diff-tree"), QStringLiteral("-p"), QStringLiteral("--root"), rev}, &diff);
    gitSync(root, {QStringLiteral("patch-id"), QStringLiteral("--stable")}, &id, diff.toUtf8() + '\n');

    return id.section(QLatin1Char(' '), 0, 0) + QChar(0) + meta;
}

// Plan for pushing just p->rev: the unpushed commits with p->rev moved to be the oldest.
void planOnly(const QString &root, PartialPush *p) {
    p->items = planRange(root, p->tracking + QStringLiteral("..") + p->head, &p->err);
    qsizetype at = -1;
    for (qsizetype i = 0; i < p->items.size() && at < 0; ++i)
        if (p->items[i].toMap().value("id").toString() == p->rev) at = i;
    if (at < 0 && p->err.isEmpty()) p->err = QStringLiteral("That commit is not an unpushed commit of this branch.");
    if (at < 0 || at == p->items.size() - 1) {
        p->items.clear(); // already the oldest: no reorder
        return;
    }
    p->items.move(at, p->items.size() - 1);
}

// Worker: fetches the push target, then checks that pushing `rev` (only: just `rev`) is safe.
// With `fingerprint` (a retry after rebasing), `rev` must still be the same commit.
PartialPush planPartialPush(const QString &root, const QString &branch, const QString &rev, bool only, const QString &fingerprint) {
    PartialPush p = {};
    QString out;
    pushTarget(root, branch, &p);
    if (!p.err.isEmpty()) return p;
    if (gitSync(root, {QStringLiteral("fetch"), QStringLiteral("--quiet"), p.remote}, &out) != 0) {
        p.err = out;
        return p;
    }
    gitSync(root, {QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("HEAD")}, &out);
    if (out != QStringLiteral("refs/heads/") + branch) {
        p.err = QStringLiteral("The checked-out branch changed. Try again.");
        return p;
    }
    gitSync(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD"), rev + QStringLiteral("^{commit}")}, &out);
    p.head = out.section(QLatin1Char('\n'), 0, 0);
    p.rev = out.section(QLatin1Char('\n'), 1, 1);
    p.fingerprint = fingerprintOf(root, p.rev);
    auto ancestor = [&](const QString &a, const QString &b) {
        return gitSync(root, {QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), a, b}, nullptr) == 0;
    };
    if ((!fingerprint.isEmpty() && fingerprint != p.fingerprint) || !ancestor(p.rev, p.head) || ancestor(p.rev, p.tracking)) {
        p.err = QStringLiteral("That commit is not an unpushed commit of this branch.");
        return p;
    }
    if (!ancestor(p.tracking, p.head)) {
        if (!fingerprint.isEmpty()) {
            p.err = QStringLiteral("%1 has commits this branch doesn't. Pull first.").arg(p.tracking.section(QLatin1Char('/'), 2));
            return p;
        }
        p.behind = true;
        gitSync(root, {QStringLiteral("rev-list"), QStringLiteral("--count"), p.rev + QStringLiteral("..") + p.head}, &out);
        p.depth = out.toInt();
        gitSync(root, {QStringLiteral("rev-list"), QStringLiteral("--count"), p.head + QStringLiteral("..") + p.tracking}, &out);
        p.remoteAhead = out.toInt();
        return p;
    }
    if (only) planOnly(root, &p);
    return p;
}

} // namespace

namespace {
QString gitDirOf(const QString &root) {
    GRepo r;
    if (!openRepo(r, root)) return {};
    QString p = u8(git_repository_path(r));
    while (p.endsWith(QLatin1Char('/'))) p.chop(1);
    return p;
}

QString commonDirOf(const QString &root) {
    GRepo r;
    if (!openRepo(r, root)) return {};
    QString p = u8(git_repository_commondir(r));
    while (p.endsWith(QLatin1Char('/'))) p.chop(1);
    return p;
}
} // namespace

Repo *Repo::instance() {
    if (!g_instance) g_instance = new Repo;
    return g_instance;
}

QString Repo::currentPath() {
    QMutexLocker l(&g_pathMutex);
    return g_path;
}

Repo::Repo(QObject *parent) : QObject(parent) {}

void Repo::open(const QString &pathOrURL) {
    QString p = pathOrURL;
    if (p.startsWith(QLatin1String("file:"))) p = QUrl(p).toLocalFile();
    if (p.isEmpty()) return;
    const quint64 gen = ++m_loadGen;
    m_loading = true;
    emit loadingChanged();
    QtConcurrent::run([p] { return gitload::loadRepo(p); }).then(this, [this, gen](LoadResult res) {
        if (gen != m_loadGen) return;
        m_loading = false;
        emit loadingChanged();
        if (!res.error.isEmpty()) {
            m_error = res.error;
            emit errorChanged();
            return;
        }
        const bool sameRepo = res.root == m_path;
        if (!m_error.isEmpty()) {
            m_error.clear();
            emit errorChanged();
        }
        {
            QMutexLocker l(&g_pathMutex);
            g_path = res.root;
        }
        if (res.root != m_path) {
            m_undo.clear();
            m_redo.clear();
            emit undoChanged();
        }
        const bool switched = res.root != m_path;
        m_path = res.root;
        m_name = res.name;
        if (switched) watchGitDir();
        m_head = res.head;
        m_detached = res.detached;
        m_branch = res.currentBranch;
        m_upstream = res.upstream;
        m_ahead = res.ahead;
        m_behind = res.behind;
        m_branches = res.branches;
        m_tags = res.tags;
        m_stashes = res.stashes;
        m_lanes = res.lanes;
        m_fingerprint = res.fingerprint;
        m_commits.setData(res.root, res);
#ifdef __GLIBC__
        malloc_trim(0); // hand libgit2's freed caches back to the OS
#endif
        if (!sameRepo) {
            m_details.clear();
            emit detailsChanged();
            m_diff = DiffDoc{};
            emit diffChanged();
        }
        emit loadedChanged();
    });
}

void Repo::refresh() {
    if (!m_path.isEmpty()) open(m_path);
}

void Repo::checkChanged() {
    if (m_path.isEmpty() || m_checking || m_loading || m_opRunning || m_indexBusy || m_settling) return;
    m_checking = true;
    const QString root = m_path;
    QtConcurrent::run([root] { return gitload::fingerprint(root); }).then(this, [this, root](QString fp) {
        m_checking = false;
        if (root == m_path && !fp.isEmpty() && fp != m_fingerprint) refresh();
    });
}

// The git dir's own files (HEAD, index, packed-refs, FETCH_HEAD...) and the ref
// folders change on every commit, checkout, fetch and push, from any program.
// A few dozen inotify watches; the working tree itself is left to the poll.
void Repo::watchGitDir() {
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        m_watchDebounce = new QTimer(this);
        m_watchDebounce->setSingleShot(true);
        m_watchDebounce->setInterval(300);
        connect(m_watchDebounce, &QTimer::timeout, this, [this] {
            if (m_rescanRefs) watchRefFolders();
            m_rescanRefs = false;
            checkChanged();
            loadStatus();
        });
        auto changed = [this](const QString &path) {
            // a new branch folder (refs/heads/feature/...) needs a watch of its own
            if (path.startsWith(m_watchedCommonDir + QStringLiteral("/refs"))) m_rescanRefs = true;
            m_watchDebounce->start();
        };
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, changed);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, changed);
    }

    // a different repository: drop every watch of the old one
    if (const QStringList old = m_watcher->directories() + m_watcher->files(); !old.isEmpty()) m_watcher->removePaths(old);
    m_watchedGitDir = gitDirOf(m_path);
    m_watchedCommonDir = commonDirOf(m_path);
    if (m_watchedGitDir.isEmpty()) return;

    m_watcher->addPaths(QStringList{m_watchedGitDir, m_watchedCommonDir});
    watchRefFolders();
}

// Every folder under refs/ (up to 200), so a commit, fetch or new branch anywhere shows up.
void Repo::watchRefFolders() {
    const QString refs = m_watchedCommonDir + QStringLiteral("/refs");
    QStringList paths{refs, refs + QStringLiteral("/heads"), refs + QStringLiteral("/tags")};
    QDirIterator it(refs, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && paths.size() < 200) paths << it.next();
    const QStringList have = m_watcher->directories();
    QStringList add;
    for (const QString &p : paths)
        if (!have.contains(p)) add << p;

    if (!add.isEmpty()) m_watcher->addPaths(add);
}

void Repo::loadDetails(const QString &id) {
    if (m_path.isEmpty() || id.isEmpty()) return;
    const quint64 gen = ++m_detailsGen;
    const QString root = m_path;
    QtConcurrent::run([root, id] { return gitload::details(root, id); }).then(this, [this, gen](QVariantMap d) {
        if (gen != m_detailsGen) return;
        m_details = d;
        emit detailsChanged();
    });
}

void Repo::loadDiff(const QString &id, const QString &path, const QString &oldPath, bool full) {
    if (m_path.isEmpty()) return;
    const quint64 gen = ++m_diffGen;
    const QString root = m_path;
    m_diffLoading = true;
    emit diffLoadingChanged();
    QtConcurrent::run([=] { return buildDiffDoc(gitload::fileDiff(root, id, path, oldPath, full)); }).then(this, [this, gen](DiffDoc d) {
        if (gen != m_diffGen) return;
        m_diff = std::move(d);
        emit diffChanged();
        m_diffLoading = false;
        emit diffLoadingChanged();
    });
}

void Repo::clearDiff() {
    ++m_diffGen;
    m_diff = DiffDoc{};
    emit diffChanged();
}

void Repo::copy(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
}

QString Repo::dateString(qint64 ms) const {
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms), QStringLiteral("ddd d MMM yyyy, HH:mm"));
}

// ---------- git operations (shell out to git: same auth, hooks and config as the terminal) ----------

void Repo::setOp(bool running, const QString &label, const QString &out, bool failed) {
    m_opRunning = running;
    m_opLabel = label;
    m_opOutput = out;
    m_opFailed = failed;
    emit opChanged();
}

void Repo::dismissOp() {
    m_opCanForce = false;
    setOp(false, QString(), QString(), false);
}

void Repo::runGit(const QString &label, const QStringList &args, const QByteArray &input, const QProcessEnvironment *env, const std::function<void(bool)> &done, bool partialPush) {
    if (m_opRunning || m_path.isEmpty()) return;
    if (m_bgFetching) { // after the background fetch
        setOp(true, label, QStringLiteral("Waiting for the background fetch…"), false);
        const std::optional<QProcessEnvironment> e = env ? std::optional(*env) : std::nullopt;
        m_afterFetch = [=, this] { runGit(label, args, input, e ? &*e : nullptr, done, partialPush); };

        return;
    }
    m_opCanForce = false;
    setOp(true, label, QString(), false);
    auto *p = new QProcess(this);
    p->setWorkingDirectory(m_path);
    p->setProcessEnvironment(env ? *env : gitEnv());
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::readyRead, this, [this, p] {
        m_opOutput += QString::fromUtf8(p->readAll());
        emit opChanged();
    });
    connect(p, &QProcess::finished, this, [this, p, label, args, done, partialPush](int code, QProcess::ExitStatus st) {
        m_opOutput += QString::fromUtf8(p->readAll());
        p->deleteLater();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (!ok && args.value(0) == QLatin1String("push") && !partialPush && !args.contains(QLatin1String("--force-with-lease")) && (m_opOutput.contains(QLatin1String("non-fast-forward")) || m_opOutput.contains(QLatin1String("[rejected]"))))
            m_opCanForce = true;
        setOp(false, label, m_opOutput.trimmed(), !ok);
        journalEnd(ok);
        emit opFinished(ok);
        if (done) done(ok);
        refresh();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, label](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        setOp(false, label, QStringLiteral("Could not start git."), true);
    });
    p->start(QStringLiteral("git"), args);
    if (!input.isEmpty()) p->write(input);
    p->closeWriteChannel();
}

void Repo::pull() {
    journalBegin(QStringLiteral("pull"), QStringLiteral("--keep"));
    runGit(QStringLiteral("Pull (rebase)"), {QStringLiteral("pull"), QStringLiteral("--rebase"), QStringLiteral("--autostash")});
}

// Background fetch: no status strip, never asks for a password or passphrase (ssh has no tty and
// no askpass; credential helpers are told not to prompt). A git command started meanwhile waits
// for it (runGit), so the two never fight over ref locks.
void Repo::fetchQuietly() {
    if (m_opRunning || m_bgFetching || m_path.isEmpty()) return;
    m_bgFetching = true;
    const QString root = m_path;
    QtConcurrent::run([root] {
        QProcessEnvironment env = gitEnv();
        env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("never"));
        env.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("never"));
        gitSync(root, {QStringLiteral("-c"), QStringLiteral("credential.interactive=never"), QStringLiteral("fetch"), QStringLiteral("--all"), QStringLiteral("--prune"), QStringLiteral("--quiet")}, nullptr, {}, env);
    }).then(this, [this, root] {
        m_bgFetching = false;
        if (auto next = std::exchange(m_afterFetch, {})) {
            m_opRunning = false;
            next();
        }
        if (root == m_path) refresh();
    });
}

void Repo::fetch() {
    runGit(QStringLiteral("Fetch"), {QStringLiteral("fetch"), QStringLiteral("--all"), QStringLiteral("--prune")});
}

void Repo::push(bool force) {
    QStringList args{QStringLiteral("push")};
    if (force) args << QStringLiteral("--force-with-lease");
    if (m_upstream.isEmpty() && !m_branch.isEmpty()) {
        // first push of this branch: set upstream on the default remote
        QString remotes;
        gitSync(m_path, {QStringLiteral("remote")}, &remotes);
        const QStringList rs = remotes.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString remote = rs.contains(QStringLiteral("origin")) ? QStringLiteral("origin") : rs.value(0);
        if (remote.isEmpty()) {
            setOp(false, QStringLiteral("Push"), QStringLiteral("This repository has no remote to push to."), true);
            return;
        }
        args << QStringLiteral("-u") << remote << m_branch;
    }
    runGit(force ? QStringLiteral("Force push (with lease)") : QStringLiteral("Push"), args);
}

// Push `id`, or (only) just `id` after moving it onto the remote tip. The checks and the fetch run
// on a worker; nothing is rewritten or pushed when they fail.
void Repo::pushPart(const QString &rev, bool only, const QString &fingerprint) {
    if (m_opRunning || m_branch.isEmpty()) return;
    if (m_bgFetching) {
        setOp(true, QStringLiteral("Push"), QStringLiteral("Waiting for the background fetch…"), false);
        m_afterFetch = [=, this] { pushPart(rev, only, fingerprint); };
        return;
    }
    const QString label = (only ? QStringLiteral("Push only ") : QStringLiteral("Push up to ")) + rev.left(8);
    setOp(true, label, QString(), false);
    const QString root = m_path, branch = m_branch;
    QtConcurrent::run([=] { return planPartialPush(root, branch, rev, only, fingerprint); }).then(this, [this, label, root, branch, only](PartialPush p) {
        // HEAD must still be the branch and commit the worker checked, also after the dialog
        const auto moved = [&] { return root != m_path || headRef() != qMakePair(QStringLiteral("refs/heads/") + branch, p.head); };
        if (p.err.isEmpty() && moved()) p.err = QStringLiteral("The branch moved. Try again.");
        if (!p.err.isEmpty()) return setOp(false, label, p.err, true);
        if (p.behind && m_askPull && !m_askPull(p.tracking.section(QLatin1Char('/'), 2), p.remoteAhead)) return setOp(false, label, QStringLiteral("Cancelled: nothing was pushed."), false);
        if (moved()) return setOp(false, label, QStringLiteral("The branch moved. Try again."), true);
        m_opRunning = false;
        if (p.behind) return rebaseThenPush(p.tracking, p.depth, p.fingerprint, only);
        if (p.items.isEmpty()) return runGit(label, {QStringLiteral("push"), p.remote, p.rev + QLatin1Char(':') + p.ref}, {}, nullptr, {}, true);
        const int n = int(p.items.size());

        // the moved commit ends up the oldest of n; --empty=keep keeps all n
        const QString spec = QStringLiteral("HEAD~%1:").arg(n - 1) + p.ref;
        rebase(p.tracking, p.items, {QStringLiteral("--empty=keep")}, [this, label, remote = p.remote, spec](bool ok) {
            if (ok && sequencerState().isEmpty()) runGit(label, {QStringLiteral("push"), remote, spec}, {}, nullptr, {}, true);
        });
    });
}

// The remote moved on: rebase the branch onto it (as a pull would), then retry the push with the
// same commit, found again by position and checked by its fingerprint. A conflict stops it all.
void Repo::rebaseThenPush(const QString &tracking, int depth, const QString &fingerprint, bool only) {
    journalBegin(QStringLiteral("rebase"), QStringLiteral("--keep"));
    const QString rev = QStringLiteral("HEAD~%1").arg(depth);
    runGit(QStringLiteral("Rebase onto ") + tracking.section(QLatin1Char('/'), 2), {QStringLiteral("rebase"), QStringLiteral("--autostash"), tracking}, {}, nullptr,
        [this, rev, only, fingerprint](bool ok) {
            if (ok && sequencerState().isEmpty()) pushPart(rev, only, fingerprint);
        });
}

int Repo::rewordState(const QString &id) {
    if (m_path.isEmpty() || id == QLatin1String("WIP") || m_branch.isEmpty()) return 0;
    if (gitSync(m_path, {QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), id, QStringLiteral("HEAD")}, nullptr) != 0) return 0;
    QString out;
    gitSync(m_path, {QStringLiteral("branch"), QStringLiteral("-r"), QStringLiteral("--contains"), id}, &out);
    return out.trimmed().isEmpty() ? 1 : 2;
}

void Repo::reword(const QString &id, const QString &message) {
    if (m_opRunning || m_path.isEmpty()) return;
    const QString root = m_path;
    QString msg = message.trimmed();
    if (msg.isEmpty()) return;
    msg += QLatin1Char('\n');
    journalBegin(QStringLiteral("message edit"), QStringLiteral("--keep"));
    setOp(true, QStringLiteral("Edit commit message"), QString(), false);

    QtConcurrent::run([root, id, msg]() -> QPair<bool, QString> {
        // 1. read the original commit
        GRepo r;
        if (!openRepo(r, root)) return {false, gitErr()};
        git_oid oid;
        if (git_oid_fromstr(&oid, id.toLatin1().constData()) != 0) return {false, QStringLiteral("bad commit id")};
        GCommit c;
        if (git_commit_lookup(c.out(), r, &oid) != 0) return {false, gitErr()};
        const git_signature *a = git_commit_author(c);
        const int off = a->when.offset;
        const QString date = QStringLiteral("@%1 %2%3%4")
                                 .arg(a->when.time)
                                 .arg(off < 0 ? QLatin1Char('-') : QLatin1Char('+'))
                                 .arg(qAbs(off) / 60, 2, 10, QLatin1Char('0'))
                                 .arg(qAbs(off) % 60, 2, 10, QLatin1Char('0'));
        QStringList args{QStringLiteral("commit-tree"), oidStr(git_commit_tree_id(c))};
        for (unsigned i = 0; i < git_commit_parentcount(c); ++i) args << QStringLiteral("-p") << oidStr(git_commit_parent_id(c, i));
        QProcessEnvironment env = gitEnv();
        env.insert(QStringLiteral("GIT_AUTHOR_NAME"), u8(a->name));
        env.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), u8(a->email));
        env.insert(QStringLiteral("GIT_AUTHOR_DATE"), date);

        GRef head;
        bool isHead = false;
        if (git_repository_head(head.out(), r) == 0 && git_reference_target(head)) isHead = git_oid_equal(git_reference_target(head), &oid);

        // 2. same tree, parents and author, new message
        QString out;
        if (gitSync(root, args, &out, msg.toUtf8(), env) != 0) return {false, out};
        const QString newID = out.section(QLatin1Char('\n'), -1).trimmed();

        // 3. move the branch (HEAD) or replay the commits above it
        if (isHead) {
            if (gitSync(root, {QStringLiteral("update-ref"), QStringLiteral("-m"), QStringLiteral("towel: reword"), QStringLiteral("HEAD"), newID, id}, &out) != 0)
                return {false, out};
            return {true, QStringLiteral("Message updated.")};
        }
        if (gitSync(root, {QStringLiteral("rebase"), QStringLiteral("--rebase-merges"), QStringLiteral("--autostash"), QStringLiteral("--onto"), newID, id}, &out) != 0) {
            QString ignored;
            gitSync(root, {QStringLiteral("rebase"), QStringLiteral("--abort")}, &ignored);
            return {false, QStringLiteral("Could not replay the later commits, nothing was changed.\n") + out};
        }
        return {true, QStringLiteral("Message updated; later commits replayed.")};
    }).then(this, [this](QPair<bool, QString> res) {
        setOp(false, QStringLiteral("Edit commit message"), res.second, !res.first);
        journalEnd(res.first);
        emit opFinished(res.first);
        refresh();
    });
}

// ---------- commit panel ----------

void Repo::loadStatus() {
    if (m_path.isEmpty()) return;
    const quint64 gen = ++m_statusGen;
    const QString root = m_path;
    QtConcurrent::run([root] { return gitload::workStatus(root); }).then(this, [this, gen](gitload::WorkStatus ws) {
        if (gen != m_statusGen) return;
        m_status = QVariantMap{{"unstaged", ws.unstaged}, {"staged", ws.staged}, {"conflicted", ws.conflicted}, {"error", ws.error}};
        m_hasHead = ws.hasHead;

        // our own index changes must not trigger a full history reload
        if (m_settling && !m_indexBusy && !ws.fingerprint.isEmpty()) m_fingerprint = ws.fingerprint;
        if (!m_indexBusy) m_settling = false;
        emit statusChanged();
    });
}

namespace {
// Literal pathspecs, NUL-separated, for --pathspec-from-file=- --pathspec-file-nul
QByteArray pathspecs(const QStringList &paths) {
    QByteArray out;
    for (const QString &p : paths) {
        out += ":(literal)" + p.toUtf8();
        out.append(char(0));
    }
    return out;
}

const QStringList pathspecArgs{QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul")};
} // namespace

void Repo::indexOp(const QStringList &args, const QByteArray &input) {
    m_indexQueue.append({args, input, {}});
    pumpIndexOps();
}

void Repo::applyLines(const QStringList &paths, bool staged, const QSet<int> &selDel, const QSet<int> &selAdd) {
    if (paths.isEmpty() || (selDel.isEmpty() && selAdd.isEmpty())) return;
    const QString path = paths.first();
    const QString root = m_path;
    const bool hasHead = m_hasHead;
    m_indexQueue.append({{}, {}, [=](QStringList &args, QByteArray &input, QString &err) {
                             bool whole = false;
                             input = gitload::partialPatch(root, staged, path, selDel, selAdd, &err, &whole);
                             if (whole) { // new, deleted, renamed or binary: the whole file
                                 input = pathspecs(paths);
                                 if (!staged)
                                     args = QStringList{QStringLiteral("add"), QStringLiteral("-A")} + pathspecArgs;
                                 else if (hasHead)
                                     args = QStringList{QStringLiteral("restore"), QStringLiteral("--staged")} + pathspecArgs;
                                 else
                                     args = QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-q")} + pathspecArgs;
                                 return;
                             }
                             args = {QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("--whitespace=nowarn")};
                             if (staged) args << QStringLiteral("--reverse");
                             args << QStringLiteral("-");
                         }});
    pumpIndexOps();
}

void Repo::failIndexOp(const QString &out) {
    m_indexBusy = false;
    m_indexQueue.clear();
    setOp(false, QStringLiteral("Staging"), out.isEmpty() ? QStringLiteral("git failed") : out, true);
    emit indexOpFailed(out);
    m_settling = true;
    loadStatus();
}

// Index operations run one at a time (git takes index.lock), then refresh only the status.
void Repo::pumpIndexOps() {
    if (m_indexBusy || m_indexQueue.isEmpty() || m_path.isEmpty()) return;
    IndexJob job = m_indexQueue.takeFirst();
    m_indexBusy = true;
    if (!job.gen) return runIndexJob(job.args, job.input);
    QtConcurrent::run([gen = job.gen] {
        IndexJob out = {};
        QString err;
        gen(out.args, out.input, err);

        return std::make_pair(out, err);
    }).then(this, [this](std::pair<IndexJob, QString> res) {
        if (!res.second.isEmpty()) return failIndexOp(res.second);
        runIndexJob(res.first.args, res.first.input);
    });
}

void Repo::runIndexJob(const QStringList &args, const QByteArray &input) {
    auto *p = new QProcess(this);
    p->setWorkingDirectory(m_path);
    p->setProcessEnvironment(gitEnv());
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(p->readAll()).trimmed();
        p->deleteLater();
        if (st != QProcess::NormalExit || code != 0) return failIndexOp(out);
        m_indexBusy = false;
        if (m_indexQueue.isEmpty()) {
            m_settling = true;
            loadStatus();
        }
        pumpIndexOps();
    });
    p->start(QStringLiteral("git"), args);
    if (!input.isEmpty()) p->write(input);
    p->closeWriteChannel();
}

void Repo::stage(const QStringList &paths) {
    if (paths.isEmpty()) return;
    indexOp(QStringList{QStringLiteral("add"), QStringLiteral("-A")} + pathspecArgs, pathspecs(paths));
}

void Repo::unstage(const QStringList &paths) {
    if (paths.isEmpty()) return;
    if (m_hasHead)
        indexOp(QStringList{QStringLiteral("restore"), QStringLiteral("--staged")} + pathspecArgs, pathspecs(paths));
    else
        indexOp(QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q")} + pathspecArgs, pathspecs(paths));
}

void Repo::stageAll() {
    indexOp({QStringLiteral("add"), QStringLiteral("-A")}, {});
}

void Repo::unstageAll() {
    if (m_hasHead)
        indexOp({QStringLiteral("reset"), QStringLiteral("-q")}, {});
    else
        indexOp({QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q"), QStringLiteral(".")}, {});
}

void Repo::commit(const QString &message, bool amend) {
    QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend) args << QStringLiteral("--amend");
    QByteArray msg = message.trimmed().toUtf8();
    msg += '\n';
    auto *once = new QObject(this);
    connect(this, &Repo::opFinished, once, [this, once](bool ok) {
        once->deleteLater();
        if (ok) emit committed();
    });
    journalBegin(amend ? QStringLiteral("amend") : QStringLiteral("commit"), QStringLiteral("--soft"));
    if (m_pending && !amend) m_pending->message = message.trimmed();
    runGit(amend ? QStringLiteral("Amend") : QStringLiteral("Commit"), args, msg);
}

// ---------- undo journal ----------

QString Repo::sequencerState() const {
    GRepo r;
    if (!openRepo(r, m_path)) return {};
    switch (git_repository_state(r)) {
    case GIT_REPOSITORY_STATE_CHERRYPICK:
    case GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE: return QStringLiteral("cherry-pick");
    case GIT_REPOSITORY_STATE_REVERT:
    case GIT_REPOSITORY_STATE_REVERT_SEQUENCE: return QStringLiteral("revert");
    case GIT_REPOSITORY_STATE_MERGE: return QStringLiteral("merge");
    case GIT_REPOSITORY_STATE_REBASE:
    case GIT_REPOSITORY_STATE_REBASE_INTERACTIVE:
    case GIT_REPOSITORY_STATE_REBASE_MERGE: return QStringLiteral("rebase");
    default: return {};
    }
}

QVariantList Repo::rebasePlanFrom(const QString &from, QString *err, QString *base) {
    QString out;
    if (m_branch.isEmpty()) {
        *err = QStringLiteral("Check out a branch first (HEAD is detached).");
        return {};
    }
    if (gitSync(m_path, {QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), from, QStringLiteral("HEAD")}, nullptr) != 0) {
        *err = QStringLiteral("That commit is not on the current branch.");
        return {};
    }
    const bool root = gitSync(m_path, {QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"), from + QStringLiteral("^")}, &out) != 0;
    *base = root ? QString() : out.trimmed();
    return planRange(m_path, root ? QStringLiteral("HEAD") : *base + QStringLiteral("..HEAD"), err);
}

// Runs `git rebase -i` with a prepared todo. Messages are applied with
// `exec git commit --amend -F file`, so git never needs an editor.
void Repo::rebase(const QString &base, const QVariantList &items, const QStringList &extra, const std::function<void(bool)> &done) {
    if (m_opRunning || items.isEmpty()) return;
    QString gitDir;
    gitSync(m_path, {QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")}, &gitDir);
    QDir dir(gitDir.trimmed() + QStringLiteral("/towel-rebase"));
    dir.removeRecursively();
    QDir().mkpath(dir.path());
    auto quote = [](QString s) { return QLatin1Char('\'') + s.replace(QLatin1Char('\''), QStringLiteral("'\\''")) + QLatin1Char('\''); };

    // oldest first; group = a pick followed by its squash/fixup rows
    QStringList todo;
    int n = 0;
    QString groupMsg, origMsg;
    bool groupOpen = false, needAmend = false;
    auto closeGroup = [&] {
        if (groupOpen && needAmend) {
            const QString file = dir.filePath(QStringLiteral("msg-%1").arg(n++));
            QFile f(file);
            if (f.open(QIODevice::WriteOnly)) f.write(groupMsg.toUtf8() + '\n');
            todo << QStringLiteral("exec git commit --amend --only --allow-empty --no-verify -q -F ") + quote(file);
        }
        groupOpen = needAmend = false;
    };
    for (qsizetype i = items.size() - 1; i >= 0; --i) {
        const QVariantMap it = items[i].toMap();
        const QString action = it.value("action").toString(), id = it.value("id").toString();
        if (action == QLatin1String("drop")) continue;
        if ((action == QLatin1String("squash") || action == QLatin1String("fixup")) && groupOpen) {
            todo << QStringLiteral("fixup ") + id;
            if (action == QLatin1String("squash")) {
                groupMsg += QStringLiteral("\n\n") + it.value("original").toString();
                needAmend = true;
            }
            continue;
        }
        closeGroup();
        todo << QStringLiteral("pick ") + id;
        groupOpen = true;
        groupMsg = it.value("message").toString().trimmed();
        needAmend = groupMsg != it.value("original").toString().trimmed();
    }
    closeGroup();
    QFile tf(dir.filePath(QStringLiteral("todo")));
    if (!tf.open(QIODevice::WriteOnly)) return setOp(false, QStringLiteral("Rebase"), QStringLiteral("Could not write the rebase plan."), true);
    tf.write(todo.join(QLatin1Char('\n')).toUtf8() + '\n');
    tf.close();

    QProcessEnvironment env = gitEnv();
    env.insert(QStringLiteral("GIT_SEQUENCE_EDITOR"), QStringLiteral("cp ") + quote(tf.fileName()));
    QStringList args{QStringLiteral("rebase"), QStringLiteral("-i"), QStringLiteral("--autostash"), QStringLiteral("--no-autosquash")};
    args << extra << (base.isEmpty() ? QStringLiteral("--root") : base);
    journalBegin(QStringLiteral("rebase"), QStringLiteral("--keep"));
    auto *once = new QObject(this);
    connect(this, &Repo::opFinished, once, [this, once, dir] {
        once->deleteLater();
        if (sequencerState() != QLatin1String("rebase")) QDir(dir).removeRecursively(); // stopped rebases still need it
    });
    runGit(QStringLiteral("Rebase"), args, {}, &env, done);
}

QPair<QString, QString> Repo::headRef() const {
    GRepo r;
    if (!openRepo(r, m_path)) return {};
    GRef head;
    if (git_repository_head(head.out(), r) != 0 || !git_reference_target(head)) return {};
    const bool detached = git_repository_head_detached(r) == 1;
    return {detached ? QStringLiteral("HEAD") : u8(git_reference_name(head)), oidStr(git_reference_target(head))};
}

void Repo::journalBegin(const QString &label, const QString &mode) {
    m_pending.reset();
    if (m_opRunning) return;
    const auto [ref, oid] = headRef();
    if (oid.isEmpty()) return; // unborn: the first commit is not undoable
    m_pending = UndoEntry{label, ref, oid, {}, {}, mode};
}

void Repo::journalEnd(bool ok) {
    if (!m_pending) return;
    UndoEntry e = *m_pending;
    m_pending.reset();
    if (!ok) return;
    const auto [ref, oid] = headRef();
    if (ref != e.ref || oid.isEmpty() || oid == e.before) return;
    e.after = oid;
    m_undo.append(e);
    if (m_undo.size() > 50) m_undo.removeFirst();
    m_redo.clear();
    emit undoChanged();
}

QString Repo::undoLabel() const {
    return m_undo.isEmpty() ? QString() : m_undo.last().label;
}

QString Repo::redoLabel() const {
    return m_redo.isEmpty() ? QString() : m_redo.last().label;
}

void Repo::undo() {
    if (m_undo.isEmpty() || m_opRunning) return;
    moveRef(m_undo.last(), true);
}

void Repo::redo() {
    if (m_redo.isEmpty() || m_opRunning) return;
    moveRef(m_redo.last(), false);
}

// Undo = move the ref back to `before`; redo = forward to `after`. Only when
// the ref is still exactly where the journal left it.
void Repo::moveRef(const UndoEntry &e, bool back) {
    const QString label = (back ? QStringLiteral("Undo ") : QStringLiteral("Redo ")) + e.label;
    const auto [ref, oid] = headRef();
    const QString from = back ? e.after : e.before, to = back ? e.before : e.after;
    if (ref != e.ref || oid != from) {
        setOp(false, label, QStringLiteral("The branch has moved since then, so this can no longer be undone here. "
                                           "git reflog still has every previous position."),
            true);
        (back ? m_undo : m_redo).clear();
        emit undoChanged();

        return;
    }

    // undo drops the commits in after..before; refuse if they are already pushed
    auto ancestor = [this](const QString &a, const QString &b) {
        return gitSync(m_path, {QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), a, b}, nullptr) == 0;
    };
    if (back && !m_upstream.isEmpty() && !ancestor(e.after, e.before) && ancestor(e.after, m_upstream)) {
        setOp(false, label, QStringLiteral("Already pushed to %1. Undoing it would need a force push, so Towel won't do it.").arg(m_upstream), true);
        return;
    }
    auto *once = new QObject(this);
    connect(this, &Repo::opFinished, once, [this, once, e, back](bool ok) {
        once->deleteLater();
        if (!ok) return;
        (back ? m_undo : m_redo).removeLast();
        (back ? m_redo : m_undo).append(e);
        emit undoChanged();
        if (back && !e.message.isEmpty()) emit restoreMessage(e.message);
        loadStatus();
    });
    runGit(label, {QStringLiteral("reset"), e.mode, to});
}

QString Repo::headID() const {
    GRepo r;
    if (!openRepo(r, m_path)) return {};
    GRef head;
    if (git_repository_head(head.out(), r) != 0 || !git_reference_target(head)) return {};
    return oidStr(git_reference_target(head));
}

QString Repo::headMessage() const {
    GRepo r;
    if (!openRepo(r, m_path)) return {};
    GObj c;
    if (git_revparse_single(c.out(), r, "HEAD^{commit}") != 0) return {};
    return u8(git_commit_message(reinterpret_cast<git_commit *>(c.get()))).trimmed();
}
