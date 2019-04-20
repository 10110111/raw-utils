#include "MainWindow.h"

#include "FramesModel.h"
#include "FrameView.h"

#include <libraw/libraw.h>

#include <QDialogButtonBox>
#include <QProgressBar>
#include <QTextEdit>
#include <QMessageBox>
#include <QFileDialog>
#include <QWheelEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QLabel>

#if defined __GNUG__ && __GNUC__<8
#include <experimental/filesystem>
namespace filesystem=std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem=std::filesystem;
#endif
#include <algorithm>
#include <sstream>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <vector>
#include <limits>
#include <cmath>
#include <map>

#include "util.h"

static constexpr auto sqr=[](auto x){return x*x;};
using std::isnan;
using Time=MainWindow::Time;

namespace
{

Time makeTime(int year, int month, int day, int hour, int minute, int second)
{
    return QDateTime(QDate(year,month,day),QTime(hour,minute,second)).toSecsSinceEpoch();
}
QString timeToString(Time time)
{
    return QDateTime::fromSecsSinceEpoch(time).toString("yyyy-MM-dd HH:mm:ss");
}
Time toTime(QVariant timeVar)
{
    return timeVar.toULongLong();
}

const auto unknownStr=QObject::tr("unknown");
QString formatAperture(double aperture)
{
    return std::isnan(aperture) ? unknownStr : QString("f/%1").arg(aperture,0,'g',2);
}
QString toStringOrUnknown(double num)
{
    return std::isnan(num) ? unknownStr : QString::number(num);
}
QString formatISO(double iso)
{
    return toStringOrUnknown(iso);
}

} // anonymous namespace

bool MainWindow::ExposureMode::sameValue(double x, double y)
{
    return (!isnan(x) && !isnan(y) && x==y) || (isnan(x) && isnan(y));
}
bool MainWindow::ExposureMode::operator==(ExposureMode const& other) const
{
    return sameValue(aperture,other.aperture) &&
           sameValue(shutterTime,other.shutterTime) &&
           sameValue(iso,other.iso);
}
bool MainWindow::ExposureMode::operator<(ExposureMode const& rhs) const
{
    return (!sameValue(aperture,rhs.aperture) && aperture<rhs.aperture) ||
           (!sameValue(iso,rhs.iso) && iso<rhs.iso) ||
           (!sameValue(shutterTime,rhs.shutterTime) && shutterTime<rhs.shutterTime);
}
QString MainWindow::ExposureMode::toString() const
{
    if(!initialized) return "(invalid)";

    return QString("Shutter %1, ISO %2, aperture %3").arg(formattedShutterTime)
                                                     .arg(formatISO(iso))
                                                     .arg(formatAperture(aperture));
}

void MainWindow::exifHandler(void* context, int tag, [[maybe_unused]] int type, int count, unsigned byteOrder, void* ifp)
{
    auto& self=*static_cast<MainWindow*>(context);
    auto& exifHandlerError=self.exifHandlerError;
    auto& lastCreatedFile=self.lastCreatedFile;

    std::string tagName="(?)";
    unsigned byteCount=0;
    enum TagValue
    {
        ModifyDate=0x0132,
        ExposureTime=0x829a,
        ISO=0x8827,
        ApertureValue=0x9202,
    };
    switch(static_cast<TagValue>(tag))
    {
    case ExposureTime:
        tagName="exposure-time";
        byteCount=8;
        break;
    case ISO:
        tagName="ISO";
        byteCount=2;
        break;
    case ApertureValue:
        tagName="aperture";
        byteCount=8;
        break;
    case ModifyDate:
        tagName="modification-date";
        byteCount=count;
        break;
    default:
        return;
    }

    if(count!=1 && tag!=ModifyDate)
    {
        std::ostringstream os;
        os << "Unexpected count " << count << " for EXIF data item 0x" << std::hex << tag
           << std::dec << " " << tagName;
        exifHandlerError=os.str();
        return;
    }

    const auto stream=static_cast<LibRaw_abstract_datastream*>(ifp);

    if(tag==ModifyDate)
    {
        std::string date(byteCount,'\0');
        try
        {
            if(stream->read(date.data(),byteCount,1)!=1)
                throw std::runtime_error("Failed to read stream for modification date tag in EXIF handler");
            if(date.size() && date.back()=='\0')
                date.pop_back();
            if(date.length()!=4+1+2+1+2+ 1 +2+1+2+1+2)
                throw std::runtime_error("Unexpected format of modification date tag: length="+std::to_string(date.length()));
            std::istringstream iss(date);
            unsigned year, month, day, hour, minute, second;
            if(!(iss >> year)) throw std::runtime_error("Bad year in modification date tag: "+date);
            if(iss.get()!=':') throw std::runtime_error("Bad separator after year in modification date tag: "+date);
            if(!(iss >> month)) throw std::runtime_error("Bad month in modification date tag: "+date);
            if(iss.get()!=':') throw std::runtime_error("Bad separator after month in modification date tag: "+date);
            if(!(iss >> day)) throw std::runtime_error("Bad day in modification date tag: "+date);
            if(iss.get()!=' ') throw std::runtime_error("Bad separator after day in modification date tag: "+date);
            if(!(iss >> hour)) throw std::runtime_error("Bad hour in modification date tag: "+date);
            if(iss.get()!=':') throw std::runtime_error("Bad separator after hour in modification date tag: "+date);
            if(!(iss >> minute)) throw std::runtime_error("Bad minute in modification date tag: "+date);
            if(iss.get()!=':') throw std::runtime_error("Bad separator after minute in modification date tag: "+date);
            if(!(iss >> second)) throw std::runtime_error("Bad second in modification date tag: "+date);
            if(iss.get() && !iss.eof()) throw std::runtime_error("Extra characters after seconds in modification date tag: "+date);

            const auto shotTime=makeTime(year,month,day,hour,minute,second);
            const auto it=self.filesMap.emplace(std::make_pair(shotTime,Frame{shotTime,self.currentFileBeingOpened}));
            lastCreatedFile=&it.first->second;
        }
        catch(std::runtime_error const& ex)
        {
            exifHandlerError=ex.what();
            return;
        }
        return;
    }

    std::uint64_t data=0;

    if(byteOrder!=0x4949)
    {
        exifHandlerError="Big endian byte order is not implemented\n";
        return;
    }

    if(stream->read(&data,byteCount,1)!=1)
    {
        exifHandlerError="Failed to read stream in EXIF handler\n";
        return;
    }

    if(!lastCreatedFile)
    {
        exifHandlerError="Unexpected peculiarity of EXIF data: modification date tag seems to not precede "+tagName;
        return;
    }

    switch(static_cast<TagValue>(tag))
    {
    case ExposureTime:
    case ApertureValue:
    {
        const auto numerator  =std::uint32_t(data);
        const auto denominator=std::uint32_t(data>>32);

        const auto value = double(numerator)/denominator;

        if(numerator==0x80000000u)
            break;

        if(tag==ExposureTime)
        {
            lastCreatedFile->shutterTime=value;
            if(denominator==10 || denominator==5)
            {
                lastCreatedFile->shutterTimeString=QString::number(value);
            }
            else
            {
                lastCreatedFile->shutterTimeString=QString::number(numerator);
                if(denominator!=1)
                    lastCreatedFile->shutterTimeString += QString("/%1").arg(denominator);
            }
            lastCreatedFile->shutterTimeString+=" s";
        }
        else
        {
            lastCreatedFile->aperture=std::exp2(value/2.);
        }

        break;
    }
    case ISO:
    {
        lastCreatedFile->iso=std::uint16_t(data);
        break;
    }
    case ModifyDate:
        assert(!"Shouldn't reach here!");
        break;
    }
}

MainWindow::MainWindow(std::string const& dirToOpen)
{
    ui.setupUi(this);
    ui.abortLoadingBtn->hide();
    ui.abortScriptGenerationBtn->hide();
    connect(ui.abortScriptGenerationBtn,&QPushButton::clicked, this,[this]{renderScriptGenerationAborted=true;});
    connect(ui.generateRenderScriptBtn,&QPushButton::clicked, this,&MainWindow::generateRenderScript);
    connect(ui.abortLoadingBtn,&QPushButton::clicked, this,[this]{fileLoadingAborted=true;});
    connect(ui.action_Open_directory, &QAction::triggered, this, &MainWindow::openDir);
    connect(ui.action_Quit, &QAction::triggered, qApp, &QApplication::quit);
    ui.treeView->setModel(framesModel=new FramesModel(this));
    ui.treeView->setRootIsDecorated(false);
    connect(ui.treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::frameSelectionChanged);
    ui.selectionsWidget->setRootIsDecorated(false);

    setCentralWidget(frameView=new FrameView(this));
    connect(ui.globalBrightnessMultiplier, qOverload<double>(&QDoubleSpinBox::valueChanged),
            frameView, &FrameView::setScale);
    connect(ui.markOverexposuresCB, &QCheckBox::stateChanged, this, [this](int state)
            { frameView->setMarkOverexposure(state==Qt::Checked); });
    connect(ui.divideByOneRB, &QRadioButton::toggled, this, [this](bool checked)
            { if(checked) frameView->setNormalizationMode(FrameView::NormalizationMode::None); });
    connect(ui.divideByAverageRB, &QRadioButton::toggled, this, [this](bool checked)
            { if(checked) frameView->setNormalizationMode(FrameView::NormalizationMode::DivideByAverage); });
    connect(ui.divideByMaxRB, &QRadioButton::toggled, this, [this](bool checked)
            { if(checked) frameView->setNormalizationMode(FrameView::NormalizationMode::DivideByMax); });
    connect(frameView, &FrameView::mouseLeft, this, &MainWindow::onMouseLeftFrameView);
    connect(frameView, &FrameView::mouseMoved, this, &MainWindow::onMouseMoved);
    connect(frameView, &FrameView::wheelScrolled, this, &MainWindow::onWheelScrolled);
    connect(frameView, &FrameView::selectionAdded, this, &MainWindow::onSelectionAdded);
    connect(frameView, &FrameView::selectionsRemoved, this, &MainWindow::onSelectionsRemoved);

    statusBar()->addPermanentWidget(statusProgressBar=new QProgressBar(statusBar()));
    statusProgressBar->hide();
    statusBar()->addPermanentWidget(pixelInfoLabel=new QLabel(this));

    if(!dirToOpen.empty())
        QMetaObject::invokeMethod(this,[this,dirToOpen]{loadFiles(dirToOpen);},Qt::QueuedConnection);
}

void MainWindow::onSelectionAdded(glm::ivec2 pointA, glm::ivec2 pointB)
{
    const auto topLeft=glm::min(pointA,pointB);
    const auto bottomRight=glm::max(pointA,pointB);
    ui.selectionsWidget->addTopLevelItem({new QTreeWidgetItem({
        QString("%1,%2").arg(topLeft.x).arg(topLeft.y),
        QString("%1,%2").arg(bottomRight.x).arg(bottomRight.y)
        })});
}

void MainWindow::onSelectionsRemoved()
{
    ui.selectionsWidget->clear();
}

void MainWindow::generateRenderScript()
{
    ui.generateRenderScriptBtn->hide();
    ui.abortScriptGenerationBtn->show();
    statusProgressBar->show();

    QString scriptSrc;
    scriptSrc+=QString("#!/bin/bash -e\n"
                       "\n"
                       "outdir=/tmp/%1-frames\n"
                       "export PATH=$HOME/myprogs/raw-histogram:$PATH\n"
                       "mkdir \"$outdir\"\n"
                       "i=0\n").arg(dirFileName);
    renderScriptGenerationAborted=false;
    statusProgressBar->setRange(0, frameGroups.size());
    std::size_t groupsProcessed=0;
    const auto cleanupBeforeStopping=[&]
        {
            ui.generateRenderScriptBtn->show();
            ui.abortScriptGenerationBtn->hide();
            statusBar()->clearMessage();
            statusProgressBar->hide();
        };
    for(auto const& group : frameGroups)
    {
        statusProgressBar->setValue(groupsProcessed++);
        struct FrameInfo
        {
            Frame const* frame;
            glm::vec3 maxFromSelectedPixels;
            glm::vec3 averageOfSelectedPixels;
        };
        std::map<double/*exposure*/, FrameInfo> framesByTotalExpo;
        for(const auto frame : group)
        {
            const auto img=readImage(frame->shotTime);
            glm::vec3 maxFromSelectedPixels, averageOfSelectedPixels;
            frameView->gatherSelectedPixelsInfo(img.data.data(), img.width, img.height,
                                                maxFromSelectedPixels, averageOfSelectedPixels);
            framesByTotalExpo.insert({frame->exposure,
                                     {frame, maxFromSelectedPixels, averageOfSelectedPixels}});
        }
        bool lineGenerated=false;
        for(auto it=framesByTotalExpo.rbegin(); it!=framesByTotalExpo.rend(); ++it)
        {
            // FIXME: make overexposure test more reliable. This one will fail
            // if we e.g. use a global amplification factor to reduce the value
            // below 1.
            const auto maxVal=max(it->second.maxFromSelectedPixels);
            if(maxVal<1)
            {
                // OK, this is the frame we want to use
                scriptSrc+=QString("data2bmp -srgb -p $(printf \"$outdir/frame-PERCENT_FOUR_D-\" $i) \"%1\" -s %2; ((++i))\n").arg(it->second.frame->path).arg(1/maxVal).replace("PERCENT_FOUR_D","%04d");
                lineGenerated=true;
                break;
            }
            qApp->processEvents();
            if(renderScriptGenerationAborted)
            {
                cleanupBeforeStopping();
                return;
            }
        }
        if(!lineGenerated)
        {
            statusBar()->clearMessage(); // Don't confuse the user with "read successfully" status
            QMessageBox::warning(this,tr("No images in current group"),
                                 tr("No suitable images were found in current group (first frame at %1).\n"
                                    "Will abort script generation.").arg(timeToString(framesByTotalExpo.begin()->second.frame->shotTime)));
            cleanupBeforeStopping();
            return;
        }
    }
    cleanupBeforeStopping();

    scriptSrc+="ffmpeg -i \"$outdir/frame-%04d-merged-srgb.bmp\" -r 16 video.mp4\n";

    QDialog dialog;
    dialog.setWindowTitle(tr("Render script"));
    const auto layout=new QVBoxLayout(&dialog);
    const auto textWidget=new QTextEdit(&dialog);
    textWidget->setReadOnly(true);
    textWidget->setAcceptRichText(false);
    textWidget->setTabChangesFocus(true);
    textWidget->setLineWrapMode(QTextEdit::NoWrap);
    textWidget->setPlainText(scriptSrc);
    layout->addWidget(textWidget);
    const auto buttonBox=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);
    dialog.exec();
}

void MainWindow::openDir()
{
    const auto dirPath=QFileDialog::getExistingDirectory(this);
    if(dirPath.isNull()) return;
    loadFiles(dirPath.toStdString());
}

auto MainWindow::makePrevExpoModesMap() -> std::map<ExposureMode, PrevExpoMode>
{
    std::map<ExposureMode, PrevExpoMode> prevExpoModesMap;
    const auto findExpoMode=[](auto it, auto end, ExposureMode const& mode)
        {
            while(++it!=end && it->second.exposureMode()!=mode);
            return it;
        };
    for(auto const& expoMode : allExposureModes)
    {
        Time smallestDist=std::numeric_limits<Time>::max();
        ExposureMode prevExpoMode;
        const auto end=filesMap.end();
        for(auto it=findExpoMode(filesMap.begin(),end,expoMode); it!=end; it=findExpoMode(it,end,expoMode))
        {
            const auto prevIt=prev(it);
            const auto& prevFile=prevIt->second;
            const auto& currFile=it->second;

            if(prevFile.exposureMode()==currFile.exposureMode())
                continue;

            const auto currDist=currFile.shotTime - prevFile.shotTime;
            if(currDist < smallestDist)
            {
                smallestDist=currDist;
                prevExpoMode=prevFile.exposureMode();
            }
        }
        if(prevExpoMode.initialized)
            prevExpoModesMap.insert({expoMode, {smallestDist,prevExpoMode}});
    }

    Time largestTimeDist=0;
    ExposureMode const* firstModeAfterPause=nullptr; // the mode with which each bracketing iteration starts
    for(auto const& [mode,prevMode] : prevExpoModesMap)
    {
        if(prevMode.timeDist>largestTimeDist)
        {
            largestTimeDist=prevMode.timeDist;
            firstModeAfterPause=&mode;
        }
    }
    assert(firstModeAfterPause);
    prevExpoModesMap.erase(*firstModeAfterPause);

    return prevExpoModesMap;
}

void MainWindow::groupFiles()
{
    if(filesMap.empty()) return;

    statusBar()->showMessage(tr("Grouping files..."));

    const auto prevExpoModesMap=makePrevExpoModesMap();

    int row=0;
    bool highlighted=false;
    auto prevMode=filesMap.begin()->second.exposureMode();
    for(auto it=next(filesMap.begin()); it!=filesMap.end(); ++it, ++row)
    {
        if(highlighted)
            framesModel->setData(framesModel->index(row,0), QColor(200,200,200), Qt::BackgroundRole);

        bool newBracketingIteration=false;
        auto currMode=it->second.exposureMode();
        if(currMode==prevMode || frameGroups.empty())
        {
            // Only one image was in the whole previous bracketing iteration,
            // or we're just starting.
            newBracketingIteration=true;
        }
        else
        {
            // If all known-previous modes for current exposure mode don't
            // match prevMode, we know that it's a new bracketing iteration.
            while(currMode!=prevMode && prevExpoModesMap.count(currMode))
                currMode=prevExpoModesMap.at(currMode).mode;

            if(currMode!=prevMode)
                newBracketingIteration=true;
        }

        if(newBracketingIteration)
        {
            frameGroups.push_back({&it->second});
            // Toggle highlighting of the list to make rows from a single group the same color
            highlighted=!highlighted;
        }
        else
        {
            frameGroups.back().emplace_back(&it->second);
        }
        prevMode=it->second.exposureMode();
    }

    statusBar()->clearMessage();
}

void MainWindow::loadFiles(std::string const& dir)
{
    fileLoadingAborted=false;
    ui.generateRenderScriptBtn->hide();
    ui.abortLoadingBtn->show();
    framesModel->removeRows(0,framesModel->rowCount());
    filesMap.clear();

    statusProgressBar->show();
    std::size_t fileCount=0;
    statusBar()->showMessage(tr("Counting files..."));
    statusProgressBar->setRange(0,0);
    for(auto const& dentry : filesystem::recursive_directory_iterator(dir))
    {
        if(!is_directory(dentry))
        {
            ++fileCount;
            if(fileCount%100)
            {
                qApp->processEvents();
                if(fileLoadingAborted)
                {
                    statusBar()->showMessage(tr("Loading of files aborted"));
                    ui.abortLoadingBtn->hide();
                    statusProgressBar->hide();
                    return;
                }
            }
        }
    }

    statusProgressBar->setRange(0,fileCount);
    statusBar()->showMessage(tr("Loading files..."));
    // Load exposure info from EXIF
    {
        LibRaw libRaw;
        libRaw.set_exifparser_handler(exifHandler, this);
        for(auto const& dentry : filesystem::recursive_directory_iterator(dir))
        {
            if(is_directory(dentry)) continue;

            statusProgressBar->setValue(filesMap.size());

            currentFileBeingOpened=dentry.path().string().c_str();
            qApp->processEvents();
            if(fileLoadingAborted)
            {
                statusBar()->showMessage(tr("Loading of files aborted"));
                ui.abortLoadingBtn->hide();
                filesMap.clear();
                framesModel->removeRows(0,framesModel->rowCount());
                statusProgressBar->hide();
                return;
            }
            lastCreatedFile=nullptr;
            // Process EXIF data
            libRaw.open_file(currentFileBeingOpened.toStdString().c_str());
            libRaw.recycle();
            if(!exifHandlerError.empty())
            {
                filesMap.clear();
                QMessageBox::critical(this, tr("Error processing file"),tr("Failed to load EXIF data from file \"%1\": %1").arg(dentry.path().string().c_str()).arg(exifHandlerError.c_str()));
                statusBar()->showMessage(tr("Failed to load EXIF data from a file"));
                statusProgressBar->hide();
                return;
            }
        }
        statusBar()->clearMessage();
        statusProgressBar->hide();
        ui.abortLoadingBtn->hide();
        ui.generateRenderScriptBtn->show();
    }

    // Initialize total exposure values (using only the data we know
    for(auto& pair : filesMap)
    {
        auto& file=pair.second;
        file.exposure=1;
        if(!isnan(file.shutterTime)) file.exposure *= file.shutterTime;
        if(!isnan(file.iso        )) file.exposure *= file.iso;
        if(!isnan(file.aperture   )) file.exposure /= sqr(file.aperture);
    }

    // Fill the tree widget and all exposures set
    const auto unknownStr=tr("unknown");
    const auto toStringOrUnknown=[&unknownStr](auto num)
        { return std::isnan(num) ? unknownStr : QString::number(num); };
    for(auto const& pair : filesMap)
    {
        const auto& file=pair.second;
        QStandardItem* timeItem;
        framesModel->appendRow(QList{
                        new QStandardItem(QFileInfo(file.path).fileName()),
                        timeItem=new QStandardItem(timeToString(file.shotTime)),
                        new QStandardItem(file.shutterTimeString),
                        new QStandardItem(toStringOrUnknown(file.iso)),
                        new QStandardItem(std::isnan(file.aperture) ? unknownStr : QString("f/%1").arg(file.aperture,0,'g',2)),
                        new QStandardItem(toStringOrUnknown(file.exposure)),
                       });
        timeItem->setData(file.shotTime, FramesModel::ShotTimeRole);

        allExposureModes.insert(file.exposureMode());
    }
    
    for(int i=0;i<FramesModel::COLUMN_COUNT;++i)
        ui.treeView->resizeColumnToContents(i);

    if(!filesMap.empty())
        ui.treeView->selectionModel()->setCurrentIndex(framesModel->index(0,0),
                                                       QItemSelectionModel::Select|QItemSelectionModel::Rows);

    {
        auto dirForTitle=QString::fromStdString(dir);
        if(dirForTitle.back()=='/')
            dirForTitle.chop(1);
        dirFileName=QFileInfo(dirForTitle).fileName();
        setWindowTitle(dirFileName);
    }

    groupFiles();

    ui.generateRenderScriptBtn->show();
}

void MainWindow::frameSelectionChanged(QItemSelection const& selected,
                      [[maybe_unused]] QItemSelection const& deselected)
{
    const auto selectedIndices=selected.indexes();
    assert(selectedIndices.isEmpty() || selectedIndices.size()==FramesModel::COLUMN_COUNT);
    if(selectedIndices.isEmpty())
    {
        frameView->clear();
    }
    else
    {
        const auto idx=selectedIndices.front();
        const auto shotTime=idx.sibling(idx.row(),FramesModel::Column::ShotTime).data(FramesModel::ShotTimeRole);
        const auto img=readImage(toTime(shotTime));
        statusBar()->showMessage("Rendering image...");
        frameView->showImage(img.data, img.width, img.height);
        statusBar()->clearMessage();
    }
}

auto MainWindow::readImage(Time time) const -> Image
{
    LibRaw libRaw;
    const auto& path=filesMap.at(time).path;
    statusBar()->showMessage("Opening file...");
    libRaw.open_file(path.toStdString().c_str());

    statusBar()->showMessage("Unpacking file...");
    if(const auto error=libRaw.unpack())
    {
        QMessageBox::critical(const_cast<MainWindow*>(this), tr("Failed to read image"),
                              tr("LibRaw failed to unpack data from file \"%1\": error %2").arg(path).arg(error));
        statusBar()->showMessage("Failed to read file");
        return Image{{glm::vec3(1,0,1)},1,1};
    }
    statusBar()->showMessage("Converting RAW data to image...");
    libRaw.raw2image();

    statusBar()->showMessage("Preparing file data for rendering...");
    const auto& pre_mul=libRaw.imgdata.color.pre_mul;
    const float preMulMax=*std::max_element(std::begin(pre_mul),std::end(pre_mul));
    const float daylightWBCoefs[4]={pre_mul[0]/preMulMax,pre_mul[1]/preMulMax,pre_mul[2]/preMulMax,(pre_mul[3]==0 ? pre_mul[1] : pre_mul[3])/preMulMax};

    const float white=libRaw.imgdata.rawdata.color.maximum;
    const float black=libRaw.imgdata.rawdata.color.black;
    const ushort (*const data)[4]=libRaw.imgdata.image;

    // TODO: move the conversion to GLSL code (render to FBO, generate mipmap and render to screen)
    const auto rgbCoefR =daylightWBCoefs[0];
    const auto rgbCoefG1=daylightWBCoefs[1];
    const auto rgbCoefB =daylightWBCoefs[2];
    const auto rgbCoefG2=daylightWBCoefs[3];
    const auto clampAndSubB=[black,white](ushort p, bool& overexposed)
        {return (p>white-10 ? overexposed=true,white : p<black ? black : p)-black; };
    const auto col=[black,white](float p) {return p/(white-black);};

    enum {RED,GREEN1,BLUE,GREEN2};
    const int stride=libRaw.imgdata.sizes.iwidth;
    const int w=libRaw.imgdata.sizes.iwidth/2;
    const int h=libRaw.imgdata.sizes.iheight/2;
    Image img{{},w,h};
    for(int y=0;y<h;++y)
    {
        for(int x=0;x<w;++x)
        {
            const auto X=x*2, Y=y*2;
            bool overexposed=false;
            const ushort pixelTopLeft    =rgbCoefR *clampAndSubB(data[X+0+(Y+0)*stride][RED]   ,overexposed);
            const ushort pixelTopRight   =rgbCoefG1*clampAndSubB(data[X+1+(Y+0)*stride][GREEN1],overexposed);
            const ushort pixelBottomLeft =rgbCoefG2*clampAndSubB(data[X+0+(Y+1)*stride][GREEN2],overexposed);
            const ushort pixelBottomRight=rgbCoefB *clampAndSubB(data[X+1+(Y+1)*stride][BLUE]  ,overexposed);

            const auto green=(pixelTopRight+pixelBottomLeft)/2.;
            const auto red=pixelTopLeft, blue=pixelBottomRight;

            const auto& cam2srgb=libRaw.imgdata.rawdata.color.rgb_cam;
            const auto srgblR=cam2srgb[0][0]*red+cam2srgb[0][1]*green+cam2srgb[0][2]*blue;
            const auto srgblG=cam2srgb[1][0]*red+cam2srgb[1][1]*green+cam2srgb[1][2]*blue;
            const auto srgblB=cam2srgb[2][0]*red+cam2srgb[2][1]*green+cam2srgb[2][2]*blue;

            img.data.push_back({overexposed ? 1.f : col(srgblR),
                                overexposed ? 1.f : col(srgblG),
                                overexposed ? 1.f : col(srgblB)});
        }
    }
    statusBar()->showMessage("File read successfully");
    return img;
}

void MainWindow::onWheelScrolled(int delta, Qt::KeyboardModifiers modifiers)
{
    bool preserveExposureMode=false;
    if(modifiers==Qt::ControlModifier)
        preserveExposureMode=true;
    else if(modifiers!=Qt::NoModifier)
        return;

    const auto currentIdx=ui.treeView->selectionModel()->currentIndex();
    if(!currentIdx.isValid()) return;

    const auto col=currentIdx.column();
    const auto step = delta<0 ? 1 : -1;

    QModelIndex newIdx=currentIdx.sibling(currentIdx.row()+step, col);
    if(!newIdx.isValid()) return;

    if(preserveExposureMode)
    {
        const auto shotTimeIdx=currentIdx.sibling(currentIdx.row(),FramesModel::Column::ShotTime);
        const auto currentFile=filesMap.at(toTime(shotTimeIdx.data(FramesModel::ShotTimeRole)));
        do
        {
            const auto shotTimeIdx=newIdx.sibling(newIdx.row(),FramesModel::Column::ShotTime);
            const auto& newFile=filesMap.at(toTime(shotTimeIdx.data(FramesModel::ShotTimeRole)));
            if(newFile.exposureMode()==currentFile.exposureMode())
                break;
        }
        while((newIdx = newIdx.sibling(newIdx.row()+step, col)).isValid());

        if(!newIdx.isValid()) return;
    }
    ui.treeView->selectionModel()->setCurrentIndex(newIdx, QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
}

void MainWindow::onMouseMoved(QPoint pos)
{
    pixelInfoLabel->setText(QString("%1, %2").arg(pos.x()).arg(pos.y()));
}

void MainWindow::onMouseLeftFrameView()
{
    pixelInfoLabel->setText("");
}
