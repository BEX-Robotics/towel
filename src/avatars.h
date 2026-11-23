#pragma once
#include <QColor>
#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QSet>

class QPainter;

// Fetches avatars (GitHub for noreply addresses, Gravatar "retro" otherwise),
// caches them on disk and in memory. Main thread only.
class AvatarCache : public QObject {
    Q_OBJECT
public:
    static AvatarCache *instance();
    static QString keyFor(const QString &email);

    QImage get(const QString &email); // null image while loading
    static void paint(QPainter *p, const QRectF &r, const QString &email, const QString &name, const QColor &ring, qreal ringWidth);

signals:
    void ready(const QString &key);

private:
    explicit AvatarCache(QObject *parent = nullptr);
    Q_INVOKABLE void request(const QString &key, const QString &email);
    void pump();
    QMutex m_lock; // m_mem / m_failed / m_pending are read from the render thread

    QNetworkAccessManager m_nam;
    QHash<QString, QImage> m_mem;
    QSet<QString> m_pending, m_failed;
    QQueue<QPair<QString, QString>> m_queue; // key, email
    int m_inflight = 0;
    QString m_dir;
};
