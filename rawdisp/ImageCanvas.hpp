#pragma once

#include <libraw/libraw.h>
#include <QOpenGLWidget>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_3_Core>

class ToolsWidget;
class ImageCanvas : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core
{
    LibRaw libRaw;
    ToolsWidget* tools_;
public:
    ImageCanvas(QString const& filename, ToolsWidget* tools, QWidget* parent=nullptr);
    ~ImageCanvas();
protected:
    void initializeGL() override;
    void paintGL() override;
private:
    void loadFile(QString const& filename);
    void setupBuffers();
    void setupShaders();
    void demosaicImage();

private:
    GLuint rawImageTex_=0, demosaicedImageTex_=0;
    GLuint vao_=0;
    GLuint vbo_=0;
    GLuint demosaicFBO_=0;
    QOpenGLShaderProgram demosaicProgram_;
    QOpenGLShaderProgram displayProgram_;
};
