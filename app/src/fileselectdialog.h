#pragma once

#include <QDialog>
#include <QVariantMap>

#include "tgui/rpcclient.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

// Выбор файлов торрента (want/skip) перед запуском.
// Дерево папок как в TransGUI: трёхпозиционные чекбоксы с каскадом,
// внизу — «Start torrent» и «Sequential download».
class FileSelectDialog : public QDialog {
    Q_OBJECT
public:
    FileSelectDialog(const tgui::TorrentDetails& details, bool start, int rpcVersion,
                     tgui::RpcClient* rpc = nullptr, QWidget* parent = nullptr);

    void offerMetainfoCopy(const tgui::RpcClient::AddTorrent& source);
    QVector<int> wanted() const;
    QVector<int> unwanted() const;
    bool startTorrent() const;
    bool sequentialDownload() const;
    tgui::RpcClient::AddTorrent options() const;
    QString saveAs() const;

    QTreeWidget* tree() const { return m_tree; }

private:
    void accept() override;
    void browseDestination();
    void setAll(Qt::CheckState state);
    void setChildrenChecked(QTreeWidgetItem* item, Qt::CheckState state);
    void updateAncestors(QTreeWidgetItem* item);
    void updateSummary();
    // Суммарный размер отмеченных файлов (по листьям дерева).
    double selectedSize() const;

    QTreeWidget* m_tree;
    QDialogButtonBox* m_buttons;
    QLabel* m_summary;
    QComboBox* m_destination;
    QComboBox* m_label = nullptr;
    QLineEdit* m_saveAs;
    QLineEdit* m_template;
    QSpinBox* m_peerLimit;
    QCheckBox* m_start;
    QCheckBox* m_sequential;
    QString m_historyKey;
    QVariantMap m_history;
    double m_totalSize = 0;
    bool m_updating = false;
};
