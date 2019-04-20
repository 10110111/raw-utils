#ifndef INCLUDE_ONCE_0F6B7FB9_7997_46E9_92E2_95089F2EEA21
#define INCLUDE_ONCE_0F6B7FB9_7997_46E9_92E2_95089F2EEA21

#include <QMainWindow>
#include "ui_MainWindow.h"
#include <glm/glm.hpp>
#include <QVector>
#include <set>

class FrameView;
class FramesModel;
class QProgressBar;
class QLabel;

class MainWindow : public QMainWindow
{
public:
    using Time=std::uint64_t;
    static constexpr auto NaN=std::numeric_limits<double>::quiet_NaN();

private /* types */:
    struct ExposureMode
    {
        double aperture;
        double iso;
        double shutterTime;
        QString formattedShutterTime;
        bool initialized=false;
        ExposureMode(double aperture, double iso, double shutterTime, QString const& formattedShutterTime)
            : aperture(aperture)
              , iso(iso)
              , shutterTime(shutterTime)
            , formattedShutterTime(formattedShutterTime)
            , initialized(true)
        {}
        ExposureMode()=default;
        static bool sameValue(double x, double y);
        bool operator==(ExposureMode const& other) const;
        bool operator!=(ExposureMode const& other) const { return !(*this==other); }
        bool operator<(ExposureMode const& rhs) const;
        QString toString() const;
    };
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

        ExposureMode exposureMode() const
        {
            return {aperture,iso,shutterTime,shutterTimeString};
        }
    };
    struct Image
    {
        QVector<glm::vec3> data;
        int width, height;
    };
private /* data */:
    Ui::MainWindow ui;
    FrameView* frameView;
    FramesModel* framesModel;
    QProgressBar* statusProgressBar;
    QLabel* pixelInfoLabel;
    bool fileLoadingAborted=false;
    bool renderScriptGenerationAborted=false;
    Frame* lastCreatedFile=nullptr;
    QString currentFileBeingOpened;
    QString dirFileName;
    // Files should be sorted by shot time, thus storing them in a map
    std::map<Time,Frame> filesMap;
    std::set<ExposureMode> allExposureModes;
    std::string exifHandlerError;
    // Images grouped by bracketing iteration. Inner vector contains images from a single iteration.
    std::vector<std::vector<Frame const*>> frameGroups;

private /* methods */:
    Image readImage(Time time) const;
    void loadFiles(std::string const& dir);
    void frameSelectionChanged(QItemSelection const& selected, QItemSelection const& deselected);
    void onMouseLeftFrameView();
    void onMouseMoved(QPoint pos);
    void onWheelScrolled(int delta, Qt::KeyboardModifiers modifiers);
    void onSelectionAdded(glm::ivec2 pointA, glm::ivec2 pointB);
    void onSelectionsRemoved();
    void openDir();
    void groupFiles();
    void generateRenderScript();
    struct PrevExpoMode
    {
        Time timeDist;
        ExposureMode mode;
    };
    std::map<ExposureMode, PrevExpoMode> makePrevExpoModesMap();
    static void exifHandler(void* context, int tag, int type, int count, unsigned byteOrder, void* ifp);
public:
    MainWindow(std::string const& dirToOpen);
};

#endif
