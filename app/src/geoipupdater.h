#pragma once

#include <QCoreApplication>
#include <QNetworkAccessManager>

#include "tgui/geoip.h"

class FlagImageLoader;
class PeerListModel;
class QMainWindow;

/** Загружает, проверяет и подменяет базу GeoIP, используемую моделью пиров. */
class GeoIpUpdater final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    /** Связывает сервис с окном и моделью. */
    GeoIpUpdater(QMainWindow& window, PeerListModel& peers, FlagImageLoader& flags);
    /** Загружает сохранённую базу и применяет настройки модели пиров. */
    void initialize();
    /** Проверяет, доступно ли определение страны. */
    bool isLoaded() const { return m_geoip.isLoaded(); }
    /** Запрашивает подтверждение и скачивает свежую базу. */
    void onUpdateGeoIp();

private:
    QMainWindow& m_window;
    PeerListModel& m_peerModel;
    FlagImageLoader& m_flags;
    tgui::GeoIpDb m_geoip;
    QNetworkAccessManager m_nam;
    bool m_geoIpDownloading = false;
};
