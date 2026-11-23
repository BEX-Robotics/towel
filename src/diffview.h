#pragma once
#include "diffdoc.h"
#include <QAbstractScrollArea>
#include <QSet>
#include <QWidget>

class QLabel;
class QToolButton;

// Paints a DiffDoc on a monospace grid: unified or split, wrapped or not.
class DiffArea : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit DiffArea(QWidget *parent = nullptr);
    void setDoc(const DiffDoc *doc, bool loading);
    void setSplit(bool s);
    void setWrap(bool w);
    bool split() const { return m_split; }
    bool wrap() const { return m_wrap; }

    // 0 = read-only, 1 = unstaged (hunks can be staged), 2 = staged (unstaged)
    void setStaging(int mode);

    // the next setDoc calls are a reload of the same file: keep scroll and hunk
    void keepPlace() { m_keepPlace = true; }
    void selectRows(int from, int to); // dev aid / tests: like a gutter drag

signals:
    // changed lines of one hunk to stage/unstage: old line numbers of '-', new of '+'
    void applyHunk(const QSet<int> &del, const QSet<int> &add);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool event(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void scrollContentsBy(int, int) override { viewport()->update(); }

private:
    struct Row {
        int kind;                   // 0 hunk, 1 context, 2 change (split pair / unified add or del)
        const DocLine *a = nullptr; // unified: the line; split: left (old)
        const DocLine *b = nullptr; // split: right (new)
        const QString *header = nullptr;
    };

    // a stageable hunk: contiguous changes, split where the context gap is > 6 lines
    struct Unit {
        int first, last; // row range of the changed lines
        QSet<int> del, add;
    };
    void rebuildRows();
    void buildUnits();
    int unitAtRow(int row) const;
    int rowAtY(int y) const;
    QRect buttonRect(int unit) const;
    void setCurrent(int unit, bool scroll);
    QRect chipRect() const; // "Stage N lines" for the line selection
    void emitSelection();
    void selectTo(int row);
    void applyCurrent();
    void relayout();
    int visualLines(const DocLine *l, int cols) const;
    static std::vector<int> breaks(const QString &text, int cols); // start column of each visual line
    void paintText(QPainter &p, const DocLine &l, int from, int to, qreal x, qreal top, const QColor &markBg) const;

    const DiffDoc *m_doc = nullptr;
    bool m_loading = false;
    bool m_split = false, m_wrap = false;
    std::vector<Row> m_rows;
    std::vector<int> m_y; // top of each row, in pixels; m_y.back() = total height
    QFont m_font, m_bold, m_italic;
    qreal m_cw = 8;
    int m_lh = 18;
    int m_cols = 80; // wrap columns (unified) or per side (split)
    std::vector<Unit> m_units;
    int m_staging = 0, m_current = -1, m_hoverUnit = -1;
    bool m_keepPlace = false;

    // line selection (unified view): rows of changed lines
    QSet<int> m_sel;
    int m_selAnchor = -1;
    bool m_dragging = false, m_dragAdd = true, m_hoverChip = false;
    QSet<int> m_selBase; // selection before the current drag
};

class DiffView : public QWidget {
    Q_OBJECT
public:
    explicit DiffView(QWidget *parent = nullptr);
    void setDoc(const DiffDoc *doc, bool loading);
    void setOptions(bool full, bool split, bool wrap);
    bool full() const { return m_full; }
    void setModeLabel(const QString &mode); // "Staged" / "Unstaged" / ""
    DiffArea *area() const { return m_area; }

signals:
    void closeRequested();
    void stepFile(int delta);
    void optionsChanged(bool full, bool split, bool wrap);

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void fitToolbar();
    DiffArea *m_area;
    QLabel *m_status, *m_path, *m_stats, *m_mode;
    QToolButton *m_hunks, *m_fullBtn, *m_unified, *m_splitBtn, *m_wrapBtn, *m_viewBtn;
    bool m_full = false;
};
