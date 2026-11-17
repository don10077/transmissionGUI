#pragma once

#include <QNetworkReply>
#include <QTimer>

/** Прерывает загрузку по общему дедлайну или при превышении лимита. */
inline void boundDownload(QNetworkReply* reply, qint64 maxBytes, int timeoutMs)
{
    reply->setReadBufferSize(maxBytes + 1);
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, maxBytes] {
        if (reply->bytesAvailable() > maxBytes)
            reply->abort();
    });
    QTimer::singleShot(timeoutMs, reply, [reply] {
        if (reply->isRunning())
            reply->abort();
    });
}
