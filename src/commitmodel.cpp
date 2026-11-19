#include "commitmodel.h"
#include "gitload.h"
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cstring>

namespace {

QList<RefGroup> groupRefs(const QList<RefLabel> &refs)
{
    QList<RefGroup> groups;
    auto find = [&](const QString &name) -> int {
        for (int i = 0; i < groups.size(); ++i)
            if (!groups[i].tag && groups[i].name == name) return i;
        return -1;
    };
    for (const RefLabel &r : refs) {
        if (r.kind == QLatin1String("tag")) {
            RefGroup g;
            g.name = r.name;
            g.tag = true;
            groups.append(g);
            continue;
        }
        QString base = r.name;
        if (r.kind == QLatin1String("remote")) {
            const int slash = base.indexOf(QLatin1Char('/'));
            const QString shortName = slash >= 0 ? base.mid(slash + 1) : base;
            for (const RefLabel &o : refs)
                if (o.kind == QLatin1String("local") && o.name == shortName) base = shortName;
        }
        int gi = find(base);
        if (gi < 0) {
            RefGroup g;
            g.name = base;
            groups.append(g);
            gi = int(groups.size()) - 1;
        }
        RefGroup &g = groups[gi];
        if (r.kind == QLatin1String("local")) g.local = true;
        if (r.kind == QLatin1String("remote")) g.remote = true;
        if (r.kind == QLatin1String("head")) g.head = true;
        if (r.current) g.current = true;
    }
    return groups;
}

} // namespace

void CommitModel::setData(const QString &root, LoadResult &res)
{
    m_root = root;
    m_rows = std::move(res.rows);
    m_edges = std::move(res.edges);
    m_names = std::move(res.authorNames);
    m_emails = std::move(res.authorEmails);
    m_refs.clear();
    for (auto it = res.refs.cbegin(); it != res.refs.cend(); ++it) m_refs.insert(it.key(), groupRefs(it.value()));
    res.refs.clear();
    m_wipCount = res.wipCount;
    m_lanes = res.lanes;
    m_sorted.clear();
    m_sorted.reserve(m_rows.size());
    for (int i = 0; i < int(m_rows.size()); ++i)
        if (!m_rows[i].wip) m_sorted.emplace_back(m_rows[i].oid, i);
    std::sort(m_sorted.begin(), m_sorted.end());
    m_text.clear();
    openRepo(m_repo, root);
    m_hits.clear();
    m_hitList.clear();
    m_searching = false;
    ++m_searchGen;
    emit reset();
}

const CommitModel::Text &CommitModel::text(int row) const
{
    if (row < 0 || row >= int(m_rows.size())) return m_empty;
    if (const Text *t = m_text.object(row)) return *t;
    const CommitRow &r = m_rows[row];
    auto *t = new Text;
    if (r.wip) {
        t->summary = QStringLiteral("// WIP");
    } else if (m_repo) {
        git_oid oid;
        std::memcpy(oid.id, r.oid.data(), 20);
        GCommit c;
        if (git_commit_lookup(c.out(), m_repo, &oid) == 0) {
            t->summary = u8(git_commit_summary(c));
            t->body = gitload::firstBodyLine(git_commit_body(c));
        }
    }
    m_text.insert(row, t);
    return *m_text.object(row);
}

const QList<RefGroup> &CommitModel::refs(int i) const
{
    static const QList<RefGroup> none;
    auto it = m_refs.constFind(i);
    return it == m_refs.constEnd() ? none : *it;
}

int CommitModel::indexOf(const QString &id) const
{
    if (id == QLatin1String("WIP")) return (!m_rows.empty() && m_rows[0].wip) ? 0 : -1;
    const QByteArray bytes = QByteArray::fromHex(id.toLatin1());
    if (bytes.size() != 20) return -1;
    RawOid o;
    std::memcpy(o.data(), bytes.constData(), 20);
    auto it = std::lower_bound(m_sorted.begin(), m_sorted.end(), o, [](const auto &e, const RawOid &k) { return e.first < k; });
    return (it != m_sorted.end() && it->first == o) ? it->second : -1;
}

void CommitModel::search(const QString &query)
{
    const QString q = query.trimmed();
    const quint64 gen = ++m_searchGen;
    if (q.isEmpty()) {
        m_searching = false;
        m_hits.clear();
        m_hitList.clear();
        emit searchFinished();
        return;
    }
    std::vector<RawOid> oids;
    std::vector<uint32_t> authors;
    oids.reserve(m_rows.size());
    authors.reserve(m_rows.size());
    for (const auto &r : m_rows) {
        oids.push_back(r.oid);
        authors.push_back(r.wip ? UINT32_MAX : r.author);
    }
    std::vector<char> authorHit(m_names.size(), 0);
    for (int i = 0; i < m_names.size(); ++i)
        authorHit[i] = m_names[i].contains(q, Qt::CaseInsensitive) || m_emails[i].contains(q, Qt::CaseInsensitive);
    std::vector<char> refHit(m_rows.size(), 0);
    for (auto it = m_refs.cbegin(); it != m_refs.cend(); ++it)
        for (const auto &ref : it.value())
            if (ref.name.contains(q, Qt::CaseInsensitive)) refHit[it.key()] = 1;

    const QString root = m_root;
    QtConcurrent::run([q, root, oids = std::move(oids), authors = std::move(authors), authorHit = std::move(authorHit),
                       refHit = std::move(refHit)]() {
        std::vector<char> hits(oids.size(), 0);
        const QByteArray needle = q.toUtf8().toLower();
        const bool hexish = needle.size() >= 4 && QByteArray::fromHex(needle).toHex() == needle;
        GRepo r;
        const bool ok = openRepo(r, root);
        for (size_t i = 0; i < oids.size(); ++i) {
            if (authors[i] == UINT32_MAX) continue;
            if (refHit[i] || authorHit[authors[i]]) {
                hits[i] = 1;
                continue;
            }
            if (hexish && QByteArray(reinterpret_cast<const char *>(oids[i].data()), 20).toHex().startsWith(needle)) {
                hits[i] = 1;
                continue;
            }
            if (!ok) continue;
            git_oid oid;
            std::memcpy(oid.id, oids[i].data(), 20);
            GCommit c;
            if (git_commit_lookup(c.out(), r, &oid) != 0) continue;
            const char *msg = git_commit_message(c);
            if (msg && QByteArray(msg).toLower().contains(needle)) hits[i] = 1;
        }
        return hits;
    }).then(this, [this, gen](std::vector<char> hits) {
        if (gen != m_searchGen) return;
        m_searching = true;
        m_hits = std::move(hits);
        m_hitList.clear();
        for (int i = 0; i < int(m_hits.size()); ++i)
            if (m_hits[i]) m_hitList.append(i);
        emit searchFinished();
    });
}
