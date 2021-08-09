#pragma once

#include <memory>
#include <libraw/libraw.h>
#include <QFuture>
#include <QOpenGLWidget>
#include <QFutureWatcher>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_3_Core>

class Histogram;
class ToolsWidget;
class ImageCanvas : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core
{
    std::shared_ptr<LibRaw> libRaw;
    ToolsWidget* tools_;
    Histogram* histogram_;
public:
    ImageCanvas(QString const& filename, ToolsWidget* tools, Histogram* histogram, QWidget* parent=nullptr);
    ~ImageCanvas();
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void initializeGL() override;
    void paintGL() override;
    void paintEvent(QPaintEvent* event) override;
private:
    int loadFile(QString const& filename);
    void setupBuffers();
    void setupShaders();
    void demosaicImage();
    double scale() const;
    void onFileLoaded();
    double scaleToSteps(const double scale) const;
    float getBlackLevel() const;

private:
    GLuint rawImageTex_=0, demosaicedImageTex_=0;
    GLuint vao_=0;
    GLuint vbo_=0;
    GLuint demosaicFBO_=0;
    QOpenGLShaderProgram demosaicProgram_;
    QOpenGLShaderProgram displayProgram_;
    QPoint dragStartPos_;
    QPoint imageShift_;
    std::optional<double> scaleSteps_;
    QFuture<int> fileLoadStatus_;
    QFutureWatcher<int> fileLoadWatcher_;
    bool demosaicedImageReady_=false;
    bool demosaicMessageShown_=false;
    bool dragging_=false;
};
