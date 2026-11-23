#pragma once
#include <QAbstractScrollArea>
#include <QVariantList>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// The plan: rows newest first, each {id, summary, message, original, author,
// email, action: pick|squash|fixup|drop}. Drag or Alt+↑/↓ to reorder.
class RebaseList : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit RebaseList(QWidget *parent = nullptr);
    void setItems(const QVariantList &items);
    QVariantList &items() { return m_items; }
    int current() const { return m_cur; }
    void setCurrent(int i);
    void setAction(const QString &action); // on the current row
    void move(int delta);
    void changed(); // after editing items from outside

signals:
    void currentChanged(int row);
    void planChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool event(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void scrollContentsBy(int, int) override { viewport()->update(); }

private:
    int rowAt(int y) const;
    QRect pillRect(int row) const;
    void relayout();
    void ensureVisible(int row);

    QVariantList m_items;
    int m_cur = 0, m_drag = -1, m_dragY = 0, m_pressY = 0;
    bool m_dragging = false;
};

class RebaseView : public QWidget {
    Q_OBJECT
public:
    explicit RebaseView(QWidget *parent = nullptr);
    void start(const QString &base, const QString &branch, const QVariantList &items);

signals:
    void cancel();
    void run(const QString &base, const QVariantList &items);

private:
    void onCurrent(int row);
    void updateSummary();

    RebaseList *m_list;
    QLabel *m_title, *m_summary, *m_msgHint;
    QPlainTextEdit *m_msg;
    QPushButton *m_start;
    QString m_base;
    bool m_loadingMsg = false;
};
