#ifndef INCLUDE_ONCE_0632B2D6_98A4_4BA6_85F2_EF34D7ABBC53
#define INCLUDE_ONCE_0632B2D6_98A4_4BA6_85F2_EF34D7ABBC53

#include <QGLWidget>
#include <glm/glm.hpp>

class FrameView : public QGLWidget
{
    Q_OBJECT
public:
    enum class NormalizationMode
    {
        None,
        DivideByMax,
        DivideByAverage,
    };

private:
    GLuint vao=0, vbo=0, tex=0;
    GLuint mainProgram=0, selectionDrawProgram=0;
    GLfloat scale=1;
    int imgWidth=1, imgHeight=1;
    bool overexposureMarkingEnabled=false;
    QVector<glm::vec3> imageDataToLoad;
    bool imageNeedsUploading=false;
    struct Selection
    {
        // Selection rectangle corners in the image coordinates (point (0,0) is top-left)
        glm::ivec2 pointA, pointB;
    };
    std::vector<Selection> selections;
    QPoint dragStart;
    bool dragging=false;
    glm::vec3 averageOfSelectedPixels=glm::vec3(1,1,1);
    glm::vec3 maxFromSelectedPixels=glm::vec3(1,1,1);
    NormalizationMode normalizationMode=NormalizationMode::DivideByMax;

    void initShaders();
    void setupBuffers();
    void initTextures();
    void updateSelectedPixelsInfo();
    void addSelectionRectangle(QPoint const& firstPoint);
    void updateLastSelectionRectangle(QPoint const& pointB);
    glm::vec2 screenPosToImagePixelPos(glm::vec2 p) const;
public:
    FrameView(QWidget* parent=nullptr);
    void showImage(QVector<glm::vec3> const& data, int width, int height);
    void clear();
    void setScale(double newScale);
    void setMarkOverexposure(bool enable);
    void setNormalizationMode(NormalizationMode mode);
    void gatherSelectedPixelsInfo(glm::vec3 const* imageData,
                                  int imgWidth, int imgHeight,
                                  glm::vec3& maxFromSelectedPixels,
                                  glm::vec3& averageOfSelectedPixels) const;

signals:
    void wheelScrolled(int delta, Qt::KeyboardModifiers modifiers);
    void selectionAdded(glm::ivec2 pointA, glm::ivec2 pointB);
    void selectionsRemoved();
protected:
    void initializeGL() override;
    void paintGL() override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
};

#endif
