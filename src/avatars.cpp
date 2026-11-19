#include "avatars.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr int kMaxInflight = 6;
constexpr int kSize = 64;
}

AvatarCache *AvatarCache::instance()
{
    static AvatarCache *c = new AvatarCache;
    return c;
}

AvatarCache::AvatarCache(QObject *parent) : QObject(parent)
{
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/avatars");
    QDir().mkpath(m_dir);
}

QString AvatarCache::keyFor(const QString &email)
{
    return QString::fromLatin1(QCryptographicHash::hash(email.trimmed().toLower().toUtf8(), QCryptographicHash::Md5).toHex());
}

QImage AvatarCache::get(const QString &email)
{
    // May be called from the scene graph render thread: only touch locked state,
    // hand the actual loading to the main thread.
    if (email.isEmpty()) return {};
    const QString key = keyFor(email);
    {
        QMutexLocker l(&m_lock);
        auto it = m_mem.constFind(key);
        if (it != m_mem.constEnd()) return *it;
        if (m_failed.contains(key) || m_pending.contains(key)) return {};
        m_pending.insert(key);
    }
    QMetaObject::invokeMethod(this, "request", Qt::QueuedConnection, Q_ARG(QString, key), Q_ARG(QString, email));
    return {};
}

void AvatarCache::request(const QString &key, const QString &email)
{
    const QString file = m_dir + QLatin1Char('/') + key + QStringLiteral(".png");
    if (QFileInfo fi(file); fi.exists() && fi.lastModified().daysTo(QDateTime::currentDateTime()) < 14) {
        QImage img(file);
        if (!img.isNull()) {
            {
                QMutexLocker l(&m_lock);
                m_mem.insert(key, img);
                m_pending.remove(key);
            }
            emit ready(key);
            return;
        }
    }
    const QString miss = m_dir + QLatin1Char('/') + key + QStringLiteral(".none");
    if (QFileInfo fi(miss); fi.exists() && fi.lastModified().daysTo(QDateTime::currentDateTime()) < 7) {
        QMutexLocker l(&m_lock);
        m_failed.insert(key);
        m_pending.remove(key);
        return;
    }
    m_queue.enqueue({key, email});
    pump();
}

void AvatarCache::pump()
{
    while (m_inflight < kMaxInflight && !m_queue.isEmpty()) {
        const auto [key, email] = m_queue.dequeue();
        QUrl url;
        static const QRegularExpression ghId(QStringLiteral("^(\\d+)\\+[^@]+@users\\.noreply\\.github\\.com$"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression ghUser(QStringLiteral("^([^@+]+)@users\\.noreply\\.github\\.com$"), QRegularExpression::CaseInsensitiveOption);
        const QString e = email.trimmed().toLower();
        if (auto m = ghId.match(e); m.hasMatch())
            url = QUrl(QStringLiteral("https://avatars.githubusercontent.com/u/%1?s=%2&v=4").arg(m.captured(1)).arg(kSize));
        else if (auto m2 = ghUser.match(e); m2.hasMatch())
            url = QUrl(QStringLiteral("https://github.com/%1.png?size=%2").arg(m2.captured(1)).arg(kSize));
        else
            url = QUrl(QStringLiteral("https://www.gravatar.com/avatar/%1?s=%2&d=retro").arg(key).arg(kSize));

        QNetworkRequest req(url);
        req.setTransferTimeout(10000);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Towel"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        ++m_inflight;
        QNetworkReply *reply = m_nam.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
            reply->deleteLater();
            --m_inflight;
            QImage img;
            if (reply->error() == QNetworkReply::NoError) img.loadFromData(reply->readAll());
            if (img.isNull()) {
                {
                    QMutexLocker l(&m_lock);
                    m_failed.insert(key);
                    m_pending.remove(key);
                }
                // only remember real "not found" answers, not offline moments
                if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
                    QFile f(m_dir + QLatin1Char('/') + key + QStringLiteral(".none"));
                    if (f.open(QIODevice::WriteOnly)) f.close();
                }
            } else {
                img = img.scaled(kSize, kSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
                img.save(m_dir + QLatin1Char('/') + key + QStringLiteral(".png"));
                {
                    QMutexLocker l(&m_lock);
                    m_mem.insert(key, img);
                    m_pending.remove(key);
                }
                emit ready(key);
            }
            pump();
        });
    }
}

void AvatarCache::paint(QPainter *p, const QRectF &r, const QString &email, const QString &name, const QColor &ring, qreal ringWidth)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->setPen(Qt::NoPen);
    if (ringWidth > 0) {
        p->setBrush(ring);
        p->drawEllipse(r);
    }
    const QRectF inner = r.adjusted(ringWidth, ringWidth, -ringWidth, -ringWidth);

    const QImage img = instance()->get(email);
    QPainterPath clip;
    clip.addEllipse(inner);
    p->setClipPath(clip);
    if (!img.isNull()) {
        p->fillRect(inner, QColor(0x1b, 0x1d, 0x23));
        p->drawImage(inner, img);
    } else {
        // initials on a stable color derived from the email
        const uint h = qHash(email.toLower());
        QColor bg = QColor::fromHsl(int(h % 360), 90, 70);
        p->fillRect(inner, bg);
        QString initials;
        for (const QString &part : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            initials += part.at(0).toUpper();
            if (initials.size() == 2) break;
        }
        if (initials.isEmpty() && !email.isEmpty()) initials = email.at(0).toUpper();
        QFont f = p->font();
        f.setPixelSize(int(inner.height() * 0.42));
        f.setBold(true);
        p->setFont(f);
        p->setPen(QColor(255, 255, 255, 230));
        p->drawText(inner, Qt::AlignCenter, initials);
    }
    p->restore();
}
