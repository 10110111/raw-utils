#include "ImageCanvas.hpp"
#include <cmath>
#include <cstring>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <QMessageBox>
#include <QtConcurrent>
#include "timing.hpp"
#include "Histogram.hpp"
#include "ToolsWidget.hpp"

constexpr int OPENGL_MAJOR_VERSION=3;
constexpr int OPENGL_MINOR_VERSION=3;
static QSurfaceFormat makeFormat()
{
    QSurfaceFormat format;
    format.setVersion(OPENGL_MAJOR_VERSION,OPENGL_MINOR_VERSION);
    format.setProfile(QSurfaceFormat::CoreProfile);
    return format;
}

void ImageCanvas::openFile(QString const& filename)
{
    fileLoadStatus_ = QtConcurrent::run([this,filename]{return loadFile(filename);});
    connect(&fileLoadWatcher_, &QFutureWatcher<int>::finished, this, &ImageCanvas::onFileLoaded);
    fileLoadWatcher_.setFuture(fileLoadStatus_);
}

int ImageCanvas::loadFile(QString const& filename)
{
    emit warning("");
    emit loadingFile(filename);
    const auto t0 = currentTime();

    libRaw.reset(new LibRaw);
    libRaw->imgdata.params.raw_processing_options &= ~LIBRAW_PROCESSING_CONVERTFLOAT_TO_INT;
    libRaw->open_file(filename.toStdString().c_str());
    if(const auto error=libRaw->unpack())
        return error;
    histogram_->compute(libRaw, getBlackLevel());

    const auto t1 = currentTime();
    qDebug().nospace() << "File loaded in " << double(t1-t0) << " seconds";

    return LIBRAW_SUCCESS;
}

ImageCanvas::ImageCanvas(ToolsWidget* tools, Histogram* histogram, QWidget* parent)
    : QOpenGLWidget(parent)
    , tools_(tools)
    , histogram_(histogram)
{
    setFormat(makeFormat());
    setFocusPolicy(Qt::StrongFocus);

    connect(tools_, &ToolsWidget::settingChanged, this, qOverload<>(&QWidget::update));
}

void ImageCanvas::onFileLoaded()
{
    if(const auto error = fileLoadStatus_.result())
    {
        QMessageBox::critical(this, tr("Error loading file"), tr("Failed to unpack file: %1").arg(libraw_strerror(error)));
        return;
    }

    update();
}
void ImageCanvas::setupBuffers()
{
    if(!vao_)
        glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    if(!vbo_)
        glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLfloat vertices[]=
    {
        -1, -1,
         1, -1,
        -1,  1,
         1,  1,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
    constexpr GLuint attribIndex=0;
    constexpr int coordsPerVertex=2;
    glVertexAttribPointer(attribIndex, coordsPerVertex, GL_FLOAT, false, 0, 0);
    glEnableVertexAttribArray(attribIndex);
    glBindVertexArray(0);
}

void ImageCanvas::setupShaders()
{
    {
        const char*const vertSrc = 1+R"(
#version 330
in vec3 vertex;
void main()
{
    gl_Position=vec4(vertex,1);
}
)";
        if(!demosaicProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc))
           QMessageBox::critical(this, tr("Shader compile failure"),
                                 tr("Failed to compile %1:\n%2").arg("demosaic vertex shader").arg(demosaicProgram_.log()));
        const char*const fragSrc = 1+R"(
#version 330
#extension GL_ARB_shading_language_420pack : require
uniform sampler2D image;
uniform float blackLevel, whiteLevel;
uniform float marginLeft, marginRight, marginTop, marginBottom;
uniform vec3 whiteBalanceCoefs;
uniform mat3 cam2srgb;
out vec3 linearSRGB;

float samplePhotoSite(const vec2 pos, const vec2 offset)
{
    const vec2 texCoord = (pos+0.5+offset)/textureSize(image,0).xy;
    return texture(image, texCoord).r;
}

uniform bool RED_FIRST;

void main()
{
    const int W = textureSize(image,0).x;
    const int H = textureSize(image,0).y;
    const vec2 pos = vec2(gl_FragCoord.x+marginLeft, H-gl_FragCoord.y-marginBottom)-0.5;
    const float vtl = samplePhotoSite(pos, vec2(-1,-1));
    const float vtc = samplePhotoSite(pos, vec2( 0,-1));
    const float vtr = samplePhotoSite(pos, vec2(+1,-1));
    const float vcl = samplePhotoSite(pos, vec2(-1, 0));
    const float vcc = samplePhotoSite(pos, vec2( 0, 0));
    const float vcr = samplePhotoSite(pos, vec2(+1, 0));
    const float vbl = samplePhotoSite(pos, vec2(-1,+1));
    const float vbc = samplePhotoSite(pos, vec2( 0,+1));
    const float vbr = samplePhotoSite(pos, vec2(+1,+1));

    const int TOP_LEFT    =0;
    const int TOP_RIGHT   =1;
    const int BOTTOM_LEFT =2;
    const int BOTTOM_RIGHT=3;

    const int photositeColorFilter = int(mod(pos.y, 2)*2 + mod(pos.x, 2));
    float subpixelFromTopLeftCF=0, green=0, subpixelFromBottomRightCF=0;
    if(photositeColorFilter == TOP_LEFT)
    {
        subpixelFromTopLeftCF = vcc;
        if(pos.x==marginLeft && pos.y==marginTop)
        {
            green = (vcr+vbc)/2;
            subpixelFromBottomRightCF = vbr;
        }
        else if(pos.y==marginTop)
        {
            green = (vcr+vbc+vcl)/3;
            subpixelFromBottomRightCF = (vbl+vbr)/2;
        }
        else if(pos.x==marginLeft)
        {
            green = (vtc+vcr+vbc)/3;
            subpixelFromBottomRightCF = (vtr+vbr)/2;
        }
        else
        {
            green = (vtc+vcr+vbc+vcl)/4;
            subpixelFromBottomRightCF = (vtl+vtr+vbl+vbr)/4;
        }
    }
    else if(photositeColorFilter == TOP_RIGHT)
    {
        green = vcc;
        if(pos.y==marginTop && pos.x==W-1-marginRight)
        {
            subpixelFromTopLeftCF = vcl;
            subpixelFromBottomRightCF = vbc;
        }
        else if(pos.y==marginTop)
        {
            subpixelFromTopLeftCF = (vcl+vcr)/2;
            subpixelFromBottomRightCF = vbc;
        }
        else if(pos.x==W-1-marginRight)
        {
            subpixelFromTopLeftCF = vcl;
            subpixelFromBottomRightCF = (vtc+vbc)/2;
        }
        else
        {
            subpixelFromTopLeftCF = (vcl+vcr)/2;
            subpixelFromBottomRightCF = (vtc+vbc)/2;
        }
    }
    else if(photositeColorFilter == BOTTOM_LEFT)
    {
        green = vcc;
        if(pos.x==marginLeft && pos.y==H-1-marginBottom)
        {
            subpixelFromTopLeftCF = vtc;
            subpixelFromBottomRightCF = vcr;
        }
        else if(pos.x==marginLeft)
        {
            subpixelFromTopLeftCF = (vtc+vbc)/2;
            subpixelFromBottomRightCF = vcr;
        }
        else if(pos.y==H-1-marginBottom)
        {
            subpixelFromTopLeftCF = vtc;
            subpixelFromBottomRightCF = (vcl+vcr)/2;
        }
        else
        {
            subpixelFromTopLeftCF = (vtc+vbc)/2;
            subpixelFromBottomRightCF = (vcl+vcr)/2;
        }
    }
    else if(photositeColorFilter == BOTTOM_RIGHT)
    {
        subpixelFromBottomRightCF = vcc;
        if(pos.y==H-1-marginBottom && pos.x==W-1-marginRight)
        {
            subpixelFromTopLeftCF = vtl;
            green = (vtc+vcl)/2;
        }
        else if(pos.y==H-1-marginBottom)
        {
            subpixelFromTopLeftCF = (vtl+vtr)/2;
            green = (vtc+vcr+vcl)/3;
        }
        else if(pos.x==W-1-marginRight)
        {
            subpixelFromTopLeftCF = (vtl+vbl)/2;
            green = (vtc+vbc+vcl)/3;
        }
        else
        {
            subpixelFromTopLeftCF = (vtl+vtr+vbl+vbr)/4;
            green = (vtc+vcr+vbc+vcl)/4;
        }
    }
    const vec3 rawRGB = RED_FIRST ? vec3(subpixelFromTopLeftCF,green,subpixelFromBottomRightCF) :
                                    vec3(subpixelFromBottomRightCF,green,subpixelFromTopLeftCF);
    const vec3 balancedRGB = (rawRGB-blackLevel)/(whiteLevel-blackLevel)*whiteBalanceCoefs;
    linearSRGB = cam2srgb*balancedRGB;
}
)";
        if(!demosaicProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc))
           QMessageBox::critical(this, tr("Error compiling shader"),
                                 tr("Failed to compile %1:\n%2").arg("demosaic fragment shader").arg(demosaicProgram_.log()));
        if(!demosaicProgram_.link())
            throw QMessageBox::critical(this, tr("Error linking shader program"),
                                        tr("Failed to link %1:\n%2").arg("demosaic shader program").arg(demosaicProgram_.log()));
    }
    {
        const char*const vertSrc = 1+R"(
#version 330
#extension GL_ARB_shading_language_420pack : require
in vec3 vertex;
uniform float scale;
uniform vec2 shift;
uniform vec2 viewportSize;
uniform vec2 imageSize;
out vec2 texCoord;
void main()
{
    const vec2 scaleRelativeToViewport = (viewportSize/imageSize/scale);
    texCoord = vertex.xy*scaleRelativeToViewport/2+0.5 + vec2(-shift.x,shift.y)/viewportSize*scaleRelativeToViewport;
    gl_Position=vec4(vertex,1);
}
)";
        if(!displayProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc))
           QMessageBox::critical(this, tr("Shader compile failure"),
                                 tr("Failed to compile %1:\n%2").arg("display vertex shader").arg(displayProgram_.log()));
        const char*const fragSrc = 1+R"(
#version 330
#extension GL_ARB_shading_language_420pack : require

uniform sampler2D sRGBLinearImage;
uniform float exposureCompensationCoef;
in vec2 texCoord;
out vec4 color;

vec3 sRGBTransferFunction(const vec3 c)
{
    return step(0.0031308,c)*(1.055*pow(c, vec3(1/2.4))-0.055)+step(-0.0031308,-c)*12.92*c;
}

void main()
{
    const vec3 linearSRGB = texture(sRGBLinearImage, texCoord).rgb;
    color = vec4(sRGBTransferFunction(linearSRGB*exposureCompensationCoef), 1);
}
)";
        if(!displayProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc))
           QMessageBox::critical(this, tr("Error compiling shader"),
                                 tr("Failed to compile %1:\n%2").arg("display fragment shader").arg(displayProgram_.log()));
        if(!displayProgram_.link())
            throw QMessageBox::critical(this, tr("Error linking shader program"),
                                        tr("Failed to link %1:\n%2").arg("display shader program").arg(displayProgram_.log()));

    }
}

void ImageCanvas::initializeGL()
{
    const auto t0 = currentTime();
    if(!initializeOpenGLFunctions())
    {
        QMessageBox::critical(this, tr("Error initializing OpenGL"), tr("Failed to initialize OpenGL %1.%2 functions")
                                    .arg(OPENGL_MAJOR_VERSION)
                                    .arg(OPENGL_MINOR_VERSION));
        return;
    }

    setupBuffers();

    glGenTextures(1, &rawImageTex_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rawImageTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    glGenTextures(1, &demosaicedImageTex_);
    glBindTexture(GL_TEXTURE_2D, demosaicedImageTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glGenFramebuffers(1, &demosaicFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, demosaicFBO_);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, demosaicedImageTex_, 0);

    setupShaders();

    glFinish();
    const auto t1 = currentTime();
    qDebug().nospace() << "OpenGL initialized in " << double(t1-t0) << " seconds";
}

ImageCanvas::~ImageCanvas()
{
    makeCurrent();
    glDeleteTextures(1, &rawImageTex_);
}

float ImageCanvas::getBlackLevel()
{
    float blackLevel = 0;
    if(libRaw->imgdata.rawdata.color.black)
    {
        blackLevel = libRaw->imgdata.rawdata.color.black;
    }
    else
    {
        const auto& cblack = libRaw->imgdata.rawdata.color.cblack;
        const auto dimX=cblack[4], dimY=cblack[5];
        if(dimX==1 && dimY==1)
        {
            blackLevel = cblack[6];
        }
        else if(dimX==2 && dimY==2)
        {
            if(cblack[6]==cblack[7] && cblack[6]==cblack[8] && cblack[6]==cblack[9])
                blackLevel = cblack[6];
            else
            {
                blackLevel = (cblack[6] + cblack[7] + cblack[8] + cblack[9])/4.;
                emit warning(tr("Warning: black level values differ between photosites: %1,%2,%3,%4. Using average in computations.")
                                .arg(cblack[6]).arg(cblack[7]).arg(cblack[8]).arg(cblack[9]));
            }
        }
        else if(dimX==0 && dimY==0)
            emit warning(tr("Warning: black level has zero dimensions"));
        else
            emit warning(tr(u8"Warning: unexpected configuration of black level information: dimensions %1×%2, data: %3,%4,%5,%6,...")
                            .arg(dimX).arg(dimY).arg(cblack[6]).arg(cblack[7]).arg(cblack[8]).arg(cblack[9]));
    }
    return blackLevel;
}

void ImageCanvas::demosaicImage()
{
    GLint origFBO=-1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &origFBO);

    {
        const auto t0 = currentTime();

        glActiveTexture(GL_TEXTURE0);

        glBindTexture(GL_TEXTURE_2D, rawImageTex_);
        const auto& sizes = libRaw->imgdata.rawdata.sizes;
        const bool haveFP = libRaw->have_fpdata();
        if(haveFP && libRaw->imgdata.rawdata.float_image)
        {
            qDebug() << "Using float data";
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, sizes.raw_width, sizes.raw_height,
                         0, GL_RED, GL_FLOAT, libRaw->imgdata.rawdata.float_image);
        }
        else if(!haveFP && libRaw->imgdata.rawdata.raw_image)
        {
            qDebug() << "Using uint16 data";
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, sizes.raw_width, sizes.raw_height,
                         0, GL_RED, GL_UNSIGNED_SHORT, libRaw->imgdata.rawdata.raw_image);
        }
        else
        {
            qDebug() << "No image available, showing constant color";
            constexpr float texel=0.5;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 1, 1, 0, GL_RED, GL_FLOAT, &texel);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        }

        glBindTexture(GL_TEXTURE_2D, demosaicedImageTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, sizes.width, sizes.height, 0, GL_RGB, GL_FLOAT, nullptr);

        glFinish();
        const auto t1 = currentTime();
        qDebug().nospace() << "Textures uploaded in " << double(t1-t0) << " seconds";
    }

    const auto t0 = currentTime();

    glBindFramebuffer(GL_FRAMEBUFFER, demosaicFBO_);
    const auto& sizes=libRaw->imgdata.sizes;
    glViewport(0, 0, sizes.width, sizes.height);

    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rawImageTex_);
    demosaicProgram_.bind();
    demosaicProgram_.setUniformValue("image", 0);
    {
        if(libRaw->imgdata.idata.cdesc[libRaw->COLOR(0,1)] != 'G' || libRaw->imgdata.idata.cdesc[libRaw->COLOR(1,0)] != 'G')
        {
            const auto col00 = libRaw->imgdata.idata.cdesc[libRaw->COLOR(0,0)];
            const auto col01 = libRaw->imgdata.idata.cdesc[libRaw->COLOR(0,1)];
            const auto col10 = libRaw->imgdata.idata.cdesc[libRaw->COLOR(1,0)];
            const auto col11 = libRaw->imgdata.idata.cdesc[libRaw->COLOR(1,1)];
            emit warning(tr("Warning: unexpected CFA pattern (%1%2/%3%4), colors will be wrong!").arg(col00).arg(col01).arg(col10).arg(col11));
        }

        const char topLeftCF = libRaw->imgdata.idata.cdesc[libRaw->COLOR(0,0)];
        if(topLeftCF == 'R')
            demosaicProgram_.setUniformValue("RED_FIRST", true);
        else
            demosaicProgram_.setUniformValue("RED_FIRST", false);
    }
    const float blackLevel=getBlackLevel();
    const float divisor = libRaw->is_floating_point() ? 1 : 65535;
    demosaicProgram_.setUniformValue("blackLevel", float(blackLevel/divisor));
    demosaicProgram_.setUniformValue("whiteLevel", float(libRaw->imgdata.rawdata.color.maximum/divisor));
    {
        const auto& pre_mul=libRaw->imgdata.color.pre_mul;
        const float preMulMax=*std::max_element(std::begin(pre_mul),std::end(pre_mul));
        const auto daylightWBCoefs = QVector3D(pre_mul[0],pre_mul[1],pre_mul[2])/preMulMax;
        demosaicProgram_.setUniformValue("whiteBalanceCoefs", daylightWBCoefs);
    }
    {
        const auto& camrgb = libRaw->imgdata.rawdata.color.rgb_cam;
        const float cam2srgb[9] = {camrgb[0][0], camrgb[0][1], camrgb[0][2],
                                   camrgb[1][0], camrgb[1][1], camrgb[1][2],
                                   camrgb[2][0], camrgb[2][1], camrgb[2][2]};
        demosaicProgram_.setUniformValue("cam2srgb", QMatrix3x3(cam2srgb));
    }
    demosaicProgram_.setUniformValue("marginLeft", float(sizes.left_margin));
    demosaicProgram_.setUniformValue("marginTop", float(sizes.top_margin));
    demosaicProgram_.setUniformValue("marginBottom", float(sizes.raw_height - sizes.height - sizes.top_margin));
    demosaicProgram_.setUniformValue("marginRight", float(sizes.raw_width - sizes.width - sizes.left_margin));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, demosaicedImageTex_);
    glGenerateMipmap(GL_TEXTURE_2D);

    glFinish();
    const auto t1 = currentTime();
    qDebug().nospace() << "Image demosaiced in " << double(t1-t0) << " seconds";

    glBindFramebuffer(GL_FRAMEBUFFER, origFBO);

    demosaicedImageReady_ = true;
}

void ImageCanvas::resizeGL([[maybe_unused]] const int w, [[maybe_unused]] const int h)
{
    if(!libRaw) return;
    emit zoomChanged(scale());
}

void ImageCanvas::wheelEvent(QWheelEvent*const event)
{
    if((event->modifiers() & (Qt::ControlModifier|Qt::ShiftModifier|Qt::AltModifier)) != Qt::ControlModifier)
        return;

    const double steps = event->angleDelta().y() / 120.;
    const double oldScale = scale();
    if(!scaleSteps_)
        scaleSteps_ = scaleToSteps(oldScale);
    scaleSteps_ = *scaleSteps_ + steps/2;
    const double newScale = scale();
    imageShift_ = (event->pos() - QPoint(width(),height())/2)*(1 - newScale/oldScale) + newScale/oldScale*imageShift_;
    update();
    emit zoomChanged(newScale);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent*const event)
{
    if(dragging_)
    {
        imageShift_ += event->pos() - dragStartPos_;
        dragStartPos_ = event->pos();
        update();
    }
}

void ImageCanvas::mousePressEvent(QMouseEvent*const event)
{
    dragStartPos_ = event->pos();
    dragging_ = true;
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = false;
}

void ImageCanvas::keyPressEvent(QKeyEvent*const event)
{
    if(event->modifiers() & (Qt::ControlModifier|Qt::ShiftModifier|Qt::AltModifier))
        return;
    switch(event->key())
    {
    case Qt::Key_Z:
        scaleSteps_ = 0.;
        emit zoomChanged(scale());
        break;
    case Qt::Key_X:
        scaleSteps_.reset();
        imageShift_ = QPoint(0,0);
        emit zoomChanged(scale());
        break;
    case Qt::Key_C:
        imageShift_ = QPoint(0,0);
        break;
    case Qt::Key_F11:
        emit fullScreenToggleRequested();
        return;
    }
    update();
}

double ImageCanvas::scale() const
{
    const float imageWidth = libRaw->imgdata.sizes.width;
    const float imageHeight = libRaw->imgdata.sizes.height;
    float scale = std::min(width()/imageWidth, height()/imageHeight);
    if(scaleSteps_)
        scale = std::pow(2., *scaleSteps_ / 2.);
    return scale;
}

double ImageCanvas::scaleToSteps(const double scale) const
{
    return std::log2(scale) * 2;
}

void ImageCanvas::paintGL()
{
    if(!isVisible()) return;
    if(!demosaicedImageReady_)
        demosaicImage();

    glViewport(0, 0, width(), height());
    glBindVertexArray(vao_);

    displayProgram_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, demosaicedImageTex_);
    displayProgram_.setUniformValue("sRGBLinearImage", 0);
    displayProgram_.setUniformValue("scale", float(scale()));
    displayProgram_.setUniformValue("shift", QVector2D(imageShift_.x(),imageShift_.y()));
    displayProgram_.setUniformValue("viewportSize", QVector2D(width(),height()));
    displayProgram_.setUniformValue("imageSize", QVector2D(libRaw->imgdata.sizes.width, libRaw->imgdata.sizes.height));
    displayProgram_.setUniformValue("exposureCompensationCoef", float(std::pow(10., tools_->exposureCompensation())));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void ImageCanvas::paintEvent(QPaintEvent*const event)
{
    if(!libRaw)
    {
        QPainter p(this);
        p.fillRect(rect(), palette().window());
        p.setPen(palette().windowText().color());
        p.drawText(rect(), Qt::AlignHCenter|Qt::AlignVCenter, tr("(nothing to display)"));
        return;
    }
    if(!fileLoadStatus_.isFinished())
    {
        QPainter p(this);
        p.fillRect(rect(), palette().window());
        p.setPen(palette().windowText().color());
        p.drawText(rect(), Qt::AlignHCenter|Qt::AlignVCenter, tr("Loading file..."));
        emit zoomChanged(scale());
        return;
    }
    if(!demosaicMessageShown_)
    {
        QPainter p(this);
        p.fillRect(rect(), palette().window());
        p.setPen(palette().windowText().color());
        p.drawText(rect(), Qt::AlignHCenter|Qt::AlignVCenter, tr("Demosaicing image..."));
        demosaicMessageShown_ = true;
        // And repaint, but avoid possible recursive call of paintEvent
        QTimer::singleShot(0, [this]{update();});
        return;
    }
    QOpenGLWidget::paintEvent(event);
}
