#pragma once
#include "data.h"
#include <QColor>
#include <vector>

// A file diff prepared for painting on a monospace grid: per line, syntax
// color spans and word-level change marks (both in character columns).
struct ColorSpan {
    int off, len;
    QRgb color; // 0 = default text color
    bool bold, italic;
};

using Ranges = std::vector<std::pair<int, int>>;

struct DocLine {
    char t; // '+', '-', ' '
    int o, n;
    QString text;
    std::vector<ColorSpan> spans;
    Ranges marks = {};
};

struct DocHunk {
    QString header;
    std::vector<DocLine> lines;
};

struct DiffDoc {
    DiffData meta = {}; // hunks moved out into `hunks` below
    std::vector<DocHunk> hunks;
    int additions = 0, deletions = 0, maxCols = 0;
};

DiffDoc buildDiffDoc(DiffData &&d);
