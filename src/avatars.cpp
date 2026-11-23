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
constexpr int maxInflight = 6;
constexpr int avatarPx = 64;
} // namespace

AvatarCache *AvatarCache::instance() {
    static AvatarCache *c = new AvatarCache;
    return c;
}

AvatarCache::AvatarCache(QObject *parent) : QObject(parent) {
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/avatars2") /* v2: no Gravatar identicons */;
    QDir().mkpath(m_dir);
}

QString AvatarCache::keyFor(const QString &email) {
    return QString::fromLatin1(QCryptographicHash::hash(email.trimmed().toLower().toUtf8(), QCryptographicHash::Md5).toHex());
}

QImage AvatarCache::get(const QString &email) {
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

namespace {
// GitHub's default identicon (a 2-color pixel pattern) is not a photo
bool isIdenticon(const QImage &img) {
    const QImage t = img.scaled(24, 24, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    QSet<QRgb> colors;
    for (int y = 0; y < t.height() && colors.size() <= 3; ++y)
        for (int x = 0; x < t.width(); ++x) colors.insert(t.pixel(x, y));
    return colors.size() <= 3;
}
} // namespace

void AvatarCache::request(const QString &key, const QString &email) {
    const QString file = m_dir + QLatin1Char('/') + key + QStringLiteral(".png");
    if (QFileInfo fi(file); fi.exists() && fi.lastModified().daysTo(QDateTime::currentDateTime()) < 14) {
        QImage img(file);
        if (!img.isNull() && !isIdenticon(img)) {
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

void AvatarCache::pump() {
    while (m_inflight < maxInflight && !m_queue.isEmpty()) {
        const auto [key, email] = m_queue.dequeue();
        QUrl url;
        static const QRegularExpression ghID(QStringLiteral("^(\\d+)\\+[^@]+@users\\.noreply\\.github\\.com$"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression ghUser(QStringLiteral("^([^@+]+)@users\\.noreply\\.github\\.com$"), QRegularExpression::CaseInsensitiveOption);
        const QString e = email.trimmed().toLower();
        if (auto m = ghID.match(e); m.hasMatch())
            url = QUrl(QStringLiteral("https://avatars.githubusercontent.com/u/%1?s=%2&v=4").arg(m.captured(1)).arg(avatarPx));
        else if (auto m2 = ghUser.match(e); m2.hasMatch())
            url = QUrl(QStringLiteral("https://github.com/%1.png?size=%2").arg(m2.captured(1)).arg(avatarPx));
        else
            url = QUrl(QStringLiteral("https://www.gravatar.com/avatar/%1?s=%2&d=404").arg(key).arg(avatarPx));

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
            const bool generated = !img.isNull() && isIdenticon(img); // draw initials instead
            if (img.isNull() || generated) {
                {
                    QMutexLocker l(&m_lock);
                    m_failed.insert(key);
                    m_pending.remove(key);
                }

                // only remember real "not found" answers, not offline moments
                if (generated || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
                    QFile f(m_dir + QLatin1Char('/') + key + QStringLiteral(".none"));
                    if (f.open(QIODevice::WriteOnly)) f.close();
                }
            } else {
                img = img.scaled(avatarPx, avatarPx, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
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

void AvatarCache::paint(QPainter *p, const QRectF &r, const QString &email, const QString &name, const QColor &ring, qreal ringWidth) {
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
        // no photo: initials on a vivid color picked from the email
        static const QRgb palette[] = {0xe5484d, 0xf76b15, 0xffb224, 0x46a758, 0x12a594, 0x00a2c7, 0x0090ff, 0x3e63dd,
            0x6e56cf, 0x8e4ec6, 0xd6409f, 0xe54666, 0xbd4b00, 0x5bb98b, 0xab4aba, 0x7ce2fe};
        const QByteArray hash = QCryptographicHash::hash(email.trimmed().toLower().toUtf8(), QCryptographicHash::Md5);
        const QColor bg = QColor::fromRgb(palette[uchar(hash[0]) % (sizeof(palette) / sizeof(palette[0]))]);
        p->fillRect(inner, bg);
        const bool light = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue() > 165;
        QString initials;
        for (const QString &part : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            initials += part.at(0).toUpper();
            if (initials.size() == 1) break; // one big letter reads better at graph size
        }
        if (initials.isEmpty() && !email.isEmpty()) initials = email.at(0).toUpper();
        QFont f = p->font();
        f.setPixelSize(std::max(7, int(inner.height() * 0.58)));
        f.setBold(true);
        p->setFont(f);
        p->setPen(light ? QColor(0x12, 0x14, 0x18) : QColor(255, 255, 255));
        p->drawText(inner, Qt::AlignCenter, initials);
    }
    p->restore();
}
