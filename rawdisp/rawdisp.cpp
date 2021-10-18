#include <iostream>
#include <QScreen>
#include <QFileInfo>
#include <QCheckBox>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QApplication>
#include "Histogram.hpp"
#include "EXIFDisplay.hpp"
#include "ImageCanvas.hpp"
#include "ToolsWidget.hpp"

namespace
{

QString formatWindowTitle(QString const& filename)
{
    return QObject::tr("%1 - rawdisp").arg(QFileInfo(filename).fileName());
}

void toggleFullScreen(QMainWindow& mainWin, std::vector<QWidget*> const& docks)
{
    if(mainWin.statusBar()->isVisible())
    {
        mainWin.statusBar()->hide();
        mainWin.showFullScreen();
        for(const auto dock : docks)
            dock->hide();
    }
    else
    {
        mainWin.statusBar()->show();
        mainWin.showNormal();
        for(const auto dock : docks)
            dock->show();
    }
}

}

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
    logCheckBox->setChecked(histogram->logY());
    QObject::connect(logCheckBox, &QCheckBox::toggled, histogram, &Histogram::setLogY);

    const auto tools = new ToolsWidget;
    mainWin.addDockWidget(Qt::RightDockWidgetArea, tools);

    const auto exif = new EXIFDisplay;
    mainWin.addDockWidget(Qt::RightDockWidgetArea, exif);

    const auto canvas = new ImageCanvas(tools, histogram);
    const auto zoomLabel = new QLabel("Zoom: N/A");
    mainWin.statusBar()->addPermanentWidget(zoomLabel);
    QObject::connect(canvas, &ImageCanvas::zoomChanged, [zoomLabel](const double zoom)
                     { zoomLabel->setText(QString(u8"Zoom: %1%").arg(zoom*100,0,'g',3)); });
    QObject::connect(canvas, &ImageCanvas::warning, [&mainWin](QString const& w){ mainWin.statusBar()->showMessage(w); });
    QObject::connect(canvas, &ImageCanvas::loadingFile, exif, &EXIFDisplay::loadFile);
    QObject::connect(canvas, &ImageCanvas::loadingFile, [&mainWin](QString const& file)
                     { mainWin.setWindowTitle(formatWindowTitle(file)); });
    QObject::connect(canvas, &ImageCanvas::fullScreenToggleRequested, exif,
                     [&mainWin, histDock,tools,exif]{toggleFullScreen(mainWin, {histDock,tools,exif});});
    mainWin.setCentralWidget(canvas);
    mainWin.resize(app.primaryScreen()->size()/1.6);
    mainWin.show();

    canvas->setFocus(Qt::OtherFocusReason);
    if(argv[1])
        canvas->openFile(argv[1]);

    return app.exec();
}
