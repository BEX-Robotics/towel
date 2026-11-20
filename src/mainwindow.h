#pragma once
#include <QMainWindow>
#include <QVariantMap>

class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QSplitter;
class QStackedWidget;
class CommitView;
class DiffView;
class DetailsPanel;
class Sidebar;
class Repo;
class CommitPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void runDemo(); // dev aid: TOWEL_DEMO_* env vars

protected:
    void closeEvent(QCloseEvent *) override;
    bool event(QEvent *) override;

private:
    QWidget *buildTopBar();
    QWidget *buildStatusStrip();
    void onLoaded();
    void onSelected(int row);
    void showFile(const QVariantMap &f, const QString &mode = QString());
    void onStatus();
    void closeDiff();
    void stepFile(int delta);
    void openDialog();
    void searchNext(bool back);
    void updateOp();
    void editMessage();
    void updateBranchChip();
    void updateUndo();
    void commitMenu(int row, const QPoint &pos);
    void refMenu(const QVariantMap &entry, const QPoint &pos);
    void checkout(const QVariantMap &entry);
    void createBranch(const QString &at);
    void deleteBranch(const QString &name);
    void stashChanges();
    // run a git command; on failure whose output matches `retryIf`, ask and run `retry`
    void gitThen(const QString &label, const QStringList &args, const QString &retryIf = {}, const QString &question = {},
                 const QStringList &retry = {}, const QString &undoLabel = {}, const QString &undoMode = QStringLiteral("--keep"));

    Repo *m_repo;
    CommitView *m_commits;
    DiffView *m_diff;
    DetailsPanel *m_details;
    CommitPanel *m_commitPanel;
    QStackedWidget *m_right;
    Sidebar *m_sidebar;
    QSplitter *m_split;
    QStackedWidget *m_center;
    QWidget *m_empty;
    QLabel *m_emptyMsg;

    QPushButton *m_repoBtn, *m_fetch, *m_pull, *m_push, *m_undoBtn;
    QLabel *m_branch, *m_hitLabel;
    QLineEdit *m_search;

    QWidget *m_strip;
    QLabel *m_opIcon, *m_opText, *m_opLast;
    QPushButton *m_opForce, *m_opToggle, *m_opClose;
    QPlainTextEdit *m_opLog;

    QString m_selectedId;
    QVariantMap m_openFile;
    QString m_openMode; // "" (commit file), "unstaged" or "staged"
    bool m_jumpOnHits = false;
    int m_hitPos = -1;
    bool m_full = false, m_splitDiff = false, m_wrap = false;
};
