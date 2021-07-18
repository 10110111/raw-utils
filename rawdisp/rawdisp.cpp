#include <iostream>
#include <QScreen>
#include <QMainWindow>
#include <QApplication>
#include "ImageCanvas.hpp"
#include "ToolsWidget.hpp"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QMainWindow mainWin;
    const auto tools = new ToolsWidget;
    mainWin.addDockWidget(Qt::RightDockWidgetArea, tools);
    const auto canvas = new ImageCanvas(argv[1] ? argv[1] : "", tools);
    mainWin.setCentralWidget(canvas);
    mainWin.resize(app.primaryScreen()->size()/1.6);
    mainWin.show();
    return app.exec();
}
