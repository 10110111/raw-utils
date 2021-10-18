#include "MainWindow.hpp"
#include <QScreen>
#include <QFileInfo>
#include <QCheckBox>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QDockWidget>
#include <QApplication>
#include "RawHistogram.hpp"
#include "EXIFDisplay.hpp"
#include "ImageCanvas.hpp"
#include "ToolsWidget.hpp"

namespace
{

QString formatWindowTitle(QString const& filename)
{
    return QObject::tr("%1 - rawdisp").arg(QFileInfo(filename).fileName());
}

}

MainWindow::MainWindow(QString const& filename)
{
    statusBar();

    const auto rawHistDock = new QDockWidget(tr("Raw Histogram"));
    const auto rawHistogram = new RawHistogram;
    const auto rawHistHolder = new QWidget;
    const auto rawHistLayout = new QVBoxLayout;
    rawHistHolder->setLayout(rawHistLayout);
    rawHistLayout->addWidget(rawHistogram);
    const auto logCheckBox = new QCheckBox(tr("logY"));
    logCheckBox->setChecked(rawHistogram->logY());
    rawHistLayout->addWidget(logCheckBox);
    rawHistDock->setWidget(rawHistHolder);
    addDockWidget(Qt::RightDockWidgetArea, rawHistDock);
    docks.push_back(rawHistDock);
    connect(logCheckBox, &QCheckBox::toggled, rawHistogram, &RawHistogram::setLogY);

    const auto tools = new ToolsWidget;
    addDockWidget(Qt::RightDockWidgetArea, tools);
    docks.push_back(tools);

    const auto exif = new EXIFDisplay;
    addDockWidget(Qt::RightDockWidgetArea, exif);
    docks.push_back(exif);

    const auto canvas = new ImageCanvas(tools, rawHistogram);
    const auto zoomLabel = new QLabel("Zoom: N/A");
    statusBar()->addPermanentWidget(zoomLabel);
    connect(canvas, &ImageCanvas::zoomChanged, [zoomLabel](const double zoom)
            { zoomLabel->setText(QString(u8"Zoom: %1%").arg(zoom*100,0,'g',3)); });
    connect(canvas, &ImageCanvas::warning, [this](QString const& w){ statusBar()->showMessage(w); });
    connect(canvas, &ImageCanvas::loadingFile, exif, &EXIFDisplay::loadFile);
    connect(canvas, &ImageCanvas::loadingFile, [this](QString const& file)
            { setWindowTitle(formatWindowTitle(file)); });
    connect(canvas, &ImageCanvas::fullScreenToggleRequested, this, &MainWindow::toggleFullScreen);
    setCentralWidget(canvas);
    resize(qApp->primaryScreen()->size()/1.6);

    canvas->setFocus(Qt::OtherFocusReason);
    if(!filename.isEmpty())
        canvas->openFile(filename);
}

void MainWindow::toggleFullScreen()
{
    if(statusBar()->isVisible())
    {
        statusBar()->hide();
        showFullScreen();
        for(const auto dock : docks)
            dock->hide();
    }
    else
    {
        statusBar()->show();
        showNormal();
        for(const auto dock : docks)
            dock->show();
    }
}
