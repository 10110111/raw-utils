#include "ImageCanvas.hpp"
#include <cmath>
#include <QDebug>
#include <QMessageBox>
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

ImageCanvas::ImageCanvas(QString const& filename, ToolsWidget* tools, QWidget* parent)
    : QOpenGLWidget(parent)
    , tools_(tools)
{
    setFormat(makeFormat());

    if(!filename.isEmpty())
        loadFile(filename);

    connect(tools_, &ToolsWidget::settingChanged, this, qOverload<>(&QWidget::update));
}

void ImageCanvas::loadFile(QString const& filename)
{
    libRaw.open_file(filename.toStdString().c_str());
    if(const auto error=libRaw.unpack())
    {
        QMessageBox::critical(this, tr("Error loading file"), tr("Failed to unpack file: %1").arg(libraw_strerror(error)));
        return;
    }
    libRaw.raw2image();
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
    const char*const vertSrc = 1+R"(
#version 330
in vec3 vertex;
void main()
{
    gl_Position=vec4(vertex,1);
}
)";
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc))
       QMessageBox::critical(this, tr("Shader compile failure"), tr("Failed to compile %1:\n%2").arg("vertex shader").arg(program_.log()));
    const char*const fragSrc = 1+R"(
#version 330
#extension GL_ARB_shading_language_420pack : require
uniform sampler2D image;
uniform float blackLevel, whiteLevel, exposureCompensationCoef;
uniform vec3 whiteBalanceCoefs;
uniform mat3 cam2srgb;
out vec4 color;

vec3 sRGBTransferFunction(const vec3 c)
{
    return step(0.0031308,c)*(1.055*pow(c, vec3(1/2.4))-0.055)+step(-0.0031308,-c)*12.92*c;
}

float sum(vec4 c)
{
    return c.x+c.y+c.z+c.w;
}

float samplePhotoSite(const vec2 pos, const vec2 offset)
{
    const vec2 texCoord = (pos+0.5+offset)/textureSize(image,0).xy;
    return sum(texture(image, texCoord));
}

// FIXME: hard-coded RGGB pattern
#define BAYER_RED    0
#define BAYER_GREEN1 1
#define BAYER_GREEN2 2
#define BAYER_BLUE   3

void main()
{
    const int W = textureSize(image,0).x;
    const int H = textureSize(image,0).y;
    const vec2 pos = vec2(gl_FragCoord.x, H-gl_FragCoord.y)-0.5;
    const float vtl = samplePhotoSite(pos, vec2(-1,-1));
    const float vtc = samplePhotoSite(pos, vec2( 0,-1));
    const float vtr = samplePhotoSite(pos, vec2(+1,-1));
    const float vcl = samplePhotoSite(pos, vec2(-1, 0));
    const float vcc = samplePhotoSite(pos, vec2( 0, 0));
    const float vcr = samplePhotoSite(pos, vec2(+1, 0));
    const float vbl = samplePhotoSite(pos, vec2(-1,+1));
    const float vbc = samplePhotoSite(pos, vec2( 0,+1));
    const float vbr = samplePhotoSite(pos, vec2(+1,+1));
    const int photositeColorFilter = int(mod(pos.y, 2)*2 + mod(pos.x, 2));
    float red=0, green=0, blue=0;
    if(photositeColorFilter == BAYER_RED)
    {
        red = vcc;
        if(pos.x==0 && pos.y==0)
        {
            green = (vcr+vbc)/2;
            blue = vbr;
        }
        else if(pos.y==0)
        {
            green = (vcr+vbc+vcl)/3;
            blue = (vbl+vbr)/2;
        }
        else if(pos.x==0)
        {
            green = (vtc+vcr+vbc)/3;
            blue = (vtr+vbr)/2;
        }
        else
        {
            green = (vtc+vcr+vbc+vcl)/4;
            blue = (vtl+vtr+vbl+vbr)/4;
        }
    }
    else if(photositeColorFilter == BAYER_GREEN1)
    {
        green = vcc;
        if(pos.y==0 && pos.x==W-1)
        {
            red = vcl;
            blue = vbc;
        }
        else if(pos.y==0)
        {
            red = (vcl+vcr)/2;
            blue = vbc;
        }
        else if(pos.x==W-1)
        {
            red = vcl;
            blue = (vtc+vbc)/2;
        }
        else
        {
            red = (vcl+vcr)/2;
            blue = (vtc+vbc)/2;
        }
    }
    else if(photositeColorFilter == BAYER_GREEN2)
    {
        green = vcc;
        if(pos.x==0 && pos.y==H-1)
        {
            red = vtc;
            blue = vcr;
        }
        else if(pos.x==0)
        {
            red = (vtc+vbc)/2;
            blue = vcr;
        }
        else if(pos.y==H-1)
        {
            red = vtc;
            blue = (vcl+vcr)/2;
        }
        else
        {
            red = (vtc+vbc)/2;
            blue = (vcl+vcr)/2;
        }
    }
    else if(photositeColorFilter == BAYER_BLUE)
    {
        blue = vcc;
        if(pos.y==H-1 && pos.x==W-1)
        {
            red = vtl;
            green = (vtc+vcl)/2;
        }
        else if(pos.y==H-1)
        {
            red = (vtl+vtr)/2;
            green = (vtc+vcr+vcl)/3;
        }
        else if(pos.x==W-1)
        {
            red = (vtl+vbl)/2;
            green = (vtc+vbc+vcl)/3;
        }
        else
        {
            red = (vtl+vtr+vbl+vbr)/4;
            green = (vtc+vcr+vbc+vcl)/4;
        }
    }
    const vec3 rawRGB = vec3(red,green,blue);
    const vec3 balancedRGB = (rawRGB-blackLevel)/(whiteLevel-blackLevel)*whiteBalanceCoefs;
    const vec3 linearSRGB = cam2srgb*balancedRGB;
    color = vec4(sRGBTransferFunction(linearSRGB*exposureCompensationCoef), 1);
}
)";
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc))
       QMessageBox::critical(this, tr("Error compiling shader"), tr("Failed to compile %1:\n%2").arg("fragment shader").arg(program_.log()));
    if(!program_.link())
        throw QMessageBox::critical(this, tr("Error linking shader program"), tr("Failed to link %1:\n%2").arg("shader program").arg(program_.log()));
}

void ImageCanvas::initializeGL()
{
    if(!initializeOpenGLFunctions())
    {
        QMessageBox::critical(this, tr("Error initializing OpenGL"), tr("Failed to initialize OpenGL %1.%2 functions")
                                    .arg(OPENGL_MAJOR_VERSION)
                                    .arg(OPENGL_MINOR_VERSION));
        return;
    }

    setupBuffers();

    glGenTextures(1, &imageTex_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, imageTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    if(libRaw.imgdata.image)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, libRaw.imgdata.sizes.iwidth, libRaw.imgdata.sizes.iheight,
                     0, GL_RGBA, GL_UNSIGNED_SHORT, libRaw.imgdata.image);
    }
    else
    {
        constexpr float texel[4]={1,0.5,0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGB, GL_FLOAT, texel);
    }

    setupShaders();
}

ImageCanvas::~ImageCanvas()
{
    makeCurrent();
    glDeleteTextures(1, &imageTex_);
}

void ImageCanvas::paintGL()
{
    if(!isVisible()) return;

    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, imageTex_);
    program_.bind();
    program_.setUniformValue("image", 0);
    float blackLevel=0;
    if(libRaw.imgdata.rawdata.color.black)
    {
        blackLevel = libRaw.imgdata.rawdata.color.black;
    }
    else
    {
        const auto& cblack = libRaw.imgdata.rawdata.color.cblack;
        const auto dimX=cblack[4], dimY=cblack[5];
        if(dimX==2 && dimY==2 && cblack[6]==cblack[7] && cblack[6]==cblack[8] && cblack[6]==cblack[9])
            blackLevel = cblack[6];
        else if(dimX==0 && dimY==0)
            qWarning().nospace() << "Warning: black level is zero\n";
        else
            qWarning().nospace() << "Warning: unexpected configuration of black level information: dimensions " << dimX << "×" << dimY << ", data: " << cblack[6] << ", " << cblack[7] << ", " << cblack[8] << ", " << cblack[9] << ", ...\n";
    }
    program_.setUniformValue("blackLevel", float(blackLevel/65535.));
    program_.setUniformValue("whiteLevel", float(libRaw.imgdata.rawdata.color.maximum/65535.));
    {
        const auto& pre_mul=libRaw.imgdata.color.pre_mul;
        const float preMulMax=*std::max_element(std::begin(pre_mul),std::end(pre_mul));
        const auto daylightWBCoefs = QVector3D(pre_mul[0],pre_mul[1],pre_mul[2])/preMulMax;
        program_.setUniformValue("whiteBalanceCoefs", daylightWBCoefs);
    }
    {
        const auto& camrgb = libRaw.imgdata.rawdata.color.rgb_cam;
        const float cam2srgb[9] = {camrgb[0][0], camrgb[0][1], camrgb[0][2],
                                   camrgb[1][0], camrgb[1][1], camrgb[1][2],
                                   camrgb[2][0], camrgb[2][1], camrgb[2][2]};
        program_.setUniformValue("cam2srgb", QMatrix3x3(cam2srgb));
    }

    program_.setUniformValue("exposureCompensationCoef", float(std::pow(10., tools_->exposureCompensation())));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}
