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
    listFileSiblings(filename, false);
}

void FileList::listFileSiblings(QString const& filename, const bool forceReload)
{
    const QFileInfo info(filename);
    auto dir = info.isDir() ? QDir(filename) : info.absoluteDir();

    if(!forceReload && dir.canonicalPath() == dir_)
        return;
    dir_ = dir.canonicalPath();
    watcher_.reset(new QFileSystemWatcher);
    watcher_->addPath(dir_);
    connect(watcher_.get(), &QFileSystemWatcher::directoryChanged, this,
            [this,filename]
            {
                const auto currFileName = currentFileName();
                listFileSiblings(currFileName.isEmpty() ? filename : currFileName, true);
            });

    const auto fileNameToSelect = QFileInfo(filename).fileName();
    list_->clear();
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
        if(currFileName==fileNameToSelect)
        {
            QSignalBlocker b(list_);
            list_->setCurrentItem(list_->item(list_->count()-1));
        }
    }
}

void FileList::onItemSelected()
{
    const auto filename = currentFileName();
    if(!filename.isEmpty())
        emit fileSelected(currentFileName());
}

void FileList::selectNextFile()
{
    const int currRow = currentItemRow();
    if(currRow < 0)
    {
        if(list_->count() == 0)
            return;
        list_->setCurrentItem(list_->item(0));
    }
    if(currRow+1 < list_->count())
        list_->setCurrentItem(list_->item(currRow+1));
}

void FileList::selectPrevFile()
{
    const int currRow = currentItemRow();
    if(currRow < 0)
    {
        if(list_->count() == 0)
            return;
        list_->setCurrentItem(list_->item(0));
    }
    if(currRow-1 >= 0)
        list_->setCurrentItem(list_->item(currRow-1));
}

void FileList::selectFirstFile()
{
    if(list_->count() == 0)
        return;
    list_->setCurrentItem(list_->item(0));
}

void FileList::selectLastFile()
{
    if(list_->count() == 0)
        return;
    list_->setCurrentItem(list_->item(list_->count()-1));
}

int FileList::currentItemRow() const
{
    const auto currItem = list_->currentItem();
    for(int row = 0; row < list_->count(); ++row)
        if(list_->item(row) == currItem)
            return row;
    return -1;
}

QString FileList::currentFileName() const
{
    const auto items = list_->selectedItems();
    if(items.isEmpty()) return {};
    return items[0]->data(FilePathRole).toString();
}
