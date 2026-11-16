#pragma once

#include <QDialog>
#include <QString>

// W10: диалог «О программе»: шапка с иконкой и версией, описание,
// блок поддержки проекта (QR + ссылка) и раскрывающийся текст лицензии.
// Тексты — чистые статические строки (тестируются без GUI).
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
    static QString aboutText();
    static QString licenseText();
};
