#pragma once

#include <QDialog>

#include "tgui/models.h"

// Диалог «Свойства» торрента (только чтение): сводка из torrent-get.
class PropsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PropsDialog(const tgui::Torrent& torrent, QWidget* parent = nullptr);
};