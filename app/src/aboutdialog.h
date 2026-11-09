#pragma once

#include <QDialog>
#include <QString>

// W10: диалог «О программе» (вкладки About + License). Образец: transgui/about.pas.
// Тексты — чистые статические строки (тестируются без GUI).
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
    static QString aboutText();
    static QString licenseText();
};