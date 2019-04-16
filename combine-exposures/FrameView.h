#ifndef INCLUDE_ONCE_0632B2D6_98A4_4BA6_85F2_EF34D7ABBC53
#define INCLUDE_ONCE_0632B2D6_98A4_4BA6_85F2_EF34D7ABBC53

#include <QGLWidget>
#include <glm/glm.hpp>

class FrameView : public QGLWidget
{
    Q_OBJECT

    GLuint vao=0, vbo=0, tex=0;
    GLuint mainProgram=0, selectionDrawProgram=0;
    GLfloat scale=1;
    int imgWidth=1, imgHeight=1;
    bool overexposureMarkingEnabled=false;
    bool divisionByMeanPixelBrightnessEnabled=true;
    QVector<glm::vec3> imageDataToLoad;
    bool imageNeedsUploading=false;
    // Selection in the image coordinates
    glm::ivec2 selectionPointA=glm::ivec2(0), selectionPointB=glm::ivec2(0);
    QPoint dragStart;
    bool dragging=false;
    glm::vec3 meanOfSelectedPixels=glm::vec3(1,1,1);

    void initShaders();
    void setupBuffers();
    void initTextures();
    glm::vec2 screenPosToImagePixelPos(glm::vec2 p) const;
public:
    FrameView(QWidget* parent=nullptr);
    void showImage(QVector<glm::vec3> const& data, int width, int height);
    void clear();
    void setScale(double newScale);
    void setMarkOverexposure(bool enable);
    void setDivideByMeanPixelBrightness(bool enable);
    void setSelectionRectangle(QPoint const& cornerA, QPoint const& cornerB);

signals:
    void wheelScrolled(int delta, Qt::KeyboardModifiers modifiers);
protected:
    void initializeGL() override;
    void paintGL() override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
};

#endif
