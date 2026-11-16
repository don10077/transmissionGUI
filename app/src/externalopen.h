#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QStringList>
#include <functional>

/** Нормализует путь к локальному .torrent или magnet из аргумента ОС. */
QString externalTorrentInput(const QString& input);

/** Передаёт запросы открытия первому экземпляру приложения текущего пользователя. */
class ExternalOpen {
public:
    enum class Result { Listening, AlreadyRunning, Unavailable };
    using Handler = std::function<void(const QStringList&)>;

    explicit ExternalOpen(const QString& dataDirectory);
    Result listen(Handler handler);
    bool forward(const QStringList& inputs) const;

private:
    QString m_directory;
    QString m_name;
    QLockFile m_lock;
    QLocalServer m_server;
};
