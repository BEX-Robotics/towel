#pragma once
// Pure functions that read a repository. They open their own libgit2 handle,
// so they are safe to run on worker threads.
#include "data.h"
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
QByteArray imageBytes(const QString &root, const QString &spec); // "blob/<oid>" or "file/<relpath>"
}
