#pragma once

#include <QMainWindow>

class ImageCanvas;
class MainWindow : public QMainWindow
{
public:
    MainWindow(QString const& filename);
private:
    void toggleFullScreen();
    bool eventFilter(QObject* obj, QEvent* event) override;
private:
    std::vector<QDockWidget*> docks;
    ImageCanvas* canvas=nullptr;
    bool wasMaximizedBeforeFullScreen = false;
};
