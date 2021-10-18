#include <QApplication>
#include "MainWindow.hpp"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    MainWindow mainWin(argv[1] ? argv[1] : "");
    mainWin.show();
    return app.exec();
}
