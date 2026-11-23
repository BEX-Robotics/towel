#pragma once
// Pure functions that read a repository. They open their own libgit2 handle,
// so they are safe to run on worker threads.
#include "data.h"
#include <QSet>
#include <QVariantMap>

namespace gitload {
LoadResult loadRepo(const QString &start);
QString fingerprint(const QString &root);
QVariantMap details(const QString &root, const QString &id);
DiffData fileDiff(const QString &root, const QString &id, const QString &path, const QString &oldPath, bool full);
QString firstBodyLine(const char *body);

// Working tree state for the commit panel: lists of file maps
// {path, oldPath, dir, file, status, untracked}. Also returns the fingerprint.
struct WorkStatus {
    QVariantList unstaged, staged, conflicted;
    QString fingerprint, error;
    bool hasHead = false;
};

WorkStatus workStatus(const QString &root);

// Patch containing only the selected changed lines of one file, for
// `git apply --cached` (reverse=false, mode "unstaged") or `--cached --reverse`
// (reverse=true, mode "staged"). selDel / selAdd hold old / new line numbers.
// Empty result + *err set on failure; *wholeFile set when partial staging is not
// possible (untracked, renamed, binary) and the caller should stage the file.
QByteArray partialPatch(const QString &root, bool staged, const QString &path, const QSet<int> &selDel,
    const QSet<int> &selAdd, QString *err, bool *wholeFile);
QByteArray imageBytes(const QString &root, const QString &spec); // "blob/<oid>" or "file/<relpath>"
} // namespace gitload
