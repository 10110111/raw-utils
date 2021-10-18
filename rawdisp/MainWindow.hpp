#pragma once

#include <QMainWindow>

class MainWindow : public QMainWindow
{
public:
    MainWindow(QString const& filename);
private:
    void toggleFullScreen();
private:
    std::vector<QDockWidget*> docks;
};
