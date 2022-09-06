#pragma once

#include <memory>
#include <libraw/libraw.h>
#include <QImage>
#include <QFuture>
#include <QOpenGLWidget>
#include <QFutureWatcher>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_3_Core>

class RawHistogram;
class ToolsWidget;
class ImageCanvas : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
    std::shared_ptr<LibRaw> libRaw;
    ToolsWidget* tools_;
    RawHistogram* histogram_;
public:
    ImageCanvas(ToolsWidget* tools, RawHistogram* histogram, QWidget* parent=nullptr);
    ~ImageCanvas();
    void openFile(QString const& filename);

signals:
    void warning(QString const&);
    void loadingFile(QString const&);
    void zoomChanged(double zoom);
    void fullScreenToggleRequested();
    void nextFileRequested();
    void prevFileRequested();
    void firstFileRequested();
    void lastFileRequested();
    void previewLoaded();
    void previewNotAvailable();
    void cursorPositionUpdated(double x, double y);
    void cursorLeft();
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void paintEvent(QPaintEvent* event) override;
private:
    static int loadFile(std::shared_ptr<LibRaw> const& libRaw, QString const& filename);
    static QImage loadPreview(QString const& filename);
    void setupBuffers();
    void setupShaders();
    void demosaicImage();
    double scale() const;
    void onFileLoaded();
    void onPreviewLoaded();
    double scaleToSteps(const double scale) const;
    float getBlackLevel();

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
    QFuture<QImage> previewLoadStatus_;
    QFutureWatcher<QImage> previewLoadWatcher_;
    QImage preview_;
    bool demosaicedImageInverted_=false;
    bool demosaicedImageReady_=false;
    bool demosaicMessageShown_=false;
    bool dragging_=false;
};
