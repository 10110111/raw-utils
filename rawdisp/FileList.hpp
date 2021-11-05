#pragma once

#include <QString>
#include <QDockWidget>

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
signals:
    void fileSelected(QString const& filename);
private:
    void onItemSelected();
    int currentItemRow() const;
private:
    QListWidget* list_ = nullptr;
    QString dir_;
};
