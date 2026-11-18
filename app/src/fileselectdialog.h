#pragma once

#include <QDialog>
#include <QPointer>
#include <QVariantMap>

#include "daemonprofile.h"

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

// Выбор файлов торрента (want/skip) перед запуском.
// Дерево папок как в TransGUI: трёхпозиционные чекбоксы с каскадом,
// внизу — «Start torrent» и «Sequential download».
class FileSelectDialog : public QDialog {
    Q_OBJECT
public:
    FileSelectDialog(const tgui::TorrentDetails& details, bool start, int rpcVersion,
                     tgui::RpcClient* rpc = nullptr, QWidget* parent = nullptr,
                     bool useDestinationTemplates = true, const QString& profile = {});

    void offerMetainfoCopy(const QByteArray& metainfo);
    void offerReplacement(const QVector<tgui::Torrent>& torrents, int selectedId);
    QString replacementHash() const;
    QVector<int> wanted() const;
    QVector<int> unwanted() const;
    bool startTorrent() const;
    bool sequentialDownload() const;
    tgui::RpcClient::AddTorrent options() const;
    QString saveAs() const;
    /** Введённое отображаемое имя (обрезанное, до 255 символов). */
    QString displayName() const;
    /** Пользователь вручную менял поле отображаемого имени. */
    bool displayNameEdited() const { return m_displayNameEdited; }

    QTreeWidget* tree() const { return m_tree; }

private:
    void accept() override;
    void browseDestination();
    void setAll(Qt::CheckState state);
    void setChildrenChecked(QTreeWidgetItem* item, Qt::CheckState state);
    void updateAncestors(QTreeWidgetItem* item);
    void applyTemplate(const QString& text);
    void updateSummary();

    QVBoxLayout* m_body;
    QWidget* m_contents = nullptr;
    QPushButton* m_browse;
    QTreeWidget* m_tree;
    QHBoxLayout* m_footer;
    QPushButton* m_ok;
    QLabel* m_summary;
    QComboBox* m_destination;
    QComboBox* m_label = nullptr;
    QLineEdit* m_saveAs;
    QLineEdit* m_displayName = nullptr;
    QLineEdit* m_template;
    QSpinBox* m_peerLimit;
    QCheckBox* m_start;
    QCheckBox* m_sequential;
    QCheckBox* m_skip = nullptr;
    QCheckBox* m_replace = nullptr;
    QComboBox* m_replacement = nullptr;
    ProfilePreferences m_preferences;
    QPointer<tgui::RpcClient> m_rpc;
    QVariantMap m_history;
    bool m_updating = false;
    bool m_displayNameEdited = false;
};
