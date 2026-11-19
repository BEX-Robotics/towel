#pragma once
// Thin RAII helpers around the libgit2 C API.
#include <git2.h>
#include <QString>

template <typename T, void (*F)(T *)>
struct GPtr {
    T *p = nullptr;
    GPtr() = default;
    GPtr(const GPtr &) = delete;
    GPtr &operator=(const GPtr &) = delete;
    ~GPtr() { reset(); }
    void reset(T *n = nullptr) { if (p) F(p); p = n; }
    T **out() { reset(); return &p; }
    T *get() const { return p; }
    T *release() { T *t = p; p = nullptr; return t; }
    operator T *() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

using GRepo = GPtr<git_repository, git_repository_free>;
using GCommit = GPtr<git_commit, git_commit_free>;
using GTree = GPtr<git_tree, git_tree_free>;
using GDiff = GPtr<git_diff, git_diff_free>;
using GPatch = GPtr<git_patch, git_patch_free>;
using GRef = GPtr<git_reference, git_reference_free>;
using GObj = GPtr<git_object, git_object_free>;
using GWalk = GPtr<git_revwalk, git_revwalk_free>;
using GBlob = GPtr<git_blob, git_blob_free>;
using GStatus = GPtr<git_status_list, git_status_list_free>;
using GRefIter = GPtr<git_reference_iterator, git_reference_iterator_free>;
using GBranchIter = GPtr<git_branch_iterator, git_branch_iterator_free>;

inline QString gitErr()
{
    const git_error *e = git_error_last();
    return (e && e->message) ? QString::fromUtf8(e->message) : QStringLiteral("unknown git error");
}

inline QString oidStr(const git_oid *o)
{
    char buf[80];
    git_oid_tostr(buf, sizeof buf, o);
    return QString::fromLatin1(buf);
}

inline QString u8(const char *s) { return s ? QString::fromUtf8(s) : QString(); }

inline bool openRepo(GRepo &r, const QString &path)
{
    return git_repository_open(r.out(), path.toUtf8().constData()) == 0;
}
