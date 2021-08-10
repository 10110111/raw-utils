#include <iostream>
#include <QScreen>
#include <QCheckBox>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QApplication>
#include "Histogram.hpp"
#include "EXIFDisplay.hpp"
#include "ImageCanvas.hpp"
#include "ToolsWidget.hpp"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QMainWindow mainWin;
    mainWin.statusBar();

    const auto histDock = new QDockWidget(QObject::tr("Raw Histogram"));
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

    const auto exif = new EXIFDisplay;
    mainWin.addDockWidget(Qt::RightDockWidgetArea, exif);

    const auto canvas = new ImageCanvas(argv[1] ? argv[1] : "", tools, histogram);
    QObject::connect(canvas, &ImageCanvas::warning, [&mainWin](QString const& w){ mainWin.statusBar()->showMessage(w); });
    QObject::connect(canvas, &ImageCanvas::loadingFile, exif, &EXIFDisplay::loadFile);
    mainWin.setCentralWidget(canvas);
    mainWin.resize(app.primaryScreen()->size()/1.6);
    mainWin.show();
    return app.exec();
}
