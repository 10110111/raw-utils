#include <iostream>
#include <QScreen>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QApplication>
#include "Histogram.hpp"
#include "ImageCanvas.hpp"
#include "ToolsWidget.hpp"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QMainWindow mainWin;

    const auto histDock = new QDockWidget(QObject::tr("Histogram"));
    const auto histogram = new Histogram;
    const auto histHolder = new QWidget;
    const auto histLayout = new QVBoxLayout;
    histHolder->setLayout(histLayout);
    histLayout->addWidget(histogram);
    const auto logCheckBox = new QCheckBox(QObject::tr("logY"));
    histLayout->addWidget(logCheckBox);
    histDock->setWidget(histHolder);
    mainWin.addDockWidget(Qt::RightDockWidgetArea, histDock);
    QObject::connect(logCheckBox, &QCheckBox::toggled, histogram, &Histogram::setLogY);
    histogram->setLogY(logCheckBox->isChecked());

    const auto tools = new ToolsWidget;
    mainWin.addDockWidget(Qt::RightDockWidgetArea, tools);

    const auto canvas = new ImageCanvas(argv[1] ? argv[1] : "", tools, histogram);
    mainWin.setCentralWidget(canvas);
    mainWin.resize(app.primaryScreen()->size()/1.6);
    mainWin.show();
    return app.exec();
}
