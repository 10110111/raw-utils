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

private:
    Ui::MainWindow ui;
    FrameView* frameView;
    FramesModel* framesModel;

    struct Image
    {
        QVector<glm::vec3> data;
        int width, height;
    };
    Image readImage(Time time);
    void loadFiles(std::string const& dir);
    void frameSelectionChanged(QItemSelection const& selected, QItemSelection const& deselected);
    void onWheelScrolled(int delta, Qt::KeyboardModifiers modifiers);
    void openDir();
public:
    MainWindow(std::string const& dirToOpen);
};

#endif
