#include "colsetup.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QTreeView>
#include <QVBoxLayout>

int ColumnLayout::persistentColumn() { return 0; } // ColName

ColumnLayout ColumnLayout::defaults(int count)
{
    ColumnLayout d;
    d.order.clear();
    for (int i = 0; i < count; ++i)
        d.order << i;
    d.visible.clear();
    for (int i = 0; i < count; ++i)
        d.visible << (i < 9); // Name..ETA видны по умолчанию (по референсу main.lfm)
    d.widths = {{0, 280}, {1, 72}, {2, 108}, {3, 96}, {4, 58}, {5, 58},
                {6, 82}, {7, 82}, {8, 80}};
    return d;
}

void ColumnLayout::ensureAtLeastOneVisible()
{
    bool any = false;
    for (bool v : visible) {
        if (v) {
            any = true;
            break;
        }
    }
    if (!any)
        visible[persistentColumn()] = true;
}

void ColumnLayout::moveItem(int position, int delta)
{
    const int to = position + delta;
    if (position < 0 || position >= order.size() || to < 0 || to >= order.size())
        return;
    order.swapItemsAt(position, to);
}

ColumnLayout ColumnLayout::fromSettings(int count)
{
    ColumnLayout d = defaults(count);
    QSettings s;
    const QStringList orderStr = s.value("columns/order").toStringList();
    if (orderStr.size() == count) {
        QList<int> o;
        QSet<int> seen;
        bool ok = true;
        for (const QString& token : orderStr) {
            bool conv = false;
            const int c = token.toInt(&conv);
            if (!conv || c < 0 || c >= count || seen.contains(c)) {
                ok = false;
                break;
            }
            seen.insert(c);
            o << c;
        }
        if (ok)
            d.order = o;
    }
    const QStringList visStr = s.value("columns/visible").toStringList();
    if (visStr.size() == count) {
        QList<bool> vis;
        for (const QString& token : visStr)
            vis << (token == "1");
        d.visible = vis;
    }
    d.widths.clear();
    for (int i = 0; i < count; ++i) {
        const int w = s.value(QStringLiteral("columns/width_%1").arg(i)).toInt();
        if (w > 0)
            d.widths[i] = w;
    }
    d.ensureAtLeastOneVisible();
    return d;
}

void ColumnLayout::saveToSettings() const
{
    QSettings s;
    QStringList orderStr;
    for (int c : order)
        orderStr << QString::number(c);
    s.setValue("columns/order", orderStr);
    QStringList visStr;
    for (bool v : visible)
        visStr << (v ? "1" : "0");
    s.setValue("columns/visible", visStr);
    s.remove("columns/widths");
    for (auto it = widths.constBegin(); it != widths.constEnd(); ++it)
        s.setValue(QStringLiteral("columns/width_%1").arg(it.key()), it.value());
    s.sync();
}

void ColumnLayout::applyToView(QTreeView* view) const
{
    if (!view)
        return;
    auto* header = view->header();
    if (!header)
        return;
    header->setSectionResizeMode(QHeaderView::Interactive);
    const int n = order.size();
    for (int visual = 0; visual < n; ++visual) {
        const int logical = order.at(visual);
        const int cur = header->visualIndex(logical);
        if (cur != visual)
            header->moveSection(cur, visual);
    }
    for (int i = 0; i < n; ++i) {
        view->setColumnHidden(i, !visible.at(i));
        if (widths.contains(i))
            view->setColumnWidth(i, widths.value(i));
    }
}
ColSetupDialog::ColSetupDialog(const ColumnLayout& layout, const QStringList& columnTitles,
                               QWidget* parent)
    : QDialog(parent)
    , m_initial(layout)
    , m_titles(columnTitles)
{
    setWindowTitle(tr("Columns"));
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_up = new QPushButton(QStringLiteral("↑"), this);
    m_up->setToolTip(tr("Move up"));
    m_down = new QPushButton(QStringLiteral("↓"), this);
    m_down->setToolTip(tr("Move down"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* listLayout = new QVBoxLayout;
    listLayout->addWidget(new QLabel(tr("Visible columns and their order:"), this));
    listLayout->addWidget(m_list, 1);
    auto* side = new QVBoxLayout;
    side->addWidget(m_up);
    side->addWidget(m_down);
    side->addStretch(1);
    auto* root = new QHBoxLayout(this);
    root->addLayout(listLayout, 1);
    root->addLayout(side);
    root->addWidget(buttons);

    for (int pos = 0; pos < layout.order.size(); ++pos) {
        const int col = layout.order.at(pos);
        auto* it = new QListWidgetItem(m_titles.value(col, QString::number(col)));
        it->setData(Qt::UserRole, col);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(layout.visible.at(col) ? Qt::Checked : Qt::Unchecked);
        m_list->addItem(it);
    }
    m_list->setCurrentRow(0);

    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (it->data(Qt::UserRole).toInt() == ColumnLayout::persistentColumn())
            it->setCheckState(Qt::Checked); // Name — всегда видима
    });
    connect(m_list, &QListWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(m_up, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_down, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        bool any = false;
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->checkState() == Qt::Checked) {
                any = true;
                break;
            }
        if (!any) {
            QMessageBox::warning(this, tr("Columns"), tr("At least one column must be visible."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateButtons();
}

ColumnLayout ColSetupDialog::layout() const
{
    ColumnLayout d = m_initial; // сохраняем ширины
    const int n = m_list->count();
    d.order.clear();
    d.visible.assign(n, false);
    for (int i = 0; i < n; ++i) {
        const int col = m_list->item(i)->data(Qt::UserRole).toInt();
        d.order << col;
        if (m_list->item(i)->checkState() == Qt::Checked)
            d.visible[col] = true;
    }
    d.ensureAtLeastOneVisible();
    return d;
}

void ColSetupDialog::moveSelected(int delta)
{
    const int pos = m_list->currentRow();
    const int to = pos + delta;
    if (pos < 0 || to < 0 || to >= m_list->count())
        return;
    QListWidgetItem* it = m_list->takeItem(pos);
    m_list->insertItem(to, it);
    m_list->setCurrentRow(to);
    updateButtons();
}

void ColSetupDialog::updateButtons()
{
    const int pos = m_list->currentRow();
    m_up->setEnabled(pos > 0);
    m_down->setEnabled(pos >= 0 && pos < m_list->count() - 1);
}