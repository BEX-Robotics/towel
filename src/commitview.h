#pragma once
#include <QAbstractScrollArea>

class CommitModel;
struct RefGroup;

// The history: graph | refs + message | author | date, painted directly from
// the compact rows. Only visible rows are touched.
class CommitView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit CommitView(CommitModel *model, QWidget *parent = nullptr);

    int current() const { return m_current; }
    void setCurrent(int row, bool ensureVisible = true);

signals:
    void currentChanged(int row);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool viewportEvent(QEvent *) override;
    void scrollContentsBy(int, int) override;

private:
    void relayout();
    int rowAt(int y) const;
    int graphWidth() const;
    int authorWidth() const;
    int dateWidth() const;
    void paintRow(QPainter &p, int row, int y);
    void paintGraph(QPainter &p, int row, const QRectF &cell);
    qreal paintChip(QPainter &p, const RefGroup &g, const QColor &lane, qreal x, qreal cy, qreal maxW);

    CommitModel *m_model;
    int m_current = -1;
    int m_hover = -1;
    static constexpr int kRowH = 32;
    static constexpr int kLaneW = 22;
    static constexpr int kNode = 22;
};
