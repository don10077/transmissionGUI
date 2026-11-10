#pragma once

#include <QDialog>

#include "tgui/rpcclient.h"

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QRadioButton;
class QLabel;

// Диалог добавления торрента (файл / magnet / URL).
class AddTorrentDialog : public QDialog {
    Q_OBJECT
public:
    AddTorrentDialog(const QString& initialFile, const QString& initialDir,
                     int rpcVersion, bool defaultStart, tgui::RpcClient* rpc,
                     QWidget* parent = nullptr);

    tgui::RpcClient::AddTorrent params() const;
    bool saveEnabled() const;
    QString savePath() const;

private slots:
    void onBrowseFile();
    void onBrowseDir();
    void updateFreeSpace();
    void updateSequentialDefault();

private:
    QRadioButton* m_rbFile;
    QRadioButton* m_rbUrl;
    QLineEdit* m_file;
    QLineEdit* m_url;
    QLineEdit* m_dir;
    QLabel* m_freeSpace;
    QSpinBox* m_peerLimit;
    QCheckBox* m_save;
    QLineEdit* m_savePath;
    QCheckBox* m_start;
    QCheckBox* m_sequential;
    tgui::RpcClient* m_rpc = nullptr;
    int m_rpcVersion = 0;
};
