#include "glad/glad.h"
#include "FrameView.h"
#include <QWheelEvent>
#include <QGLFormat>
#include <iostream>
#include <cassert>
#include <cstring>
#include "util.h"

using namespace glm;

QGLFormat getGLFormat()
{
    QGLFormat fmt;
    fmt.setVersion(3,3);
    fmt.setProfile(QGLFormat::CoreProfile);
    return fmt;
}

FrameView::FrameView(QWidget* parent)
    : QGLWidget(getGLFormat(),parent)
{
}

static void printInfoLog(bool statusOK,
                         GLuint object,
                         PFNGLGETSHADERIVPROC getLogLength,
                         PFNGLGETSHADERINFOLOGPROC getLog,
                         std::string const& shaderName,
                         const char* operation)
{
    GLint infoLogLength;
    getLogLength(object,GL_INFO_LOG_LENGTH,&infoLogLength);
    if(statusOK && infoLogLength<=1) return;

    std::string infoLog(infoLogLength,0);
    getLog(object,infoLogLength,NULL,&infoLog[0]);

    assert(infoLogLength-1ul==std::strlen(infoLog.c_str()));
    infoLog.resize(infoLogLength-1); // remove null terminator

    if(statusOK)
        std::cerr << "Shader " << (shaderName.empty() ? "" : '"'+shaderName+"\" ") << operation << " info log:\n" << infoLog << "\n";
    else
        std::cerr << "GLSL shader " << (shaderName.empty() ? "" : '"'+shaderName+"\" ") << operation << " failed. Info log:\n" << infoLog << "\n";
}

static GLuint makeShader(GLenum type, std::string const& srcStr, std::string const& name)
{
    const auto shader=glCreateShader(type);
    const GLint srcLen=srcStr.size();
    const GLchar*const src=srcStr.c_str();
    glShaderSource(shader, 1, &src, &srcLen);
    glCompileShader(shader);
    GLint status=-1;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    assert(glGetError()==GL_NO_ERROR);
    printInfoLog(status==1,shader,glGetShaderiv,glGetShaderInfoLog,name,"compilation");
    if(status!=1) exit(3);
    return shader;
}

static GLuint makeShaderProgram(std::string const& vertexShaderSrc, std::string const& fragmentShaderSrc, std::string const& name)
{
    const auto program=glCreateProgram();

    const auto vertexShader=makeShader(GL_VERTEX_SHADER, vertexShaderSrc, "main vertex shader");
    glAttachShader(program, vertexShader);

    const auto fragmentShader=makeShader(GL_FRAGMENT_SHADER, fragmentShaderSrc, "main fragment shader");
    glAttachShader(program, fragmentShader);

    glLinkProgram(program);
    GLint status=0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    assert(glGetError()==GL_NO_ERROR);
    printInfoLog(status==1,program,glGetProgramiv,glGetProgramInfoLog,name,"linking");
    if(status!=1) exit(4);

    glDetachShader(program, fragmentShader);
    glDeleteShader(fragmentShader);

    glDetachShader(program, vertexShader);
    glDeleteShader(vertexShader);

    return program;
}

void FrameView::initShaders()
{
    mainProgram=makeShaderProgram(1+R"(
#version 330
in vec4 vertex;
out vec2 position;
void main()
{
    gl_Position=vertex;
    position=(vertex.xy+vec2(1))/2;
}
)",
1+R"(
#version 330

uniform sampler2D tex;
uniform float scale;
uniform bool markOverexposures;

in vec2 position;
out vec3 color;

vec3 toSRGB(vec3 linearColor)
{
    return pow(clamp(linearColor,0,1),vec3(1/2.2));
}

void main()
{
    vec3 pix=texture(tex, vec2(position.x,1-position.y)).rgb;
    vec3 linearColor=pix*scale;
    if(markOverexposures && (linearColor.r>1 || linearColor.g>1 || linearColor.b>1))
        linearColor=vec3(0,1,1);
    color=toSRGB(linearColor);
}
)", "main program");

    selectionDrawProgram=makeShaderProgram(1+R"(
#version 330
in vec4 vertex;
uniform mat4 mvp;
void main()
{
    gl_Position=mvp*vertex;
}
)",
1+R"(
#version 330

in vec2 position;
out vec4 color;

void main()
{
    color=vec4(1,1,1,0.1);
}
)", "selection drawing program");
}

void FrameView::setupBuffers()
{
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
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

void FrameView::initTextures()
{
    glGenTextures(1,&tex);
}

void FrameView::initializeGL()
{
    if(!gladLoadGL())
    {
        qFatal("GLAD initialization failed\n");
    }
    if(!GLAD_GL_VERSION_3_0)
        qFatal("OpenGL 3.0 or higher is required");
    setupBuffers();
    initShaders();
    initTextures();

    if(imageDataToLoad.isEmpty())
        clear();
}

static void calcAverageAndMaxSelectedPixels(vec3 const*const data, const int width, const int height,
                                            const ivec2 selectionPointA, const ivec2 selectionPointB,
                                            vec3& average, vec3& max)
{
    const auto iMin=std::min(selectionPointA.x,selectionPointB.x);
    const auto iMax=std::max(selectionPointA.x,selectionPointB.x);
    const auto jMin=std::min(selectionPointA.y,selectionPointB.y);
    const auto jMax=std::max(selectionPointA.y,selectionPointB.y);

    vec3 sum(0), maxval(0);
    for(int j=0;j<height;++j)
    {
        if(j>jMax || j<jMin) continue;
        for(int i=0;i<width;++i)
        {
            if(i>iMax || i<iMin) continue;
            sum+=data[j*width+i];
            maxval=glm::max(maxval,data[j*width+i]);
        }
    }
    average=sum/(float(iMax-iMin+1)*(jMax-jMin+1));
    max=maxval;
}

void FrameView::showImage(QVector<vec3> const& data, int width, int height)
{
    imgWidth=width;
    imgHeight=height;
    imageDataToLoad=data;
    imageNeedsUploading=true;
    updateSelectedPixelsInfo();
    update();
}

void FrameView::setScale(double newScale)
{
    scale=newScale;
    update();
}

void FrameView::setMarkOverexposure(bool enable)
{
    overexposureMarkingEnabled=enable;
    update();
}

void FrameView::setNormalizationMode(NormalizationMode mode)
{
    normalizationMode=mode;
    update();
}

void FrameView::clear()
{
    const auto color=vec3(0.5,0.5,0.5); // FIXME: choose a better color, maybe take it from theme
    showImage({color}, 1,1);
}

void FrameView::paintGL()
{
    glClearColor(0,0,0,1); // TODO: use mean color of the image;
    glClear(GL_COLOR_BUFFER_BIT);

    if(imageNeedsUploading)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F, imgWidth,imgHeight, 0,GL_RGB, GL_FLOAT, imageDataToLoad.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
        imageNeedsUploading=false;
    }
    {
        const auto imgAspect=float(imgWidth)/imgHeight;
        float width=this->width(), height=this->height();
        float x=0, y=0;
        if(float(width)/height>imgAspect)
        {
            const auto newWidth=height*imgAspect;
            x=(width-newWidth)/2.f;
            width=newWidth;
        }
        else
        {
            const auto newHeight=width/imgAspect;
            y=(height-newHeight)/2.f;
            height=newHeight;
        }
        glViewport(x,y,width,height);
    }

    glUseProgram(mainProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(mainProgram,"tex"),0);
    float normalizationCoef=1;
    switch(normalizationMode)
    {
    case NormalizationMode::None:
        normalizationCoef=1;
        break;
    case NormalizationMode::DivideByMax:
        normalizationCoef=1/max(maxFromSelectedPixels);
        break;
    case NormalizationMode::DivideByAverage:
        normalizationCoef=1/max(averageOfSelectedPixels);
        break;
    }
    glUniform1f(glGetUniformLocation(mainProgram,"scale"),scale*normalizationCoef);
    glUniform1i(glGetUniformLocation(mainProgram,"markOverexposures"),overexposureMarkingEnabled);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Same VAO for selection rectangles, but with a different program
    glUseProgram(selectionDrawProgram);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for(const auto& selection : selections)
    {
        if(selection.pointA!=selection.pointB)
        {
            const auto imgSize=vec2(imgWidth,imgHeight);
            const auto pA=vec2(selection.pointA)*2.f/imgSize-vec2(1,1);
            const auto pB=vec2(selection.pointB)*2.f/imgSize-vec2(1,1);
            const auto s=(pB-pA)/2.f, t=(pA+pB)/2.f;
            glUniformMatrix4fv(glGetUniformLocation(selectionDrawProgram,"mvp"),1,true,&mat4(vec4(s.x, 0  , 0, t.x),
                                                                                             vec4(0  ,-s.y, 0,-t.y),
                                                                                             vec4(0  , 0  , 1, 0  ),
                                                                                             vec4(0  , 0  , 0, 1  ))[0][0]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

vec2 FrameView::screenPosToImagePixelPos(vec2 posScr) const
{
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const auto posVP=vec2(viewport[0],viewport[1]);
    const auto sizeVP=vec2(viewport[2],viewport[3]);
    const auto sizeImg=vec2(imgWidth,imgHeight);
    return (posScr-posVP)*sizeImg/sizeVP;
}

void FrameView::addSelectionRectangle(QPoint const& firstPoint)
{
    const auto p=screenPosToImagePixelPos(vec2(firstPoint.x(),firstPoint.y()));
    selections.emplace_back(Selection{p,p});
}

void FrameView::updateLastSelectionRectangle(QPoint const& pointB)
{
    assert(!selections.empty());
    selections.back().pointB=screenPosToImagePixelPos(vec2(pointB.x(),pointB.y()));
    update();
}

void FrameView::wheelEvent(QWheelEvent* event)
{
    emit wheelScrolled(event->angleDelta().y(), event->modifiers());
}

void FrameView::mousePressEvent(QMouseEvent* event)
{
    if(!(event->modifiers()&Qt::ControlModifier))
    {
        selections.clear();
        emit selectionsRemoved();
    }

    if(event->buttons()==Qt::LeftButton)
    {
        addSelectionRectangle(event->pos());
        dragging=true;
    }
}

void FrameView::updateSelectedPixelsInfo()
{
    gatherSelectedPixelsInfo(imageDataToLoad.data(), imgWidth, imgHeight,
                             maxFromSelectedPixels,averageOfSelectedPixels);
}

void FrameView::gatherSelectedPixelsInfo(vec3 const* imageData, int imgWidth, int imgHeight, vec3& maxFromSelectedPixels, vec3& averageOfSelectedPixels) const
{
    auto sumOfAverages=vec3(0);
    int processedCount=0;
    auto totalMax=vec3(0);
    for(const auto& selection : selections)
    {
        if(selection.pointA!=selection.pointB)
        {
            vec3 average, max;
            calcAverageAndMaxSelectedPixels(imageData,imgWidth,imgHeight,
                                            selection.pointA,selection.pointB,
                                            average,max);
            sumOfAverages+=average;
            totalMax=glm::max(max,totalMax);
            ++processedCount;
        }
    }

    if(processedCount)
    {
        maxFromSelectedPixels=totalMax;
        averageOfSelectedPixels=sumOfAverages/float(processedCount);
    }
    else
    {
        // No-op divisors
        maxFromSelectedPixels=vec3(1,1,1);
        averageOfSelectedPixels=vec3(1,1,1);
    }
}

void FrameView::mouseReleaseEvent(QMouseEvent* event)
{
    if(!(event->buttons()&Qt::LeftButton)) // the button should be released, so its state should be 0
    {
        updateLastSelectionRectangle(event->pos());
        emit selectionAdded(selections.back().pointA, selections.back().pointB);
        dragging=false;
        updateSelectedPixelsInfo();
    }
}

void FrameView::mouseMoveEvent(QMouseEvent* event)
{
    if(dragging)
        updateLastSelectionRectangle(event->pos());
}
