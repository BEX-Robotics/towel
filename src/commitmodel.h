#pragma once
#include "data.h"
#include "gitutil.h"
#include <QCache>
#include <QHash>
#include <QObject>

// Refs pointing at one commit, grouped the way GitKraken shows them:
// local "main" and "origin/main" become one pill with two icons.
struct RefGroup {
    QString name;
    bool local = false, remote = false, tag = false, current = false, head = false;
};

// History. Holds only the compact graph rows; commit titles are read from git
// on demand for rows being painted (with a small cache).
class CommitModel : public QObject
{
    Q_OBJECT
public:
    explicit CommitModel(QObject *parent = nullptr) : QObject(parent) {}

    void setData(const QString &root, LoadResult &res);

    int count() const { return int(m_rows.size()); }
    const CommitRow *row(int i) const { return (i >= 0 && i < int(m_rows.size())) ? &m_rows[i] : nullptr; }
    const Edge *edgesOf(const CommitRow *r) const { return m_edges.data() + r->edgeStart; }
    QString authorOf(const CommitRow *r) const { return r->wip ? QString() : m_names.value(int(r->author)); }
    QString emailOf(const CommitRow *r) const { return r->wip ? QString() : m_emails.value(int(r->author)); }
    QString summary(int i) const { return text(i).summary; }
    QString body(int i) const { return text(i).body; }
    const QList<RefGroup> &refs(int i) const;
    int wipCount() const { return m_wipCount; }
    int lanes() const { return m_lanes; }

    int indexOf(const QString &id) const;
    QString idAt(int i) const { return row(i) ? m_rows[i].id() : QString(); }

    // Background search over messages, authors, SHAs and refs.
    void search(const QString &q);
    bool searching() const { return m_searching; }
    bool isHit(int i) const { return !m_searching || (i < int(m_hits.size()) && m_hits[i]); }
    const QList<int> &hits() const { return m_hitList; }

signals:
    void reset();
    void searchFinished();

private:
    struct Text {
        QString summary, body;
    };
    const Text &text(int row) const;

    QString m_root;
    std::vector<CommitRow> m_rows;
    std::vector<Edge> m_edges;
    QStringList m_names, m_emails;
    QHash<int, QList<RefGroup>> m_refs;
    std::vector<std::pair<RawOid, int>> m_sorted;
    int m_wipCount = 0;
    int m_lanes = 1;

    mutable GRepo m_repo;
    mutable QCache<int, Text> m_text{4000};
    Text m_empty;

    std::vector<char> m_hits;
    QList<int> m_hitList;
    bool m_searching = false;
    quint64 m_searchGen = 0;
};
