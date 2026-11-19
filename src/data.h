#pragma once
#include <QHash>
#include <QImage>
#include <QList>
#include <QStringList>
#include <QString>
#include <QVariant>
#include <array>
#include <cstdint>
#include <vector>

// One line segment of the commit graph inside a single row.
//  k = 0: lane passes straight through (top -> bottom at lane a)
//  k = 1: lane a (top) flows into this row's node at lane b
//  k = 2: node (lane a) flows down to lane b (bottom)
struct Edge {
    quint16 a, b;
    quint8 k, c; // kind, color index
};

struct RefLabel {
    QString name;
    QString kind; // head | local | remote | tag
    bool current = false;
};

using RawOid = std::array<uint8_t, 20>;

// 48 bytes per commit. Commit text is NOT stored: the model reads titles from
// git for the rows on screen. Authors are indices into a shared table.
struct CommitRow {
    RawOid oid{};
    qint64 time = 0;
    uint32_t edgeStart = 0;
    uint32_t author = 0;
    uint16_t edgeCount = 0;
    uint16_t col = 0;
    uint8_t color = 0;
    bool merge = false;
    bool wip = false;
    bool hasRefs = false;

    QString id() const
    {
        if (wip) return QStringLiteral("WIP");
        static const char hex[] = "0123456789abcdef";
        QString s(40, Qt::Uninitialized);
        for (int i = 0; i < 20; ++i) {
            s[2 * i] = QLatin1Char(hex[oid[i] >> 4]);
            s[2 * i + 1] = QLatin1Char(hex[oid[i] & 15]);
        }
        return s;
    }
};

struct LoadResult {
    QString error;
    QString root, name, head;
    bool detached = false;
    std::vector<CommitRow> rows;
    std::vector<Edge> edges;
    QStringList authorNames, authorEmails;
    QHash<int, QList<RefLabel>> refs; // row -> labels (sparse)
    int wipCount = 0;
    int lanes = 1;
    QVariantList branches, tags, stashes;
    QString fingerprint;
    QString currentBranch, upstream;
    int ahead = 0, behind = 0;
};

struct DLine {
    char t; // '+', '-', ' '
    int o, n;
    QString s;
};

struct Hunk {
    QString header;
    std::vector<DLine> lines;
};

struct DiffData {
    QString error;
    QString id, path, oldPath, status;
    bool binary = false, image = false, truncated = false;
    QImage oldImage, newImage;
    std::vector<Hunk> hunks;
};
