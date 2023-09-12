#include "MainWindow.hpp"
#include <QScreen>
#include <QSpinBox>
#include <QKeyEvent>
#include <QFileInfo>
#include <QCheckBox>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QDockWidget>
#include <QApplication>
#include <QDoubleSpinBox>
#include "RawHistogram.hpp"
#include "EXIFDisplay.hpp"
#include "ImageCanvas.hpp"
#include "ToolsWidget.hpp"
#include "FileList.hpp"

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

    const auto fileList = new FileList;
    addDockWidget(Qt::LeftDockWidgetArea, fileList);
    docks.push_back(fileList);

    canvas = new ImageCanvas(tools, rawHistogram);
    const auto cursorLabel = new QLabel("");
    statusBar()->addPermanentWidget(cursorLabel);
    const auto zoomLabel = new QLabel("Zoom: N/A");
    statusBar()->addPermanentWidget(zoomLabel);
    connect(canvas, &ImageCanvas::zoomChanged, [zoomLabel](const double zoom)
            { zoomLabel->setText(QString(u8"Zoom: %1%").arg(zoom*100,0,'g',3)); });
    connect(canvas, &ImageCanvas::warning, [this](QString const& w){ statusBar()->showMessage(w); });
    connect(canvas, &ImageCanvas::loadingFile, [this]{ setCursor(Qt::WaitCursor); });
    connect(canvas, &ImageCanvas::fileLoadingFinished, [this]{ unsetCursor(); });
    connect(canvas, &ImageCanvas::loadingFile, exif, &EXIFDisplay::loadFile);
    connect(canvas, &ImageCanvas::loadingFile, fileList, qOverload<QString const&>(&FileList::listFileSiblings));
    connect(canvas, &ImageCanvas::loadingFile, [this](QString const& file)
            { setWindowTitle(formatWindowTitle(file)); });
    connect(canvas, &ImageCanvas::fullScreenToggleRequested, this, &MainWindow::toggleFullScreen);
    connect(canvas, &ImageCanvas::nextFileRequested, fileList, &FileList::selectNextFile);
    connect(canvas, &ImageCanvas::prevFileRequested, fileList, &FileList::selectPrevFile);
    connect(canvas, &ImageCanvas::firstFileRequested, fileList, &FileList::selectFirstFile);
    connect(canvas, &ImageCanvas::lastFileRequested, fileList, &FileList::selectLastFile);
    connect(canvas, &ImageCanvas::previewLoaded, this, [tools]{ tools->enablePreview(); });
    connect(canvas, &ImageCanvas::previewNotAvailable, this, [tools]{ tools->disablePreview(); });
    connect(canvas, &ImageCanvas::cursorPositionUpdated, this,
            [cursorLabel](const double x, const double y){ cursorLabel->setText(QString("x,y:(%1, %2)").arg(x,0,'f',1).arg(y,0,'f',1)); });
    connect(canvas, &ImageCanvas::cursorLeft, this, [cursorLabel]{ cursorLabel->setText(""); });
    connect(fileList, &FileList::fileSelected, canvas, &ImageCanvas::openFile);
    setCentralWidget(canvas);
    resize(qApp->primaryScreen()->size()/1.6);

    canvas->setFocus(Qt::OtherFocusReason);
    if(!filename.isEmpty())
        canvas->openFile(filename);

    qApp->installEventFilter(this);
}

void MainWindow::toggleFullScreen()
{
    if(statusBar()->isVisible())
    {
        wasMaximizedBeforeFullScreen = isMaximized();
        statusBar()->hide();
        showFullScreen();
        for(const auto dock : docks)
            dock->hide();
    }
    else
    {
        statusBar()->show();
        if(wasMaximizedBeforeFullScreen)
        {
            showNormal(); // undo full screen first: direct switch to maximized doesn't maximize
            showMaximized();
        }
        else
        {
            showNormal();
        }
        for(const auto dock : docks)
            dock->show();
    }
}

bool MainWindow::eventFilter(QObject*const obj, QEvent*const event)
{
    if(obj == canvas)
        return QObject::eventFilter(obj, event);
    if(qobject_cast<QWidget*>(obj))
        return QObject::eventFilter(obj, event);
    if(event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(obj, event);

    const auto ke = static_cast<QKeyEvent*>(event);
    if(ke->modifiers() & (Qt::ControlModifier|Qt::ShiftModifier|Qt::AltModifier))
        return QObject::eventFilter(obj, event);

    const auto focusedWidget = qApp->focusWidget();
    const bool focusedWidgetIsSpinBox = qobject_cast<QSpinBox*>(focusedWidget) || qobject_cast<QDoubleSpinBox*>(focusedWidget);

    const auto key = ke->key();
    const bool isAlpha = Qt::Key_A <= key && key <= Qt::Key_Z;
    const bool isFunc  = Qt::Key_F1 <= key && key <= Qt::Key_F35;
    const bool isNavig = key==Qt::Key_PageUp || key==Qt::Key_PageDown || key==Qt::Key_Home || key==Qt::Key_End;
    if(isAlpha || isFunc || (isNavig && !focusedWidgetIsSpinBox))
    {
        qApp->sendEvent(canvas, event);
        return true;
    }

    return QObject::eventFilter(obj, event);
}
