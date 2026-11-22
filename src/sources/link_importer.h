#pragma once
// Downloads one sound from a page link (MyInstants, Voicemod, ...) or a direct
// audio URL. Only runs when the user asks; requests are serialised with a pause
// between them so we never hammer a site.
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QString>

namespace vfx {

class LinkImporter : public QObject {
    Q_OBJECT
public:
    explicit LinkImporter(QString downloadDir, QObject* parent = nullptr);
    void import(const QString& url);

signals:
    void progress(const QString& message);
    void imported(const QString& filePath, const QString& title, const QString& source,
                  const QString& sourceUrl);
    void failed(const QString& url, const QString& error);

private:
    void next();
    void fetchPage(const QString& url);
    void fetchAudio(const QString& pageUrl, const QString& audioUrl, const QString& title);
    QNetworkRequest request(const QString& url) const;

    QNetworkAccessManager net_;
    QString dir_;
    QQueue<QString> queue_;
    bool busy_ = false;
};

} // namespace vfx
