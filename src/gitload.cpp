#include "gitload.h"
#include "gitutil.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QHash>
#include <QUrl>
#include <algorithm>
#include <cstring>

namespace {

constexpr int kPalette = 10;
constexpr int kMaxDiffLines = 40000;
constexpr int kMaxStatFiles = 3000;

// ---------- lane layout ----------
struct Lanes {
    struct Slot {
        bool used = false;
        git_oid oid{};
        int color = 0;
    };
    std::vector<Slot> s;
    int next = 0;
    int width = 1;

    int newColor()
    {
        int c = next;
        next = (next + 1) % kPalette;
        return c;
    }
    int freeSlot()
    {
        for (size_t i = 0; i < s.size(); ++i)
            if (!s[i].used) return int(i);
        s.push_back({});
        return int(s.size() - 1);
    }
    int find(const git_oid *o) const
    {
        for (size_t i = 0; i < s.size(); ++i)
            if (s[i].used && git_oid_equal(&s[i].oid, o)) return int(i);
        return -1;
    }

    void place(const git_oid *id, const std::vector<git_oid> &parents, CommitRow &row, std::vector<Edge> &pool)
    {
        row.edgeStart = uint32_t(pool.size());
        std::vector<int> matches;
        for (size_t i = 0; i < s.size(); ++i)
            if (s[i].used && git_oid_equal(&s[i].oid, id)) matches.push_back(int(i));

        int col, color;
        if (!matches.empty()) {
            col = matches.front();
            color = s[col].color;
        } else {
            col = freeSlot();
            color = newColor();
        }

        for (size_t i = 0; i < s.size(); ++i) {
            if (!s[i].used) continue;
            const bool m = git_oid_equal(&s[i].oid, id);
            pool.push_back({quint16(i), quint16(m ? col : int(i)), quint8(m ? 1 : 0), quint8(s[i].color)});
        }
        for (int m : matches) s[m].used = false;

        if (!parents.empty()) {
            if (col >= int(s.size())) s.resize(col + 1);
            s[col] = {true, parents[0], color};
            pool.push_back({quint16(col), quint16(col), 2, quint8(color)});
            for (size_t pi = 1; pi < parents.size(); ++pi) {
                int j = find(&parents[pi]);
                int c;
                if (j < 0) {
                    j = freeSlot();
                    c = newColor();
                    s[j] = {true, parents[pi], c};
                } else {
                    c = s[j].color;
                }
                pool.push_back({quint16(col), quint16(j), 2, quint8(c)});
            }
        }
        while (!s.empty() && !s.back().used) s.pop_back();

        row.edgeCount = uint16_t(pool.size() - row.edgeStart);
        row.col = uint16_t(col);
        row.color = uint8_t(color);
        width = std::max(width, col + 1);
        for (size_t k = row.edgeStart; k < pool.size(); ++k) width = std::max(width, int(std::max(pool[k].a, pool[k].b)) + 1);
    }
};

int dirtyCount(git_repository *r)
{
    if (git_repository_is_bare(r)) return 0;
    git_status_options o;
    git_status_options_init(&o, GIT_STATUS_OPTIONS_VERSION);
    o.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    o.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
    GStatus st;
    if (git_status_list_new(st.out(), r, &o) != 0) return 0;
    return int(git_status_list_entrycount(st));
}

QString fingerprintOf(git_repository *r)
{
    QCryptographicHash h(QCryptographicHash::Md5);
    GRef head;
    if (git_repository_head(head.out(), r) == 0) {
        h.addData(QByteArray(git_reference_name(head)));
        if (auto t = git_reference_target(head)) h.addData(oidStr(t).toLatin1());
    }
    GRefIter it;
    if (git_reference_iterator_new(it.out(), r) == 0) {
        git_reference *ref = nullptr;
        while (git_reference_next(&ref, it) == 0) {
            GRef g;
            g.p = ref;
            h.addData(QByteArray(git_reference_name(ref)));
            if (auto t = git_reference_target(ref)) h.addData(oidStr(t).toLatin1());
        }
    }
    if (!git_repository_is_bare(r)) {
        git_status_options o;
        git_status_options_init(&o, GIT_STATUS_OPTIONS_VERSION);
        o.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
        GStatus st;
        if (git_status_list_new(st.out(), r, &o) == 0) {
            const size_t n = git_status_list_entrycount(st);
            for (size_t i = 0; i < n; ++i) {
                const git_status_entry *e = git_status_byindex(st, i);
                const git_diff_delta *d = e->index_to_workdir ? e->index_to_workdir : e->head_to_index;
                if (d && d->new_file.path) h.addData(QByteArray(d->new_file.path));
                h.addData(QByteArray::number(int(e->status)));
                // modification time catches repeated edits of the same file
                if (e->index_to_workdir && e->index_to_workdir->new_file.path) {
                    QFileInfo fi(u8(git_repository_workdir(r)) + u8(e->index_to_workdir->new_file.path));
                    h.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
                }
            }
        }
    }
    return QString::fromLatin1(h.result().toHex());
}

int kindOrder(const QString &k)
{
    if (k == QLatin1String("head")) return 0;
    if (k == QLatin1String("local")) return 1;
    if (k == QLatin1String("remote")) return 2;
    return 3;
}

const char *statusLetter(git_delta_t s)
{
    switch (s) {
    case GIT_DELTA_ADDED:
    case GIT_DELTA_UNTRACKED: return "A";
    case GIT_DELTA_DELETED: return "D";
    case GIT_DELTA_RENAMED: return "R";
    case GIT_DELTA_COPIED: return "C";
    case GIT_DELTA_TYPECHANGE: return "T";
    default: return "M";
    }
}

int makeDiff(git_diff **out, git_repository *r, const QString &id, git_diff_options *o)
{
    int rc;
    if (id == QLatin1String("WIP:staged") || id == QLatin1String("WIP:unstaged")) {
        git_index *idx = nullptr;
        if (git_repository_index(&idx, r) != 0) return -1;
        if (id == QLatin1String("WIP:staged")) {
            GTree headTree;
            GRef head;
            if (git_repository_head(head.out(), r) == 0) {
                GObj t;
                if (git_reference_peel(t.out(), head, GIT_OBJECT_TREE) == 0) headTree.p = reinterpret_cast<git_tree *>(t.release());
            }
            rc = git_diff_tree_to_index(out, r, headTree, idx, o);
        } else {
            o->flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS | GIT_DIFF_SHOW_UNTRACKED_CONTENT;
            rc = git_diff_index_to_workdir(out, r, idx, o);
        }
        git_index_free(idx);
    } else if (id == QLatin1String("WIP")) {
        GTree headTree;
        GRef head;
        if (git_repository_head(head.out(), r) == 0) {
            GObj t;
            if (git_reference_peel(t.out(), head, GIT_OBJECT_TREE) == 0) headTree.p = reinterpret_cast<git_tree *>(t.release());
        }
        o->flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS | GIT_DIFF_SHOW_UNTRACKED_CONTENT;
        rc = git_diff_tree_to_workdir_with_index(out, r, headTree, o);
    } else {
        git_oid oid;
        if (git_oid_fromstr(&oid, id.toLatin1().constData()) != 0) return -1;
        GCommit c;
        if (git_commit_lookup(c.out(), r, &oid) != 0) return -1;
        GTree t, pt;
        if (git_commit_tree(t.out(), c) != 0) return -1;
        if (git_commit_parentcount(c) > 0) {
            GCommit p;
            if (git_commit_parent(p.out(), c, 0) == 0) git_commit_tree(pt.out(), p);
        }
        rc = git_diff_tree_to_tree(out, r, pt, t, o);
    }
    if (rc != 0) return rc;
    git_diff_find_options fo;
    git_diff_find_options_init(&fo, GIT_DIFF_FIND_OPTIONS_VERSION);
    fo.flags = GIT_DIFF_FIND_RENAMES | GIT_DIFF_FIND_FOR_UNTRACKED;
    git_diff_find_similar(*out, &fo);
    return 0;
}

bool isImagePath(const QString &p)
{
    static const QStringList exts = {"png", "jpg", "jpeg", "gif", "webp", "bmp", "ico", "svg", "avif", "tif", "tiff"};
    return exts.contains(QFileInfo(p).suffix().toLower());
}

// Join hard-wrapped lines into paragraphs; keep blank lines, lists, indented
// blocks and trailers (Co-Authored-By: ...) on their own lines.
QString reflow(const QString &text)
{
    static const QRegularExpression keep(QStringLiteral("^(\\s|[-*•>]|\\d+[.)]\\s|[A-Z][A-Za-z]*(-[A-Za-z]+)+:\\s)"));
    QStringList out;
    QString para;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) {
            if (!para.isEmpty()) out << para, para.clear();
            out << QString();
            continue;
        }
        if (keep.match(line).hasMatch() || para.isEmpty()) {
            if (!para.isEmpty()) out << para;
            para = line;
        } else {
            para += QLatin1Char(' ') + line.trimmed();
        }
    }
    if (!para.isEmpty()) out << para;
    return out.join(QLatin1Char('\n')).trimmed();
}

} // namespace
namespace gitload {
QString firstBodyLine(const char *body)
{
    if (!body) return {};
    const QString b = QString::fromUtf8(body);
    for (const QString &l : b.split(QLatin1Char('\n'))) {
        const QString t = l.trimmed();
        if (!t.isEmpty()) return t.left(160);
    }
    return {};
}
} // namespace gitload
namespace {

} // namespace

namespace gitload {

QString fingerprint(const QString &root)
{
    GRepo r;
    if (!openRepo(r, root)) return {};
    return fingerprintOf(r);
}

LoadResult loadRepo(const QString &start)
{
    LoadResult res;
    GRepo repo;
    if (git_repository_open_ext(repo.out(), start.toUtf8().constData(), 0, nullptr) != 0) {
        res.error = QStringLiteral("Not a git repository: %1").arg(start);
        return res;
    }
    const char *wd = git_repository_workdir(repo);
    res.root = u8(wd ? wd : git_repository_path(repo));
    if (res.root.size() > 1 && res.root.endsWith(QLatin1Char('/'))) res.root.chop(1);
    res.name = QFileInfo(res.root).fileName();

    // HEAD
    GRef head;
    git_oid headOid{};
    bool headValid = false;
    QString headBranch;
    if (git_repository_head(head.out(), repo) == 0) {
        if (auto t = git_reference_target(head)) {
            headOid = *t;
            headValid = true;
        }
        if (git_reference_is_branch(head)) headBranch = u8(git_reference_shorthand(head));
        res.head = u8(git_reference_shorthand(head));
    } else {
        res.head = QStringLiteral("(no commits)");
    }
    res.detached = git_repository_head_detached(repo) == 1;
    if (res.detached && headValid) res.head = oidStr(&headOid).left(8);
    res.currentBranch = headBranch;

    // ref labels for the graph
    QHash<QString, QList<RefLabel>> labels;
    {
        GRefIter it;
        if (git_reference_iterator_new(it.out(), repo) == 0) {
            git_reference *r = nullptr;
            while (git_reference_next(&r, it) == 0) {
                GRef ref;
                ref.p = r;
                QString kind;
                if (git_reference_is_branch(r)) kind = QStringLiteral("local");
                else if (git_reference_is_remote(r)) kind = QStringLiteral("remote");
                else if (git_reference_is_tag(r)) kind = QStringLiteral("tag");
                else continue;
                const QString name = u8(git_reference_shorthand(r));
                if (kind == QLatin1String("remote") && name.endsWith(QLatin1String("/HEAD"))) continue;
                GObj obj;
                if (git_reference_peel(obj.out(), r, GIT_OBJECT_COMMIT) != 0) continue;
                labels[oidStr(git_object_id(obj))].append({name, kind, kind == QLatin1String("local") && name == headBranch});
            }
        }
        if (res.detached && headValid) labels[oidStr(&headOid)].prepend({QStringLiteral("HEAD"), QStringLiteral("head"), true});
        for (auto &v : labels) {
            std::stable_sort(v.begin(), v.end(), [](const RefLabel &a, const RefLabel &b) {
                if (a.current != b.current) return a.current;
                return kindOrder(a.kind) < kindOrder(b.kind);
            });
        }
    }

    // history walk + lane layout
    Lanes lanes;
    if (headValid) {
        const int n = dirtyCount(repo);
        git_oid zero;
        std::memset(&zero, 0, sizeof zero);
        // the current branch (and the WIP row above it) always runs down the
        // leftmost lane, however many newer commits other branches have
        lanes.s.push_back({true, n > 0 ? zero : headOid, lanes.newColor()});
        if (n > 0) {
            CommitRow row;
            lanes.place(&zero, {headOid}, row, res.edges);
            row.wip = true;
            res.wipCount = n;
            row.time = QDateTime::currentSecsSinceEpoch();
            res.rows.push_back(std::move(row));
        }
    }

    // author table: each distinct (name, email) stored once
    QHash<QByteArray, uint32_t> authorIdx;
    auto authorOf = [&](const git_signature *a) -> uint32_t {
        QByteArray key(a->email);
        key += '\n';
        key += a->name;
        auto it = authorIdx.constFind(key);
        if (it != authorIdx.constEnd()) return *it;
        const uint32_t i = uint32_t(res.authorNames.size());
        res.authorNames << u8(a->name);
        res.authorEmails << u8(a->email);
        authorIdx.insert(key, i);
        return i;
    };
    res.rows.reserve(4096);
    res.edges.reserve(4096 * 3);

    GWalk walk;
    if (git_revwalk_new(walk.out(), repo) == 0) {
        git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
        for (const char *g : {"refs/heads", "refs/remotes", "refs/tags"}) git_revwalk_push_glob(walk, g);
        if (headValid) git_revwalk_push_head(walk);

        git_oid oid;
        std::vector<git_oid> parents;
        while (git_revwalk_next(&oid, walk) == 0) {
            GCommit c;
            if (git_commit_lookup(c.out(), repo, &oid) != 0) continue;
            parents.clear();
            const unsigned pc = git_commit_parentcount(c);
            for (unsigned i = 0; i < pc; ++i) parents.push_back(*git_commit_parent_id(c, i));

            CommitRow row;
            std::memcpy(row.oid.data(), oid.id, 20);
            lanes.place(&oid, parents, row, res.edges);
            const git_signature *a = git_commit_author(c);
            row.author = authorOf(a);
            row.time = a->when.time;
            row.merge = pc > 1;
            if (!labels.isEmpty()) {
                auto it = labels.find(oidStr(&oid));
                if (it != labels.end()) {
                    res.refs.insert(int(res.rows.size()), *it);
                    row.hasRefs = true;
                    labels.erase(it);
                }
            }
            res.rows.push_back(std::move(row));
        }
    }
    res.lanes = lanes.width;
    res.rows.shrink_to_fit();
    res.edges.shrink_to_fit();

    // branches
    QVariantList locals, remotes;
    {
        GBranchIter bi;
        if (git_branch_iterator_new(bi.out(), repo, GIT_BRANCH_ALL) == 0) {
            git_reference *r = nullptr;
            git_branch_t bt;
            while (git_branch_next(&r, &bt, bi) == 0) {
                GRef ref;
                ref.p = r;
                const char *nm = nullptr;
                git_branch_name(&nm, r);
                const QString name = u8(nm);
                if (bt == GIT_BRANCH_REMOTE && name.endsWith(QLatin1String("/HEAD"))) continue;
                QString id;
                GObj obj;
                if (git_reference_peel(obj.out(), r, GIT_OBJECT_COMMIT) == 0) id = oidStr(git_object_id(obj));
                QVariantMap m{{"name", name},
                              {"kind", bt == GIT_BRANCH_LOCAL ? "local" : "remote"},
                              {"id", id},
                              {"current", git_branch_is_head(r) == 1},
                              {"ahead", 0},
                              {"behind", 0}};
                if (bt == GIT_BRANCH_LOCAL) {
                    GRef up;
                    if (git_branch_upstream(up.out(), r) == 0) {
                        const char *un = nullptr;
                        git_branch_name(&un, up);
                        m["upstream"] = u8(un);
                        const git_oid *l = git_reference_target(r);
                        const git_oid *u = git_reference_target(up);
                        size_t ah = 0, bh = 0;
                        if (l && u && git_graph_ahead_behind(&ah, &bh, repo, l, u) == 0) {
                            m["ahead"] = int(ah);
                            m["behind"] = int(bh);
                        }
                    }
                    if (m["current"].toBool()) {
                        res.upstream = m.value("upstream").toString();
                        res.ahead = m["ahead"].toInt();
                        res.behind = m["behind"].toInt();
                    }
                    locals.append(m);
                } else {
                    remotes.append(m);
                }
            }
        }
        auto byName = [](const QVariant &a, const QVariant &b) {
            const auto ma = a.toMap(), mb = b.toMap();
            if (ma["current"].toBool() != mb["current"].toBool()) return ma["current"].toBool();
            return ma["name"].toString().localeAwareCompare(mb["name"].toString()) < 0;
        };
        std::sort(locals.begin(), locals.end(), byName);
        std::sort(remotes.begin(), remotes.end(), byName);
        res.branches = locals + remotes;
    }

    // tags (newest version first)
    {
        git_strarray arr{};
        if (git_tag_list(&arr, repo) == 0) {
            QStringList names;
            for (size_t i = 0; i < arr.count; ++i) names << u8(arr.strings[i]);
            git_strarray_dispose(&arr);
            QCollator col;
            col.setNumericMode(true);
            std::sort(names.begin(), names.end(), [&](const QString &a, const QString &b) { return col.compare(a, b) > 0; });
            for (const QString &n : names) {
                GObj o, c;
                if (git_revparse_single(o.out(), repo, ("refs/tags/" + n).toUtf8().constData()) != 0) continue;
                if (git_object_peel(c.out(), o, GIT_OBJECT_COMMIT) != 0) continue;
                res.tags.append(QVariantMap{{"name", n}, {"kind", "tag"}, {"id", oidStr(git_object_id(c))}});
            }
        }
    }

    // stashes
    git_stash_foreach(
        repo,
        [](size_t index, const char *msg, const git_oid *id, void *payload) -> int {
            auto *list = static_cast<QVariantList *>(payload);
            list->append(QVariantMap{{"name", u8(msg)}, {"kind", "stash"}, {"index", int(index)}, {"id", oidStr(id)}});
            return 0;
        },
        &res.stashes);

    res.fingerprint = fingerprintOf(repo);
    return res;
}

QVariantMap details(const QString &root, const QString &id)
{
    QVariantMap out;
    GRepo r;
    if (!openRepo(r, root)) return {{"error", gitErr()}};

    git_diff_options o;
    git_diff_options_init(&o, GIT_DIFF_OPTIONS_VERSION);
    GDiff diff;
    if (makeDiff(diff.out(), r, id, &o) != 0) return {{"error", gitErr()}};

    QVariantList files;
    int totalAdd = 0, totalDel = 0;
    const size_t n = git_diff_num_deltas(diff);
    for (size_t i = 0; i < n; ++i) {
        const git_diff_delta *d = git_diff_get_delta(diff, i);
        size_t ctx = 0, add = 0, del = 0;
        bool binary = d->flags & GIT_DIFF_FLAG_BINARY;
        if (i < size_t(kMaxStatFiles)) {
            GPatch p;
            if (git_patch_from_diff(p.out(), diff, i) == 0 && p) {
                git_patch_line_stats(&ctx, &add, &del, p);
                binary = binary || (git_patch_get_delta(p)->flags & GIT_DIFF_FLAG_BINARY);
            }
        }
        totalAdd += int(add);
        totalDel += int(del);
        const QString path = u8(d->new_file.path ? d->new_file.path : d->old_file.path);
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        files.append(QVariantMap{
            {"path", path},
            {"oldPath", d->status == GIT_DELTA_RENAMED ? u8(d->old_file.path) : QString()},
            {"dir", slash >= 0 ? path.left(slash + 1) : QString()},
            {"file", slash >= 0 ? path.mid(slash + 1) : path},
            {"status", QString::fromLatin1(statusLetter(d->status))},
            {"add", int(add)},
            {"del", int(del)},
            {"binary", binary},
        });
    }
    out["files"] = files;
    out["add"] = totalAdd;
    out["del"] = totalDel;
    out["id"] = id;

    if (id == QLatin1String("WIP")) {
        out["wip"] = true;
        out["summary"] = QStringLiteral("Uncommitted changes");
        out["body"] = QString();
        GRef head;
        QStringList parents;
        if (git_repository_head(head.out(), r) == 0 && git_reference_target(head)) parents << oidStr(git_reference_target(head));
        out["parents"] = parents;
        return out;
    }

    git_oid oid;
    git_oid_fromstr(&oid, id.toLatin1().constData());
    GCommit c;
    if (git_commit_lookup(c.out(), r, &oid) != 0) return {{"error", gitErr()}};
    QStringList parents;
    for (unsigned i = 0; i < git_commit_parentcount(c); ++i) parents << oidStr(git_commit_parent_id(c, i));
    const git_signature *a = git_commit_author(c);
    const git_signature *cm = git_commit_committer(c);
    const QString message = u8(git_commit_message(c));
    const int nl = message.indexOf(QLatin1Char('\n'));
    out["wip"] = false;
    out["parents"] = parents;
    out["author"] = u8(a->name);
    out["email"] = u8(a->email);
    out["time"] = qint64(a->when.time) * 1000;
    out["committer"] = u8(cm->name);
    out["committerEmail"] = u8(cm->email);
    out["ctime"] = qint64(cm->when.time) * 1000;
    out["message"] = message;
    out["summary"] = u8(git_commit_summary(c));
    out["body"] = nl >= 0 ? reflow(message.mid(nl + 1).trimmed()) : QString();
    return out;
}

DiffData fileDiff(const QString &root, const QString &id, const QString &path, const QString &oldPath, bool full)
{
    DiffData res;
    res.id = id;
    res.path = path;
    GRepo r;
    if (!openRepo(r, root)) {
        res.error = gitErr();
        return res;
    }
    git_diff_options o;
    git_diff_options_init(&o, GIT_DIFF_OPTIONS_VERSION);
    o.context_lines = full ? 1000000 : 3;
    o.flags |= GIT_DIFF_DISABLE_PATHSPEC_MATCH;
    QByteArray p1 = path.toUtf8(), p2 = oldPath.toUtf8();
    char *specs[2] = {p1.data(), p2.data()};
    o.pathspec.strings = specs;
    o.pathspec.count = oldPath.isEmpty() ? 1 : 2;

    GDiff diff;
    if (makeDiff(diff.out(), r, id, &o) != 0) {
        res.error = gitErr();
        return res;
    }
    const size_t n = git_diff_num_deltas(diff);
    size_t idx = n;
    for (size_t i = 0; i < n; ++i) {
        const git_diff_delta *d = git_diff_get_delta(diff, i);
        if ((d->new_file.path && path == u8(d->new_file.path)) || (d->old_file.path && path == u8(d->old_file.path))) {
            idx = i;
            break;
        }
    }
    if (idx == n) {
        res.error = path.endsWith(QLatin1Char('/')) ? QStringLiteral("Untracked folder. Stage it to see its files.")
                                                    : QStringLiteral("File not found in this change.");
        return res;
    }
    const git_diff_delta *d = git_diff_get_delta(diff, idx);
    res.status = QString::fromLatin1(statusLetter(d->status));
    if (d->old_file.path && u8(d->old_file.path) != path) res.oldPath = u8(d->old_file.path);

    if (isImagePath(path)) {
        res.image = true;
        const bool added = d->status == GIT_DELTA_ADDED || d->status == GIT_DELTA_UNTRACKED;
        const bool deleted = d->status == GIT_DELTA_DELETED;
        if (!added && !git_oid_is_zero(&d->old_file.id)) res.oldImage.loadFromData(imageBytes(root, "blob/" + oidStr(&d->old_file.id)));
        if (!deleted) {
            if (id.startsWith(QLatin1String("WIP")) && id != QLatin1String("WIP:staged"))
                res.newImage.loadFromData(imageBytes(root, "file/" + QString::fromLatin1(QUrl::toPercentEncoding(path))));
            else if (!git_oid_is_zero(&d->new_file.id))
                res.newImage.loadFromData(imageBytes(root, "blob/" + oidStr(&d->new_file.id)));
        }
        return res;
    }

    GPatch p;
    if (git_patch_from_diff(p.out(), diff, idx) != 0 || !p) {
        res.binary = d->flags & GIT_DIFF_FLAG_BINARY;
        return res;
    }
    res.binary = git_patch_get_delta(p)->flags & GIT_DIFF_FLAG_BINARY;
    int total = 0;
    const size_t nh = git_patch_num_hunks(p);
    for (size_t h = 0; h < nh && !res.truncated; ++h) {
        const git_diff_hunk *hunk = nullptr;
        size_t nl = 0;
        if (git_patch_get_hunk(&hunk, &nl, p, h) != 0) break;
        Hunk out;
        out.header = QString::fromUtf8(hunk->header, int(hunk->header_len)).trimmed();
        out.lines.reserve(nl);
        for (size_t l = 0; l < nl; ++l) {
            const git_diff_line *line = nullptr;
            if (git_patch_get_line_in_hunk(&line, p, h, l) != 0) break;
            const char t = line->origin;
            if (t != '+' && t != '-' && t != ' ') continue;
            QString s = QString::fromUtf8(line->content, int(line->content_len));
            while (s.endsWith(QLatin1Char('\n')) || s.endsWith(QLatin1Char('\r'))) s.chop(1);
            if (s.contains(QLatin1Char('\t'))) {
                QString e;
                e.reserve(s.size() + 16);
                for (QChar ch : s) {
                    if (ch == QLatin1Char('\t')) e += QString(4 - (e.size() % 4), QLatin1Char(' '));
                    else e += ch;
                }
                s = std::move(e);
            }
            out.lines.push_back({t, line->old_lineno, line->new_lineno, std::move(s)});
            if (++total >= kMaxDiffLines) {
                res.truncated = true;
                break;
            }
        }
        res.hunks.push_back(std::move(out));
    }
    return res;
}

QByteArray partialPatch(const QString &root, bool staged, const QString &path, const QSet<int> &selDel,
                        const QSet<int> &selAdd, QString *err, bool *wholeFile)
{
    *wholeFile = false;
    GRepo r;
    if (!openRepo(r, root)) {
        *err = gitErr();
        return {};
    }
    // same engine and options as the displayed diff, so line numbers match
    git_diff_options o;
    git_diff_options_init(&o, GIT_DIFF_OPTIONS_VERSION);
    o.context_lines = 3;
    o.flags |= GIT_DIFF_DISABLE_PATHSPEC_MATCH;
    QByteArray p1 = path.toUtf8();
    char *specs[1] = {p1.data()};
    o.pathspec.strings = specs;
    o.pathspec.count = 1;
    GDiff diff;
    if (makeDiff(diff.out(), r, staged ? QStringLiteral("WIP:staged") : QStringLiteral("WIP:unstaged"), &o) != 0) {
        *err = gitErr();
        return {};
    }
    size_t idx = SIZE_MAX;
    for (size_t i = 0; i < git_diff_num_deltas(diff); ++i) {
        const git_diff_delta *d = git_diff_get_delta(diff, i);
        if (d->new_file.path && path == u8(d->new_file.path)) idx = i;
    }
    if (idx == SIZE_MAX) {
        *err = QStringLiteral("The file has no changes left here.");
        return {};
    }
    const git_diff_delta *d = git_diff_get_delta(diff, idx);
    if (d->status != GIT_DELTA_MODIFIED) { // new/deleted/renamed: whole file only
        *wholeFile = true;
        return {};
    }
    GPatch p;
    if (git_patch_from_diff(p.out(), diff, idx) != 0 || !p || (git_patch_get_delta(p)->flags & GIT_DIFF_FLAG_BINARY)) {
        *wholeFile = true;
        return {};
    }
    // file header: everything libgit2 prints before the first hunk
    git_buf buf = GIT_BUF_INIT;
    if (git_patch_to_buf(&buf, p) != 0) {
        *err = gitErr();
        return {};
    }
    QByteArray full(buf.ptr, qsizetype(buf.size));
    git_buf_dispose(&buf);
    const qsizetype at = full.startsWith("@@ ") ? 0 : full.indexOf("\n@@ ");
    if (at < 0) {
        *err = QStringLiteral("No text changes to stage.");
        return {};
    }
    QByteArray out = full.left(at ? at + 1 : 0);

    // Staging (forward): unselected '+' dropped, unselected '-' kept as context.
    // Unstaging (applied in reverse): unselected '-' dropped, unselected '+' kept as context.
    int cum = 0, picked = 0;
    const size_t nh = git_patch_num_hunks(p);
    for (size_t h = 0; h < nh; ++h) {
        const git_diff_hunk *hunk = nullptr;
        size_t nl = 0;
        if (git_patch_get_hunk(&hunk, &nl, p, h) != 0) break;
        QByteArray body;
        int oc = 0, nc = 0, changes = 0;
        bool kept = true; // whether the previous line was emitted (for "\ No newline")
        for (size_t l = 0; l < nl; ++l) {
            const git_diff_line *line = nullptr;
            if (git_patch_get_line_in_hunk(&line, p, h, l) != 0) break;
            const char t = line->origin;
            if (t == GIT_DIFF_LINE_CONTEXT_EOFNL || t == GIT_DIFF_LINE_ADD_EOFNL || t == GIT_DIFF_LINE_DEL_EOFNL) {
                if (kept) body += "\\ No newline at end of file\n";
                continue;
            }
            char ch = t;
            if (t == '+' && !selAdd.contains(line->new_lineno)) ch = staged ? ' ' : 0;
            else if (t == '-' && !selDel.contains(line->old_lineno)) ch = staged ? 0 : ' ';
            kept = ch != 0;
            if (!kept) continue;
            if (ch != ' ') ++changes;
            if (ch != '+') ++oc;
            if (ch != '-') ++nc;
            body += ch;
            body.append(line->content, qsizetype(line->content_len));
            if (!body.endsWith('\n')) body += '\n';
        }
        if (!changes) continue;
        picked += changes;
        // the side git checks against stays exact; the other side is derived
        int os, ns;
        if (!staged) {
            os = hunk->old_start;
            const int pos = (hunk->old_lines ? os : os + 1) + cum;
            ns = nc ? pos : pos - 1;
        } else {
            ns = hunk->new_start;
            const int pos = (hunk->new_lines ? ns : ns + 1) - cum;
            os = oc ? pos : pos - 1;
        }
        cum += nc - oc;
        out += QStringLiteral("@@ -%1,%2 +%3,%4 @@\n").arg(os).arg(oc).arg(ns).arg(nc).toLatin1();
        out += body;
    }
    if (!picked) {
        *err = QStringLiteral("Those lines are no longer changed. Reloading.");
        return {};
    }
    return out;
}

WorkStatus workStatus(const QString &root)
{
    WorkStatus ws;
    GRepo r;
    if (!openRepo(r, root)) {
        ws.error = gitErr();
        return ws;
    }
    GRef head;
    ws.hasHead = git_repository_head(head.out(), r) == 0;
    git_status_options o;
    git_status_options_init(&o, GIT_STATUS_OPTIONS_VERSION);
    o.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    o.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX | GIT_STATUS_OPT_EXCLUDE_SUBMODULES
              | GIT_STATUS_OPT_SORT_CASE_SENSITIVELY;
    GStatus st;
    if (git_status_list_new(st.out(), r, &o) != 0) {
        ws.error = gitErr();
        return ws;
    }
    auto entry = [](const QString &path, const QString &oldPath, const char *status, bool untracked) {
        const int slash = path.lastIndexOf(QLatin1Char('/'), path.endsWith(QLatin1Char('/')) ? -2 : -1);
        return QVariantMap{{"path", path},
                           {"oldPath", oldPath},
                           {"dir", slash >= 0 ? path.left(slash + 1) : QString()},
                           {"file", slash >= 0 ? path.mid(slash + 1) : path},
                           {"status", QString::fromLatin1(status)},
                           {"untracked", untracked}};
    };
    const size_t n = git_status_list_entrycount(st);
    for (size_t i = 0; i < n; ++i) {
        const git_status_entry *e = git_status_byindex(st, i);
        const unsigned s = e->status;
        if (s & GIT_STATUS_CONFLICTED) {
            const git_diff_delta *d = e->index_to_workdir ? e->index_to_workdir : e->head_to_index;
            if (d) ws.conflicted.append(entry(u8(d->new_file.path ? d->new_file.path : d->old_file.path), QString(), "C", false));
            continue;
        }
        if (s & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE)) {
            const git_diff_delta *d = e->head_to_index;
            const char *letter = (s & GIT_STATUS_INDEX_NEW) ? "A" : (s & GIT_STATUS_INDEX_DELETED) ? "D" : (s & GIT_STATUS_INDEX_RENAMED) ? "R"
                               : (s & GIT_STATUS_INDEX_TYPECHANGE) ? "T" : "M";
            const QString path = u8(d->new_file.path ? d->new_file.path : d->old_file.path);
            ws.staged.append(entry(path, (s & GIT_STATUS_INDEX_RENAMED) ? u8(d->old_file.path) : QString(), letter, false));
        }
        if (s & (GIT_STATUS_WT_NEW | GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED | GIT_STATUS_WT_TYPECHANGE | GIT_STATUS_WT_RENAMED)) {
            const git_diff_delta *d = e->index_to_workdir;
            const bool untracked = s & GIT_STATUS_WT_NEW;
            const char *letter = untracked ? "A" : (s & GIT_STATUS_WT_DELETED) ? "D" : (s & GIT_STATUS_WT_TYPECHANGE) ? "T" : "M";
            ws.unstaged.append(entry(u8(d->new_file.path ? d->new_file.path : d->old_file.path), QString(), letter, untracked));
        }
    }
    ws.fingerprint = fingerprintOf(r);
    return ws;
}

QByteArray imageBytes(const QString &root, const QString &spec)
{
    if (spec.startsWith(QLatin1String("blob/"))) {
        GRepo r;
        if (!openRepo(r, root)) return {};
        git_oid oid;
        if (git_oid_fromstr(&oid, spec.mid(5).toLatin1().constData()) != 0) return {};
        GBlob b;
        if (git_blob_lookup(b.out(), r, &oid) != 0) return {};
        return QByteArray(static_cast<const char *>(git_blob_rawcontent(b)), qsizetype(git_blob_rawsize(b)));
    }
    if (spec.startsWith(QLatin1String("file/"))) {
        const QString rel = QUrl::fromPercentEncoding(spec.mid(5).toUtf8());
        if (rel.contains(QLatin1String(".."))) return {};
        QFile f(root + QLatin1Char('/') + rel);
        if (f.open(QIODevice::ReadOnly)) return f.readAll();
    }
    return {};
}

} // namespace gitload
