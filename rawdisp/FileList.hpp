#pragma once

#include <memory>
#include <QString>
#include <QDockWidget>
#include <QFileSystemWatcher>

class QListWidget;
class FileList : public QDockWidget
{
    Q_OBJECT
public:
    FileList(QWidget* parent = nullptr);
    void listFileSiblings(QString const& filename);
    void selectNextFile();
    void selectPrevFile();
    void selectFirstFile();
    void selectLastFile();
    QString currentFileName() const;
signals:
    void fileSelected(QString const& filename);
private:
    void listFileSiblings(QString const& filename, bool forceReload);
    void onItemSelected();
    int currentItemRow() const;
private:
    QListWidget* list_ = nullptr;
    QString dir_;
    std::unique_ptr<QFileSystemWatcher> watcher_;
};
