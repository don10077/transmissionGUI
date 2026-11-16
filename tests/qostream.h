#pragma once

#include <QByteArray>
#include <QString>
#include <ostream>

// gtest-вывод типов Qt (EXPECT_TRUE(...) << str).
inline std::ostream& operator<<(std::ostream& os, const QString& s)
{
    return os << s.toStdString();
}
inline std::ostream& operator<<(std::ostream& os, const QByteArray& b)
{
    return os << b.toStdString();
}