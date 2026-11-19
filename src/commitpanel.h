#pragma once
#include <QVariantMap>
#include <QWidget>

class FileList;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;

// Right panel when the WIP row is selected: unstaged / staged files and the
// commit message editor. Emits intents; Repo does the git work.
class CommitPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CommitPanel(QWidget *parent = nullptr);

    void setStatus(const QVariantMap &status);
    void setBranch(const QString &branch);
    void setRepoKey(const QString &repoPath); // loads that repo's message draft
    void setOpenFile(const QString &mode, const QString &path);
    void setAmendInfo(const QString &headMessage, bool headPushed);
    void clearMessage();
    void triggerCommit();
    const QVariantList &files(const QString &mode) const;

signals:
    void openFile(const QVariantMap &file, const QString &mode); // mode: "unstaged" | "staged"
    void stage(const QStringList &paths);
    void unstage(const QStringList &paths);
    void stageAll();
    void unstageAll();
    void commit(const QString &message, bool amend);
    void amendToggled(bool on);

private:
    void updateButton();
    void saveDraft();

    FileList *m_unstaged, *m_staged;
    QLabel *m_unstagedHead, *m_stagedHead, *m_conflicts, *m_counter, *m_amendWarn;
    QPushButton *m_stageAll, *m_unstageAll, *m_commit;
    QLineEdit *m_summary;
    QPlainTextEdit *m_body;
    QCheckBox *m_amend;
    QTimer *m_draftTimer;
    QString m_branch, m_draftKey;
    QVariantList m_unstagedFiles, m_stagedFiles;
};
