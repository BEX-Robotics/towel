#pragma once
#include <QFileSystemWatcher>
#include <QTimer>
#include <QProcessEnvironment>
#include <QSet>
#include <functional>
#include <optional>
#include "commitmodel.h"
#include "diffdoc.h"
#include <QObject>
#include <QVariantMap>

// The open repository: history model, details, diffs and git operations.
class Repo : public QObject {
    Q_OBJECT

public:
    static Repo *instance();
    static QString currentPath();

    QString path() const { return m_path; }
    QString name() const { return m_name; }
    QString head() const { return m_head; }
    bool detached() const { return m_detached; }
    QString upstream() const { return m_upstream; }
    int ahead() const { return m_ahead; }
    int behind() const { return m_behind; }
    QVariantList branches() const { return m_branches; }
    QVariantList tags() const { return m_tags; }
    QVariantList stashes() const { return m_stashes; }
    int lanes() const { return m_lanes; }
    bool isOpen() const { return !m_path.isEmpty(); }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    CommitModel *commits() { return &m_commits; }
    const DiffDoc &diff() const { return m_diff; }
    QVariantMap details() const { return m_details; }
    bool diffLoading() const { return m_diffLoading; }
    bool opRunning() const { return m_opRunning; }
    QString opLabel() const { return m_opLabel; }
    QString opOutput() const { return m_opOutput; }
    bool opFailed() const { return m_opFailed; }
    bool opCanForce() const { return m_opCanForce; }

    void open(const QString &pathOrURL);
    void refresh();
    void checkChanged();
    void watchGitDir(); // inotify on .git so commits/pushes from elsewhere show up while in the background
    void loadDetails(const QString &id);
    void loadDiff(const QString &id, const QString &path, const QString &oldPath, bool full);
    void clearDiff();
    void copy(const QString &text);
    QString dateString(qint64 ms) const;

    void pull();
    void push(bool force = false);
    void fetch();
    void dismissOp();

    // any other git command, shown in the status strip; refreshes afterwards
    // undoLabel: record in the undo journal (undo resets back with --keep)
    // undoMode: how undo moves back: "--keep" (drops the commits' changes), "--soft", "--mixed"
    void run(const QString &label, const QStringList &args, const QString &undoLabel = {}, const QString &undoMode = QStringLiteral("--keep")) {
        if (!undoLabel.isEmpty()) journalBegin(undoLabel, undoMode);
        runGit(label, args);
    }
    QString sequencerState() const;

    // interactive rebase: commits from `from` (inclusive) up to HEAD, newest first:
    // {id, summary, message, author, email}. Empty + *err when not possible.
    QVariantList rebasePlanFrom(const QString &from, QString *err, QString *base);

    // items newest first: {id, action: pick|squash|fixup|drop, message (final, for pick rows)}
    void rebase(const QString &base, const QVariantList &items); // "cherry-pick", "revert", "merge", "rebase" or ""

    // 0 = not rewordable, 1 = rewordable (local only), 2 = rewordable but already pushed
    int rewordState(const QString &id);
    void reword(const QString &id, const QString &message);

    // commit panel
    QVariantMap status() const { return m_status; }
    void loadStatus();
    void stage(const QStringList &paths);
    void unstage(const QStringList &paths);
    void stageAll();
    void unstageAll();

    // stage (staged=false) or unstage (staged=true) only these changed lines of a file
    void applyLines(const QStringList &paths, bool staged, const QSet<int> &selDel, const QSet<int> &selAdd);
    void commit(const QString &message, bool amend);
    QString headID() const;
    QString branch() const { return m_branch; } // "" when detached
    QString headMessage() const;
    bool indexBusy() const { return m_indexBusy; }

    // undo journal: ref moves done through Towel (commit, amend, reword, pull)
    QString undoLabel() const; // "" if nothing to undo
    QString redoLabel() const;
    void undo();
    void redo();

signals:
    void loadedChanged();
    void loadingChanged();
    void errorChanged();
    void detailsChanged();
    void diffLoadingChanged();
    void diffChanged();
    void opChanged();
    void opFinished(bool ok);
    void statusChanged();
    void indexOpFailed(const QString &message);
    void committed();
    void undoChanged();
    void restoreMessage(const QString &message); // after undoing a commit

private:
    explicit Repo(QObject *parent = nullptr);
    void runGit(const QString &label, const QStringList &args, const QByteArray &input = {}, const QProcessEnvironment *env = nullptr);
    struct IndexJob {
        QStringList args;
        QByteArray input;

        // computes args + input on a worker right before running; sets err to fail
        std::function<void(QStringList &args, QByteArray &input, QString &err)> gen;
    };
    void indexOp(const QStringList &args, const QByteArray &input);
    void runIndexJob(const QStringList &args, const QByteArray &input);
    void failIndexOp(const QString &out);
    void pumpIndexOps();
    void setOp(bool running, const QString &label, const QString &out, bool failed);
    struct UndoEntry {
        QString label, ref, before, after, message;
        QString mode = QStringLiteral("--soft"); // how to reset back / forward
    };
    void journalBegin(const QString &label, const QString &mode);
    void journalEnd(bool ok);
    void moveRef(const UndoEntry &e, bool back);
    QPair<QString, QString> headRef() const; // ref name ("HEAD" when detached), oid

    void watchRefFolders();
    QFileSystemWatcher *m_watcher = 0;
    QString m_watchedGitDir, m_watchedCommonDir;
    bool m_rescanRefs = false;
    QTimer *m_watchDebounce = 0;
    CommitModel m_commits;
    DiffDoc m_diff = {};
    QString m_path, m_name, m_head, m_upstream, m_fingerprint, m_branch;
    bool m_detached = false;
    int m_ahead = 0, m_behind = 0, m_lanes = 1;
    QVariantList m_branches, m_tags, m_stashes;
    bool m_loading = false;
    QString m_error;
    QVariantMap m_details;
    bool m_diffLoading = false;
    quint64 m_loadGen = 0, m_detailsGen = 0, m_diffGen = 0;
    bool m_checking = false;
    QVariantMap m_status;
    bool m_hasHead = true;
    quint64 m_statusGen = 0;
    bool m_indexBusy = false;
    bool m_settling = false; // index op done, waiting for the status reload
    QList<IndexJob> m_indexQueue;

    std::optional<UndoEntry> m_pending;
    QList<UndoEntry> m_undo, m_redo;

    bool m_opRunning = false, m_opFailed = false, m_opCanForce = false;
    QString m_opLabel, m_opOutput;
};
