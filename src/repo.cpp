#include "repo.h"
#include "gitload.h"
#include "gitutil.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMutex>
#include <QProcess>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace {
QMutex g_pathMutex;
QString g_path;
Repo *g_instance = nullptr;

QProcessEnvironment gitEnv()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_EDITOR"), QStringLiteral("true"));
    env.insert(QStringLiteral("GIT_SEQUENCE_EDITOR"), QStringLiteral("true"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    return env;
}

// Synchronous git call, for worker threads.
int gitSync(const QString &root, const QStringList &args, QString *out, const QByteArray &input = {}, const QProcessEnvironment &env = gitEnv())
{
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
} // namespace

Repo *Repo::instance()
{
    if (!g_instance) g_instance = new Repo;
    return g_instance;
}

QString Repo::currentPath()
{
    QMutexLocker l(&g_pathMutex);
    return g_path;
}

Repo::Repo(QObject *parent) : QObject(parent) {}

void Repo::open(const QString &pathOrUrl)
{
    QString p = pathOrUrl;
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
        m_path = res.root;
        m_name = res.name;
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

void Repo::refresh()
{
    if (!m_path.isEmpty()) open(m_path);
}

void Repo::checkChanged()
{
    if (m_path.isEmpty() || m_checking || m_loading || m_opRunning || m_indexBusy || m_settling) return;
    m_checking = true;
    const QString root = m_path;
    QtConcurrent::run([root] { return gitload::fingerprint(root); }).then(this, [this, root](QString fp) {
        m_checking = false;
        if (root == m_path && !fp.isEmpty() && fp != m_fingerprint) refresh();
    });
}

void Repo::loadDetails(const QString &id)
{
    if (m_path.isEmpty() || id.isEmpty()) return;
    const quint64 gen = ++m_detailsGen;
    const QString root = m_path;
    QtConcurrent::run([root, id] { return gitload::details(root, id); }).then(this, [this, gen](QVariantMap d) {
        if (gen != m_detailsGen) return;
        m_details = d;
        emit detailsChanged();
    });
}

void Repo::loadDiff(const QString &id, const QString &path, const QString &oldPath, bool full)
{
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

void Repo::clearDiff()
{
    ++m_diffGen;
    m_diff = DiffDoc{};
    emit diffChanged();
}

void Repo::copy(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

QString Repo::dateString(qint64 ms) const
{
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms), QStringLiteral("ddd d MMM yyyy, HH:mm"));
}

// ---------- git operations (shell out to git: same auth, hooks and config as the terminal) ----------

void Repo::setOp(bool running, const QString &label, const QString &out, bool failed)
{
    m_opRunning = running;
    m_opLabel = label;
    m_opOutput = out;
    m_opFailed = failed;
    emit opChanged();
}

void Repo::dismissOp()
{
    m_opCanForce = false;
    setOp(false, QString(), QString(), false);
}

void Repo::runGit(const QString &label, const QStringList &args, const QByteArray &input)
{
    if (m_opRunning || m_path.isEmpty()) return;
    m_opCanForce = false;
    setOp(true, label, QString(), false);
    auto *p = new QProcess(this);
    p->setWorkingDirectory(m_path);
    p->setProcessEnvironment(gitEnv());
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::readyRead, this, [this, p] {
        m_opOutput += QString::fromUtf8(p->readAll());
        emit opChanged();
    });
    connect(p, &QProcess::finished, this, [this, p, label, args](int code, QProcess::ExitStatus st) {
        m_opOutput += QString::fromUtf8(p->readAll());
        p->deleteLater();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (!ok && args.value(0) == QLatin1String("push") && !args.contains(QLatin1String("--force-with-lease"))
            && (m_opOutput.contains(QLatin1String("non-fast-forward")) || m_opOutput.contains(QLatin1String("[rejected]"))))
            m_opCanForce = true;
        setOp(false, label, m_opOutput.trimmed(), !ok);
        emit opFinished(ok);
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

void Repo::pull()
{
    runGit(QStringLiteral("Pull (rebase)"), {QStringLiteral("pull"), QStringLiteral("--rebase"), QStringLiteral("--autostash")});
}

void Repo::fetch()
{
    runGit(QStringLiteral("Fetch"), {QStringLiteral("fetch"), QStringLiteral("--all"), QStringLiteral("--prune")});
}

void Repo::push(bool force)
{
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

int Repo::rewordState(const QString &id)
{
    if (m_path.isEmpty() || id == QLatin1String("WIP") || m_branch.isEmpty()) return 0;
    if (gitSync(m_path, {QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), id, QStringLiteral("HEAD")}, nullptr) != 0) return 0;
    QString out;
    gitSync(m_path, {QStringLiteral("branch"), QStringLiteral("-r"), QStringLiteral("--contains"), id}, &out);
    return out.trimmed().isEmpty() ? 1 : 2;
}

void Repo::reword(const QString &id, const QString &message)
{
    if (m_opRunning || m_path.isEmpty()) return;
    const QString root = m_path;
    QString msg = message.trimmed();
    if (msg.isEmpty()) return;
    msg += QLatin1Char('\n');
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
        const QString newId = out.section(QLatin1Char('\n'), -1).trimmed();

        // 3. move the branch (HEAD) or replay the commits above it
        if (isHead) {
            if (gitSync(root, {QStringLiteral("update-ref"), QStringLiteral("-m"), QStringLiteral("towel: reword"), QStringLiteral("HEAD"), newId, id}, &out) != 0)
                return {false, out};
            return {true, QStringLiteral("Message updated.")};
        }
        if (gitSync(root, {QStringLiteral("rebase"), QStringLiteral("--rebase-merges"), QStringLiteral("--autostash"), QStringLiteral("--onto"), newId, id}, &out) != 0) {
            QString ignored;
            gitSync(root, {QStringLiteral("rebase"), QStringLiteral("--abort")}, &ignored);
            return {false, QStringLiteral("Could not replay the later commits, nothing was changed.\n") + out};
        }
        return {true, QStringLiteral("Message updated; later commits replayed.")};
    }).then(this, [this](QPair<bool, QString> res) {
        setOp(false, QStringLiteral("Edit commit message"), res.second, !res.first);
        emit opFinished(res.first);
        refresh();
    });
}

// ---------- commit panel ----------

void Repo::loadStatus()
{
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
QByteArray pathspecs(const QStringList &paths)
{
    QByteArray out;
    for (const QString &p : paths) {
        out += ":(literal)" + p.toUtf8();
        out += '\0';
    }
    return out;
}
const QStringList kFromStdin{QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul")};
} // namespace

void Repo::indexOp(const QStringList &args, const QByteArray &input)
{
    m_indexQueue.append({args, input});
    pumpIndexOps();
}

// Index operations run one at a time (git takes index.lock), then refresh only the status.
void Repo::pumpIndexOps()
{
    if (m_indexBusy || m_indexQueue.isEmpty() || m_path.isEmpty()) return;
    const auto [args, input] = m_indexQueue.takeFirst();
    m_indexBusy = true;
    auto *p = new QProcess(this);
    p->setWorkingDirectory(m_path);
    p->setProcessEnvironment(gitEnv());
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(p->readAll()).trimmed();
        p->deleteLater();
        m_indexBusy = false;
        if (st != QProcess::NormalExit || code != 0) {
            m_indexQueue.clear();
            emit indexOpFailed(out.isEmpty() ? QStringLiteral("git failed") : out);
        }
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

void Repo::stage(const QStringList &paths)
{
    if (paths.isEmpty()) return;
    indexOp(QStringList{QStringLiteral("add"), QStringLiteral("-A")} + kFromStdin, pathspecs(paths));
}

void Repo::unstage(const QStringList &paths)
{
    if (paths.isEmpty()) return;
    if (m_hasHead) indexOp(QStringList{QStringLiteral("restore"), QStringLiteral("--staged")} + kFromStdin, pathspecs(paths));
    else indexOp(QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q")} + kFromStdin, pathspecs(paths));
}

void Repo::stageAll()
{
    indexOp({QStringLiteral("add"), QStringLiteral("-A")}, {});
}

void Repo::unstageAll()
{
    if (m_hasHead) indexOp({QStringLiteral("reset"), QStringLiteral("-q")}, {});
    else indexOp({QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q"), QStringLiteral(".")}, {});
}

void Repo::commit(const QString &message, bool amend)
{
    QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend) args << QStringLiteral("--amend");
    QByteArray msg = message.trimmed().toUtf8();
    msg += '\n';
    auto *once = new QObject(this);
    connect(this, &Repo::opFinished, once, [this, once](bool ok) {
        once->deleteLater();
        if (ok) emit committed();
    });
    runGit(amend ? QStringLiteral("Amend") : QStringLiteral("Commit"), args, msg);
}

QString Repo::headId() const
{
    GRepo r;
    if (!openRepo(r, m_path)) return {};
    GRef head;
    if (git_repository_head(head.out(), r) != 0 || !git_reference_target(head)) return {};
    return oidStr(git_reference_target(head));
}

QString Repo::headMessage() const
{
    GRepo r;
    if (!openRepo(r, m_path)) return {};
    GObj c;
    if (git_revparse_single(c.out(), r, "HEAD^{commit}") != 0) return {};
    return u8(git_commit_message(reinterpret_cast<git_commit *>(c.get()))).trimmed();
}
