#pragma once

#include <QDialog>

class QLineEdit;
class QCheckBox;
class QLabel;

// Диалог добавления торрента по ссылке/магниту (образец — addlink.pas):
// поле для ссылки, валидация «пусто → ошибка» и OK/Cancel.
class AddLinkDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddLinkDialog(const QString& initialLink = QString(), QWidget* parent = nullptr);

    QString link() const;          // обрезанный текст поля ссылки
    bool startWhenAdded() const;   // запускать при добавлении

private slots:
    void onOk();

private:
    QLineEdit* m_link;
    QCheckBox* m_start;
    QLabel* m_status;
};