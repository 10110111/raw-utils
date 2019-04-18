#ifndef INCLUDE_ONCE_0F6B7FB9_7997_46E9_92E2_95089F2EEA21
#define INCLUDE_ONCE_0F6B7FB9_7997_46E9_92E2_95089F2EEA21

#include <QMainWindow>
#include "ui_MainWindow.h"
#include <glm/glm.hpp>
#include <QVector>

class FrameView;
class FramesModel;

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
        ExposureMode(double aperture, double iso, double shutterTime)
            : aperture(aperture)
              , iso(iso)
              , shutterTime(shutterTime)
        {}
        bool operator==(ExposureMode const& other);
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
            return {aperture,iso,shutterTime};
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
    bool fileLoadingAborted=false;
    Frame* lastCreatedFile=nullptr;
    QString currentFileBeingOpened;
    // Files should be sorted by shot time, thus storing them in a map
    std::map<Time,Frame> filesMap;
    std::string exifHandlerError;

private /* methods */:
    Image readImage(Time time);
    void loadFiles(std::string const& dir);
    void frameSelectionChanged(QItemSelection const& selected, QItemSelection const& deselected);
    void onWheelScrolled(int delta, Qt::KeyboardModifiers modifiers);
    void openDir();
    static void exifHandler(void* context, int tag, int type, int count, unsigned byteOrder, void* ifp);
public:
    MainWindow(std::string const& dirToOpen);
};

#endif
