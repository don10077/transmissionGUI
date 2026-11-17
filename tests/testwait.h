#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <functional>

// TST-02: детерминированное ожидание условия вместо «выхода по фиксированному
// таймеру» (singleShot(N, &loop, &QEventLoop::quit)): события продолжают
// обрабатываться, пока предикат не станет истинным либо не истечёт таймаут.
// Возвращает истинность предиката (false — таймаут, тест должен провалиться
// явным ASSERT/EXPECT, а не молча пройти).
inline bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return predicate();
}