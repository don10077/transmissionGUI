#include "fileselectdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "tgui/util.h"

FileSelectDialog::FileSelectDialog(const QVector<tgui::FileStat>& files, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Выбор файлов"));
    resize(560, 400);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(1);
    m_tree->setHeaderLabel(tr("Файл (размер)"));
    for (const auto& f : files) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, QStringLiteral("%1  (%2)")
                             .arg(f.name)
                             .arg(QString::fromStdString(tgui::formatSize(f.size))));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, f.index);
    }

    auto* btnAll = new QPushButton(tr("Выделить все"), this);
    auto* btnNone = new QPushButton(tr("Снять все"), this);
    connect(btnAll, &QPushButton::clicked, this, [this] { setAll(Qt::Checked); });
    connect(btnNone, &QPushButton::clicked, this, [this] { setAll(Qt::Unchecked); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* bottom = new QHBoxLayout();
    bottom->addWidget(btnAll);
    bottom->addWidget(btnNone);
    bottom->addStretch(1);
    bottom->addWidget(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Не отмеченные файлы не будут скачиваться."), this));
    layout->addWidget(m_tree, 1);
    layout->addLayout(bottom);
}

QVector<int> FileSelectDialog::wanted() const
{
    QVector<int> w;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            w.push_back(item->data(0, Qt::UserRole).toInt());
    }
    return w;
}

QVector<int> FileSelectDialog::unwanted() const
{
    QVector<int> u;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked)
            u.push_back(item->data(0, Qt::UserRole).toInt());
    }
    return u;
}

void FileSelectDialog::setAll(Qt::CheckState state)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        m_tree->topLevelItem(i)->setCheckState(0, state);
}
