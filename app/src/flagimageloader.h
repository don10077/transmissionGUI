#pragma once

#include <QCoreApplication>
#include <QNetworkAccessManager>

#include <functional>

class PeerListModel;

/** Загружает архив флагов и передаёт готовые изображения модели пиров. */
class FlagImageLoader final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    /** Привязывает модель пиров и уведомления об ошибках. */
    FlagImageLoader(PeerListModel& peers, std::function<void(const QString&)> notify);
    /** Читает локальный кэш и при необходимости загружает архив. */
    void loadFlagImages(bool allowDownload, bool reportFailure = false);
    /** Прекращает запись настроек, когда приложение применяет импорт. */
    void suspend() { m_settingsImported = true; }

private:
    void downloadFlagImages(const QString& appDir);
    void unpackFlagImages(const QString& appDir, const QByteArray& zip, bool retryDownload);
    void finishFlagImages(const QString& directory = {});

    PeerListModel& m_peerModel;
    std::function<void(const QString&)> m_notify;
    QNetworkAccessManager m_nam;
    bool m_loadingFlags = false;
    bool m_reportFlagFailure = false;
    bool m_settingsImported = false;
};
