#pragma once
#include "theme.h"
#include <QAbstractScrollArea>

class CommitModel;
struct CommitRow;
struct RefGroup;

// The history: graph | refs + message | author | date, painted directly from
// the compact rows. Only visible rows are touched.
class CommitView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit CommitView(CommitModel *model, QWidget *parent = nullptr);

    int current() const { return m_current; }
    void setCurrent(int row, bool ensureVisible = true);

signals:
    void currentChanged(int row);
    void menuRequested(int row, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool viewportEvent(QEvent *) override;
    void scrollContentsBy(int, int) override;

private:
    void relayout();
    int rowAt(int y) const;
    int graphWidth() const;
    int m_graphWidthCache = 60;
    int m_rowHAtZoom = rowHeight(); // row height before the last zoom, to keep the top row in place
    int refsWidth() const;          // left column with branch/tag chips; 0 when narrow (chips go inline)
    int authorWidth() const;
    int dateWidth() const;
    void paintRow(QPainter &p, int row, int y);
    void paintGraph(QPainter &p, int row, const QRectF &cell);
    void paintNode(QPainter &p, const CommitRow *r, qreal x, qreal mid);
    qreal paintChip(QPainter &p, const RefGroup &g, const QColor &lane, qreal x, qreal cy, qreal maxW);

    CommitModel *m_model;
    int m_current = -1;
    int m_hover = -1;
    // row = one line of message text plus padding that grows slower than the text
    static int rowHeight() { return theme::lineHeight(13) + theme::zp(8); }
    static int nodeSize() { return std::min(theme::z(20), rowHeight() - 4); }
    static int laneWidth() { return nodeSize() - theme::zp(2); }
};
