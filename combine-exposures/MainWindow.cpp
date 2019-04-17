#include "MainWindow.h"

#include "FramesModel.h"
#include "FrameView.h"

#include <libraw/libraw.h>

#include <QMessageBox>
#include <QFileDialog>
#include <QWheelEvent>
#include <QDateTime>
#include <QFileInfo>

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

static constexpr auto NaN=std::numeric_limits<double>::quiet_NaN();
static constexpr auto sqr=[](auto x){return x*x;};
using std::isnan;
using Time=MainWindow::Time;

namespace
{

Time makeTime(int year, int month, int day, int hour, int minute, int second)
{
    return QDateTime(QDate(year,month,day),QTime(hour,minute,second)).toSecsSinceEpoch();
}
QString toString(Time time)
{
    return QDateTime::fromSecsSinceEpoch(time).toString("yyyy-MM-dd HH:mm:ss");
}
Time toTime(QVariant timeVar)
{
    return timeVar.toULongLong();
}

struct Frame
{
    Frame(Time const& shotTime, QString const& path)
        : shotTime(shotTime)
        , path(path)
    {
    }
    Time shotTime;
    QString path;
    double aperture=NaN;
    double iso=NaN;
    double shutterTime=NaN;
    QString shutterTimeString="unknown";
    double exposure; // combined aperture, ISO and shutter time
};
// Should be sorted by shot time, thus map
std::map<Time,Frame> filesMap;
Frame* lastCreatedFile=nullptr;
QString currentFileBeingOpened;

bool sameExposureMode(Frame const& a, Frame const& b)
{
    const auto sameValue=[](double x, double y)
    { return (!isnan(x) && !isnan(y) && x==y) || (isnan(x) && isnan(y)); };
    return sameValue(a.aperture,b.aperture) &&
           sameValue(a.shutterTime,b.shutterTime) &&
           sameValue(a.iso,b.iso);
}


static std::string exifHandlerError;
void exifHandler([[maybe_unused]] void* context, int tag, [[maybe_unused]] int type, int count, unsigned byteOrder, void* ifp)
{
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
            const auto it=filesMap.emplace(std::make_pair(shotTime,Frame{shotTime,currentFileBeingOpened}));
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

} // anonymous namespace

MainWindow::MainWindow(std::string const& dirToOpen)
{
    ui.setupUi(this);
    ui.abortLoadingBtn->setHidden(true);
    connect(ui.abortLoadingBtn,&QPushButton::clicked, this,[this]{fileLoadingAborted=true;});
    connect(ui.action_Open_directory, &QAction::triggered, this, &MainWindow::openDir);
    connect(ui.action_Quit, &QAction::triggered, qApp, &QApplication::quit);
    ui.treeView->setModel(framesModel=new FramesModel(this));
    ui.treeView->setRootIsDecorated(false);
    connect(ui.treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::frameSelectionChanged);

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
    connect(frameView, &FrameView::wheelScrolled, this, &MainWindow::onWheelScrolled);

    if(!dirToOpen.empty())
        QMetaObject::invokeMethod(this,[this,dirToOpen]{loadFiles(dirToOpen);},Qt::QueuedConnection);
}

void MainWindow::openDir()
{
    const auto dirPath=QFileDialog::getExistingDirectory(this);
    if(dirPath.isNull()) return;
    loadFiles(dirPath.toStdString());
}

void MainWindow::loadFiles(std::string const& dir)
{
    fileLoadingAborted=false;
    ui.abortLoadingBtn->setVisible(true);
    framesModel->removeRows(0,framesModel->rowCount());
    filesMap.clear();
    // Load exposure info from EXIF
    {
        LibRaw libRaw;
        libRaw.set_exifparser_handler(exifHandler, 0);
        for(auto const& dentry : filesystem::recursive_directory_iterator(dir))
        {
            currentFileBeingOpened=dentry.path().string().c_str();
            statusBar()->showMessage(tr("Opening file \"%1\"...").arg(currentFileBeingOpened));
            qApp->processEvents();
            if(fileLoadingAborted)
            {
                statusBar()->showMessage(tr("Loading of files aborted"));
                filesMap.clear();
                framesModel->removeRows(0,framesModel->rowCount());
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
                return;
            }
        }
        statusBar()->clearMessage();
        ui.abortLoadingBtn->setHidden(true);
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

    // Fill the tree widget
    const auto unknownStr=tr("unknown");
    const auto toStringOrUnknown=[&unknownStr](auto num)
        { return std::isnan(num) ? unknownStr : QString::number(num); };
    for(auto const& pair : filesMap)
    {
        const auto& file=pair.second;
        QStandardItem* timeItem;
        framesModel->appendRow(QList{
                        new QStandardItem(QFileInfo(file.path).fileName()),
                        timeItem=new QStandardItem(toString(file.shotTime)),
                        new QStandardItem(file.shutterTimeString),
                        new QStandardItem(toStringOrUnknown(file.iso)),
                        new QStandardItem(std::isnan(file.aperture) ? unknownStr : QString("f/%1").arg(file.aperture,0,'g',2)),
                        new QStandardItem(toStringOrUnknown(file.exposure)),
                       });
        timeItem->setData(file.shotTime, FramesModel::ShotTimeRole);
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
        setWindowTitle(QFileInfo(dirForTitle).fileName());
    }
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

auto MainWindow::readImage(Time time) -> Image
{
    LibRaw libRaw;
    const auto& path=filesMap.at(time).path;
    statusBar()->showMessage("Opening file...");
    libRaw.open_file(path.toStdString().c_str());

    statusBar()->showMessage("Unpacking file...");
    if(const auto error=libRaw.unpack())
    {
        QMessageBox::critical(this, tr("Failed to read image"),
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
            if(sameExposureMode(newFile,currentFile))
                break;
        }
        while((newIdx = newIdx.sibling(newIdx.row()+step, col)).isValid());

        if(!newIdx.isValid()) return;
    }
    ui.treeView->selectionModel()->setCurrentIndex(newIdx, QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
}
