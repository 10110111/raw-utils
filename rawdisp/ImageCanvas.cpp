#include "ImageCanvas.hpp"
#include <cmath>
#include <cstring>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <QMessageBox>
#include <QFileDialog>
#include <QImageReader>
#include <QtConcurrent>
#include "timing.hpp"
#include "RawHistogram.hpp"
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

static double sRGBTransferFunction(const double c)
{
    return c > 0.0031308 ? 1.055*std::pow(c, 1/2.4)-0.055
                         : 12.92*c;
}

void ImageCanvas::openFile(QString const& filename)
{
    currentFile_ = filename;
    demosaicedImageReady_=false;
    demosaicStarted_=false;
    emit warning("");
    emit loadingFile(filename);
    libRaw.reset();

    if(QFileInfo(filename).isDir())
    {
        emit fileLoadingFinished();
        return;
    }

    preview_ = {};
    previewLoadStatus_ = QtConcurrent::run([filename]{return loadPreview(filename);});
    connect(&previewLoadWatcher_, &QFutureWatcher<int>::finished, this, &ImageCanvas::onPreviewLoaded);
    previewLoadWatcher_.setFuture(previewLoadStatus_);

    libRaw.reset(new LibRaw);
    fileLoadStatus_ = QtConcurrent::run([libRaw=this->libRaw,filename]{return loadFile(libRaw,filename);});
    connect(&fileLoadWatcher_, &QFutureWatcher<int>::finished, this, &ImageCanvas::onFileLoaded);
    fileLoadWatcher_.setFuture(fileLoadStatus_);
}

int ImageCanvas::loadFile(std::shared_ptr<LibRaw> const& libRaw, QString const& filename)
{
    const auto t0 = currentTime();

#if LIBRAW_MINOR_VERSION < 21
    libRaw->imgdata.params.raw_processing_options &= ~LIBRAW_PROCESSING_CONVERTFLOAT_TO_INT;
#else
    libRaw->imgdata.rawparams.options &= ~LIBRAW_RAWOPTIONS_CONVERTFLOAT_TO_INT;
#endif
    if(const auto error=libRaw->open_file(filename.toStdString().c_str()))
        return error;
    if(const auto error=libRaw->unpack())
        return error;

    const auto t1 = currentTime();
    qDebug().nospace() << "File loaded in " << double(t1-t0) << " seconds";

    return LIBRAW_SUCCESS;
}

QImage ImageCanvas::loadPreview(QString const& filename)
{
    const auto t0 = currentTime();
    LibRaw libRaw;
    if(const auto error=libRaw.open_file(filename.toStdString().c_str()))
    {
        qDebug().nospace() << "loadPreview() failed to open file: " << libraw_strerror(error);
        return {};
    }
    if(const auto error = libRaw.unpack_thumb())
    {
        qDebug().nospace() << "loadPreview() failed to unpack thumbnail: " << libraw_strerror(error);
        return {};
    }
    if(libRaw.imgdata.thumbnail.tformat != LIBRAW_THUMBNAIL_JPEG)
    {
        qDebug().nospace() << "Preview format is not JPEG, instead it's " << libRaw.imgdata.thumbnail.tformat;
        return {};
    }

    auto arr = QByteArray::fromRawData(libRaw.imgdata.thumbnail.thumb, libRaw.imgdata.thumbnail.tlength);
    QBuffer buf(&arr);
    QImageReader reader(&buf);
    const auto img = reader.read();
    const auto t1 = currentTime();

    if(img.isNull())
        qDebug().nospace() << "Failed to load preview: " << reader.errorString();
    else
        qDebug().nospace() << "Preview loaded in " << double(t1-t0) << " seconds";


    return img;
}

ImageCanvas::ImageCanvas(ToolsWidget* tools, RawHistogram* histogram, QWidget* parent)
    : QOpenGLWidget(parent)
    , tools_(tools)
    , histogram_(histogram)
{
    setFormat(makeFormat());
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    connect(tools_, &ToolsWidget::settingChanged, this, qOverload<>(&QWidget::update));
    connect(tools_, &ToolsWidget::demosaicSettingChanged, this, [this]{openFile(currentFile_);});
}

void ImageCanvas::onFileLoaded()
{
    if(const auto error = fileLoadStatus_.result())
    {
        QMessageBox::critical(this, tr("Error loading file"), tr("Failed to unpack file: %1").arg(libraw_strerror(error)));
        oldDemosaicedImagePresent_ = false;
        emit fileLoadingFinished();
        return;
    }

    histogram_->compute(libRaw, getBlackLevel());
    update();
}

void ImageCanvas::onPreviewLoaded()
{
    preview_ = previewLoadStatus_.result();
    if(preview_.isNull())
        emit previewNotAvailable();
    else
        emit previewLoaded();
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
        if(!denoiseProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc))
           QMessageBox::critical(this, tr("Shader compile failure"),
                                 tr("Failed to compile %1:\n%2").arg("denoise vertex shader").arg(denoiseProgram_.log()));
        const char*const fragSrc = 1+R"(
#version 330
#extension GL_ARB_shading_language_420pack : require
uniform sampler2D image;
uniform float whiteLevel;
out vec4 color;

float samplePhotoSite(const vec2 pos, const vec2 offset)
{
    vec2 texCoord = (pos+0.5+offset)/textureSize(image,0).xy;
    return texture(image, texCoord).r;
}

void main()
{
    const vec2 pos = vec2(gl_FragCoord.x, gl_FragCoord.y)-0.5;

    const float vtl = samplePhotoSite(pos, vec2(-1,-1));
    const float vtc = samplePhotoSite(pos, vec2( 0,-1));
    const float vtr = samplePhotoSite(pos, vec2(+1,-1));
    const float vcl = samplePhotoSite(pos, vec2(-1, 0));
    const float vcc = samplePhotoSite(pos, vec2( 0, 0));
    const float vcr = samplePhotoSite(pos, vec2(+1, 0));
    const float vbl = samplePhotoSite(pos, vec2(-1,+1));
    const float vbc = samplePhotoSite(pos, vec2( 0,+1));
    const float vbr = samplePhotoSite(pos, vec2(+1,+1));

    float c = vcc;
    if(vcc > 0.1*whiteLevel && vcc > 1.5*max(vtl,max(vtc,max(vtr,max(vcr,max(vbr,max(vbc,max(vbl,vcl))))))))
    {
        c = (vtl+vtc+vtr+vcr+vbr+vbc+vbl+vcl)/8;
    }
    color = vec4(c);
}
)";
        if(!denoiseProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc))
           QMessageBox::critical(this, tr("Error compiling shader"),
                                 tr("Failed to compile %1:\n%2").arg("denoise fragment shader").arg(denoiseProgram_.log()));
        if(!denoiseProgram_.link())
            throw QMessageBox::critical(this, tr("Error linking shader program"),
                                        tr("Failed to link %1:\n%2").arg("denoise shader program").arg(denoiseProgram_.log()));
    }
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
uniform bool verticalInversion;
uniform bool reducePepperNoise;
out vec4 linearSRGB; // w-component is 1 if any raw component is saturated

float samplePhotoSite(const vec2 pos, const vec2 offset)
{
    vec2 texCoord = (pos+0.5+offset)/textureSize(image,0).xy;
    if(verticalInversion)
        texCoord.t = 1 - texCoord.t;
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
    const bool highlightClipped = rawRGB.r >= whiteLevel || rawRGB.g >= whiteLevel || rawRGB.b >= whiteLevel;
    const vec3 balancedRGB = (rawRGB-blackLevel)/(whiteLevel-blackLevel)*whiteBalanceCoefs;
    linearSRGB = vec4(cam2srgb*balancedRGB, float(highlightClipped));
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
uniform float rotationAngle;
out vec2 texCoord;
void main()
{
    const mat2 rot = mat2(cos(rotationAngle), sin(rotationAngle),
                         -sin(rotationAngle), cos(rotationAngle));
    texCoord = rot*(viewportSize*vertex.xy/2 + vec2(-shift.x,shift.y))/(imageSize*scale) + 0.5;
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
uniform bool showClippedHighlights;
uniform bool demosaicedImageInverted;
in vec2 texCoord;
out vec4 color;

vec3 sRGBTransferFunction(const vec3 c)
{
    vec3 s = step(vec3(0.0031308),c);
    return s  * (1.055*pow(c, vec3(1/2.4))-0.055) +
        (1-s) *  12.92*c;
}

void main()
{
    vec2 texcoordToUse = texCoord;
    if(demosaicedImageInverted)
        texcoordToUse.t = 1 - texcoordToUse.t;
    const vec4 linearSRGB = texture(sRGBLinearImage, texcoordToUse);
    color = vec4(sRGBTransferFunction(linearSRGB.rgb*exposureCompensationCoef), 1);
    if(showClippedHighlights)
    {
        if(linearSRGB.w>0)
        {
            color = mod(gl_FragCoord.x-gl_FragCoord.y-1, 6.)>2 ? vec4(0,0,0,1) : vec4(1,1,1,1);
        }
        else if(color.r>1 || color.g>1 || color.b>1)
            color=vec4(1,0,1,1);
    }
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

    glGenTextures(1, &denoisedImageTex_);
    glBindTexture(GL_TEXTURE_2D, denoisedImageTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glGenFramebuffers(1, &denoiseFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, denoiseFBO_);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, denoisedImageTex_, 0);

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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, sizes.width, sizes.height, 0, GL_RGB, GL_FLOAT, nullptr);

        if(tools_->mustReducePepperNoise())
        {
            glBindTexture(GL_TEXTURE_2D, denoisedImageTex_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, sizes.raw_width, sizes.raw_height, 0, GL_RED, GL_FLOAT, nullptr);
        }

        glFinish();
        const auto t1 = currentTime();
        qDebug().nospace() << "Textures uploaded in " << double(t1-t0) << " seconds";
    }

    const auto t0 = currentTime();

    const auto& sizes=libRaw->imgdata.sizes;

    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rawImageTex_);

    const float levelDivisor = libRaw->is_floating_point() ? 1 : 65535;
    if(tools_->mustReducePepperNoise())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, denoiseFBO_);
        glViewport(0, 0, sizes.raw_width, sizes.raw_height);
        denoiseProgram_.bind();
        denoiseProgram_.setUniformValue("image", 0);
        denoiseProgram_.setUniformValue("whiteLevel", float(libRaw->imgdata.rawdata.color.maximum/levelDivisor));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindTexture(GL_TEXTURE_2D, denoisedImageTex_);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, demosaicFBO_);
    glViewport(0, 0, sizes.width, sizes.height);
    demosaicProgram_.bind();
    demosaicProgram_.setUniformValue("image", 0);
    {
        demosaicedImageInverted_ = libRaw->imgdata.idata.cdesc[libRaw->COLOR(0,1)] != 'G' ||
                                   libRaw->imgdata.idata.cdesc[libRaw->COLOR(1,0)] != 'G';
        demosaicProgram_.setUniformValue("verticalInversion", demosaicedImageInverted_);

        const char topLeftCF = demosaicedImageInverted_ ? libRaw->imgdata.idata.cdesc[libRaw->COLOR(1,0)]
                                                        : libRaw->imgdata.idata.cdesc[libRaw->COLOR(0,0)];
        if(topLeftCF == 'R')
            demosaicProgram_.setUniformValue("RED_FIRST", true);
        else
            demosaicProgram_.setUniformValue("RED_FIRST", false);
    }
    const float blackLevel=getBlackLevel();
    demosaicProgram_.setUniformValue("blackLevel", float(blackLevel/levelDivisor));
    demosaicProgram_.setUniformValue("whiteLevel", float(libRaw->imgdata.rawdata.color.maximum/levelDivisor));
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
        demosaicProgram_.setUniformValue("cam2srgb",
                                         tools_->mustTransformToSRGB() ? QMatrix3x3(cam2srgb) : QMatrix3x3{});
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
    oldDemosaicedImagePresent_ = true;
    emit fileLoadingFinished();
}

void ImageCanvas::resizeGL([[maybe_unused]] const int w, [[maybe_unused]] const int h)
{
    if(!libRaw) return;
    emit zoomChanged(scale());
}

void ImageCanvas::wheelEvent(QWheelEvent*const event)
{
    if(event->modifiers() & (Qt::ShiftModifier|Qt::AltModifier))
        return;

    if(event->modifiers() & Qt::ControlModifier)
    {
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
    else
    {
        if(event->angleDelta().y() < 0)
            emit nextFileRequested();
        else
            emit prevFileRequested();
    }
}

void ImageCanvas::mouseMoveEvent(QMouseEvent*const event)
{
    if(!libRaw) return;

    if(dragging_)
    {
        imageShift_ += event->pos() - dragStartPos_;
        dragStartPos_ = event->pos();
        update();
    }
    else
    {
        const QSizeF canvasSize = size();
        const QSizeF imageSize(libRaw->imgdata.sizes.width, libRaw->imgdata.sizes.height);
        const QPointF cursor = event->pos();
        const double scale = this->scale();

        const auto centeredPos = (canvasSize - imageSize*scale)/2;
        const QRectF centeredRect{QPointF(centeredPos.width(),centeredPos.height()), imageSize*scale};
        const QRectF imageRect{centeredRect.topLeft()+imageShift_, centeredRect.size()};
        const auto p = (cursor-imageRect.topLeft())/scale;

        emit cursorPositionUpdated(p.x(),p.y());
    }
}

void ImageCanvas::mousePressEvent(QMouseEvent*const event)
{
    if(!libRaw) return;

    dragStartPos_ = event->pos();
    dragging_ = true;
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent*)
{
    if(!libRaw) return;

    dragging_ = false;
}

void ImageCanvas::leaveEvent(QEvent*)
{
    emit cursorLeft();
}

void ImageCanvas::keyPressEvent(QKeyEvent*const event)
{
    const auto mods = event->modifiers() & (Qt::ControlModifier|Qt::ShiftModifier|Qt::AltModifier);
    switch(event->key())
    {
    case Qt::Key_Z:
        if(mods) return;
        scaleSteps_ = 0.;
        emit zoomChanged(scale());
        break;
    case Qt::Key_X:
        if(mods) return;
        scaleSteps_.reset();
        imageShift_ = QPoint(0,0);
        emit zoomChanged(scale());
        break;
    case Qt::Key_C:
        if(mods) return;
        imageShift_ = QPoint(0,0);
        break;
    case Qt::Key_F:
    case Qt::Key_F11:
        if(mods) return;
        emit fullScreenToggleRequested();
        return;
    case Qt::Key_PageDown:
        if(mods) return;
        emit nextFileRequested();
        break;
    case Qt::Key_PageUp:
        if(mods) return;
        emit prevFileRequested();
        break;
    case Qt::Key_Home:
        if(mods) return;
        emit firstFileRequested();
        break;
    case Qt::Key_End:
        if(mods) return;
        emit lastFileRequested();
        break;
    case Qt::Key_S:
        if(mods == Qt::ControlModifier && demosaicedImageReady_)
        {
            const auto path = QFileDialog::getSaveFileName(this, "Save file as...", {}, "PNG images (*.png)");
            if(path.isEmpty()) return;

            makeCurrent();
            glBindTexture(GL_TEXTURE_2D, demosaicedImageTex_);

            const int W = libRaw->imgdata.sizes.width;
            const int H = libRaw->imgdata.sizes.height;
            QImage img(W, H, QImage::Format_RGBA8888);
            std::vector<GLfloat> data(4*W*H);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, data.data());

            const auto coef = std::pow(10., tools_->exposureCompensation());
            const auto p = img.bits();
            const int stride = img.bytesPerLine() / sizeof p[0];
            for(int j = 0; j < H; ++j)
            {
                for(int i = 0; i < W; ++i)
                {
                    p[(H-1-j)*stride + 4*i + 0] = 255*sRGBTransferFunction(std::clamp(data[4*(j*W+i)+0]*coef, 0., 1.));
                    p[(H-1-j)*stride + 4*i + 1] = 255*sRGBTransferFunction(std::clamp(data[4*(j*W+i)+1]*coef, 0., 1.));
                    p[(H-1-j)*stride + 4*i + 2] = 255*sRGBTransferFunction(std::clamp(data[4*(j*W+i)+2]*coef, 0., 1.));
                    p[(H-1-j)*stride + 4*i + 3] = 255;
                }
            }
            if(!img.save(path))
                QMessageBox::critical(this, "Error saving image", "Failed to save the image");
        }
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

void ImageCanvas::renderLastValidImage()
{
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
    displayProgram_.setUniformValue("rotationAngle", float(M_PI/180*tools_->rotationAngle()));
    displayProgram_.setUniformValue("showClippedHighlights", tools_->clippedHighlightsMarkingEnabled());
    displayProgram_.setUniformValue("exposureCompensationCoef", float(std::pow(10., tools_->exposureCompensation())));
    displayProgram_.setUniformValue("demosaicedImageInverted", demosaicedImageInverted_);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void ImageCanvas::paintGL()
{
    if(!isVisible()) return;
    if(!demosaicedImageReady_)
        demosaicImage();
    renderLastValidImage();
}

void ImageCanvas::paintEvent(QPaintEvent*const event)
{
    if(!libRaw)
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignHCenter|Qt::AlignVCenter, tr("(nothing to display)"));
        return;
    }
    if(tools_->previewMode() && !preview_.isNull())
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), Qt::black);
        const auto centeredPos = (size() - preview_.size()*scale())/2;
        const QRect centeredRect(QPoint(centeredPos.width(),centeredPos.height()), preview_.size()*scale());
        p.drawImage(QRect(centeredRect.topLeft()+imageShift_, centeredRect.size()), preview_, preview_.rect());
        return;
    }
    if(!fileLoadStatus_.isFinished())
    {
        if(oldDemosaicedImagePresent_)
        {
            renderLastValidImage();
        }
        else
        {
            QPainter p(this);
            p.fillRect(rect(), Qt::black);
            p.setPen(Qt::gray);
            p.drawText(rect(), Qt::AlignHCenter|Qt::AlignVCenter, tr("Loading file..."));
        }
        emit zoomChanged(scale());
        return;
    }
    if(!demosaicStarted_)
    {
        if(oldDemosaicedImagePresent_)
        {
            renderLastValidImage();
        }
        else
        {
            QPainter p(this);
            p.fillRect(rect(), Qt::black);
            p.setPen(Qt::gray);
            p.drawText(rect(), Qt::AlignHCenter|Qt::AlignVCenter, tr("Demosaicing image..."));
        }
        demosaicStarted_ = true;
        // And repaint, but avoid possible recursive call of paintEvent
        QTimer::singleShot(0, [this]{update();});
        return;
    }
    QOpenGLWidget::paintEvent(event);
}
