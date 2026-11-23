#pragma once
#include <QAbstractScrollArea>
#include <QScrollArea>
#include <QSet>
#include <QVariantMap>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QHBoxLayout;

// Round avatar (details panel).
class AvatarWidget : public QWidget {
public:
    explicit AvatarWidget(int size, QWidget *parent = nullptr);
    void set(const QString &email, const QString &name, const QColor &ring);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_email, m_name;
    QColor m_ring;
};

// Branches / remotes / tags / stashes, painted list with collapsible sections.
class SideList : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit SideList(QWidget *parent = nullptr);
    void setData(const QVariantList &branches, const QVariantList &tags, const QVariantList &stashes);
    void setFilter(const QString &f);

signals:
    void jumpTo(const QString &id);

    // entry: {kind: local|remote|tag|stash, name, id, index, current}
    void menuRequested(const QVariantMap &entry, const QPoint &globalPos);
    void activated(const QVariantMap &entry); // double-click

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void scrollContentsBy(int, int) override { viewport()->update(); }

private:
    struct Entry {
        bool header = false;
        QString key, label, name, id, kind;
        int count = 0, ahead = 0, behind = 0, index = 0;
        bool current = false;
        QVariantMap map() const;
    };
    void rebuild();
    int rowAt(int y) const;

    QVariantList m_branches, m_tags, m_stashes;
    QString m_filter;
    QSet<QString> m_collapsed{QStringLiteral("tag")};
    std::vector<Entry> m_entries;
    int m_hover = -1;
};

class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget *parent = nullptr);
    SideList *list() const { return m_list; }

private:
    QLineEdit *m_filter;
    SideList *m_list;
};

// Changed files of a commit, painted.
class FileList : public QWidget {
    Q_OBJECT
public:
    explicit FileList(QWidget *parent = nullptr);
    void setFiles(const QVariantList &files);
    void setOpenPath(const QString &p);

    // "+" or "−": shows a per-row stage/unstage button on hover
    void setAction(const QString &glyph, const QString &tip);
    const QVariantList &files() const { return m_files; }
    QSize sizeHint() const override;

signals:
    void fileClicked(const QVariantMap &file);
    void actionClicked(const QVariantMap &file);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    bool event(QEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRect actionRect(int row) const;
    QVariantList m_files;
    QString m_open, m_action, m_actionTip;
    int m_hover = -1;
};

class DetailsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit DetailsPanel(QWidget *parent = nullptr);
    void setDetails(const QVariantMap &d, int laneColor, int rewordState);
    void setOpenPath(const QString &p) { m_files->setOpenPath(p); }

signals:
    void openFile(const QVariantMap &file);
    void jumpTo(const QString &id);
    void editMessage();

protected:
    bool eventFilter(QObject *, QEvent *) override;

private:
    void showBody();
    QString m_fullBody;
    bool m_bodyOpen = false;
    QLabel *m_more;
    QLabel *m_placeholder, *m_summary, *m_body, *m_author, *m_when, *m_committer, *m_filesHead, *m_stats;
    AvatarWidget *m_avatar;
    QPushButton *m_sha, *m_edit;
    QWidget *m_authorRow, *m_parentsRow, *m_content;
    QHBoxLayout *m_parentsLayout;
    FileList *m_files;
    QString m_id;
};
