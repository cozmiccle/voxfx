#include "sources/link_importer.h"

#include "sources/link_resolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

namespace vfx {

namespace {
constexpr qint64 kMaxPageBytes = 3 * 1024 * 1024;
constexpr qint64 kMaxAudioBytes = 25 * 1024 * 1024;
constexpr int kPauseMs = 1000;
}

LinkImporter::LinkImporter(QString downloadDir, QObject* parent)
    : QObject(parent), dir_(std::move(downloadDir)) {
    QDir().mkpath(dir_);
    net_.setTransferTimeout(20000);
}

QNetworkRequest LinkImporter::request(const QString& url) const {
    QNetworkRequest r{QUrl(url)};
    r.setHeader(QNetworkRequest::UserAgentHeader, "voxfx/0.1 (personal soundboard)");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return r;
}

void LinkImporter::import(const QString& url) {
    QUrl u = QUrl::fromUserInput(url.trimmed());
    if (!u.isValid() || (u.scheme() != "https" && u.scheme() != "http")) {
        emit failed(url, "not a web link");
        return;
    }
    queue_.enqueue(u.toString());
    if (!busy_) next();
}

void LinkImporter::next() {
    if (queue_.isEmpty()) { busy_ = false; return; }
    busy_ = true;
    QString url = queue_.dequeue();
    if (looksLikeAudioUrl(url.toStdString())) fetchAudio(url, url, QString());
    else fetchPage(url);
}

void LinkImporter::fetchPage(const QString& url) {
    emit progress("Reading " + QUrl(url).host() + "…");
    QNetworkReply* reply = net_.get(request(url));
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64) {
        if (got > kMaxPageBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(url, reply->errorString());
            QTimer::singleShot(kPauseMs, this, &LinkImporter::next);
            return;
        }
        QString ctype = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        QByteArray body = reply->readAll();
        QString finalUrl = reply->url().toString();
        if (ctype.startsWith("audio/")) {
            // The link pointed straight at audio after all: save what we already have.
            QString name = QString::fromStdString(sanitizeFileName(QUrl(finalUrl).fileName().toStdString()));
            QString path = QDir(dir_).filePath(name);
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) { f.write(body); f.close(); }
            emit imported(path, QFileInfo(name).completeBaseName(),
                          siteName(siteOf(url.toStdString())), url);
            QTimer::singleShot(kPauseMs, this, &LinkImporter::next);
            return;
        }
        auto res = resolveAudioFromHtml(body.toStdString(), finalUrl.toStdString());
        if (!res) {
            emit failed(url, "no sound file found on that page (the site layout may have changed)");
            QTimer::singleShot(kPauseMs, this, &LinkImporter::next);
            return;
        }
        QString audio = QString::fromStdString(res->audioUrl);
        QString title = QString::fromStdString(res->title);
        QTimer::singleShot(kPauseMs / 2, this, [this, url, audio, title] { fetchAudio(url, audio, title); });
    });
}

void LinkImporter::fetchAudio(const QString& pageUrl, const QString& audioUrl, const QString& title) {
    emit progress("Downloading " + QUrl(audioUrl).fileName() + "…");
    QNetworkReply* reply = net_.get(request(audioUrl));
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64) {
        if (got > kMaxAudioBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, pageUrl, audioUrl, title] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(pageUrl, reply->errorString());
        } else {
            QString base = QUrl(audioUrl).fileName();
            QString name = QString::fromStdString(sanitizeFileName(base.toStdString()));
            QString path = QDir(dir_).filePath(name);
            // Don't overwrite a different sound with the same file name.
            for (int i = 2; QFile::exists(path); ++i)
                path = QDir(dir_).filePath(QFileInfo(name).completeBaseName() + QString("_%1.").arg(i) +
                                           QFileInfo(name).suffix());
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                emit failed(pageUrl, "cannot write " + path);
            } else {
                f.write(reply->readAll());
                f.close();
                QString t = title.isEmpty() ? QFileInfo(name).completeBaseName() : title;
                emit imported(path, t, siteName(siteOf(pageUrl.toStdString())), pageUrl);
            }
        }
        QTimer::singleShot(kPauseMs, this, &LinkImporter::next);
    });
}

} // namespace vfx
