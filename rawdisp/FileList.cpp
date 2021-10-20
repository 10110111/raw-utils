#include "FileList.hpp"
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QListWidget>

namespace
{

enum ItemRole
{
    FilePathRole = Qt::UserRole,
};

}

FileList::FileList(QWidget* parent)
    : QDockWidget(tr("Files"), parent)
{
    const auto holder = new QWidget;
    setWidget(holder);
    const auto layout = new QVBoxLayout;
    holder->setLayout(layout);
    list_ = new QListWidget;
    layout->addWidget(list_);

    connect(list_, &QListWidget::itemSelectionChanged, this, &FileList::onItemSelected);
}

void FileList::listFileSiblings(QString const& filename)
{
    list_->clear();
    const QFileInfo info(filename);
    auto dir = info.isDir() ? QDir(filename) : info.absoluteDir();
    dir.setNameFilters(QStringList{} << "*.arw" << "*.srf" << "*.sr2" << "*.crw" << "*.cr2" << "*.kdc"
                                     << "*.dcr" << "*.k25" << "*.raf" << "*.mef" << "*.mos" << "*.mrw"
                                     << "*.nef" << "*.orf" << "*.pef" << "*.ptx" << "*.dng" << "*.x3f"
                                     << "*.raw" << "*.r3d" << "*.3fr" << "*.erf" << "*.srw" << "*.rw2");
    for(const auto& e : dir.entryInfoList(QDir::Files))
    {
        const auto currFileName = e.fileName();
        const auto item = new QListWidgetItem(currFileName);
        item->setData(FilePathRole, e.absoluteFilePath());
        list_->addItem(item);
        if(currFileName==filename)
        {
            QSignalBlocker b(list_);
            list_->setCurrentItem(list_->item(list_->count()-1));
        }
    }
}

void FileList::onItemSelected()
{
    const auto items = list_->selectedItems();
    if(items.isEmpty()) return;
    emit fileSelected(items[0]->data(FilePathRole).toString());
}
