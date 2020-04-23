#include <libraw/libraw.h>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <sstream>
#include <vector>
#include <cmath>
#define cimg_use_tiff
#define cimg_display 0
#include <CImg.h>

using std::uint8_t;
using std::size_t;

bool needTrueSRGB=false;
bool needFakeSRGB=false;
bool needChromaOnlyFile=false;
bool needCombinedFile=false;
bool needTIFFFile=false;
bool needUnweightedTIFF=false;
bool needF32=false;
bool needRedFile=false;
bool needGreen1File=false;
bool needGreen2File=false;
bool needBlueFile=false;

float pixelScale=1;
std::string filePathPrefix="/tmp/outfile-";

inline int usage(const char* argv0, int returnValue)
{
    std::cerr << "Usage: [options...] " << argv0 << " filename\n";
    std::cerr << "Options:\n"
              << "  -srgb,--srgb        Create an sRGB image by merging RGGB data and applying the cam2rgb conversion matrix\n"
              << "  -chroma,--chroma    Create an sRGB image by merging RGGB data applying the cam2rgb\n"
                 "                       conversion matrix and stripping brightness info\n"
              << "  --fake-srgb         Similar to -srgb, but without applying the conversion matrix\n"
              << "  --combined          Create a file containing RGGB data on the Bayer grid, coded by sRGB colors\n"
              << "  --tiff              Create a floating-point TIFF RGB file containing merged RGGB data from the Bayer grid,\n"
                 "                       with green being average of the two Bayer values. Color space is sRGB-linear, values are\n"
                 "                       normalized to maximum possible value of the raw file (taken from libRaw).\n"
              << "  --tiff-unw          Same as --tiff, but without applying cam_rgb matrix and without white balancing - only\n"
                 "                       averaging the two Bayer green channels.\n"
              << "  --f32               Save as floating-point single-component texture with header being uint16 width & height\n"
              << "  -r,--red            Create a file with red channel only data on the Bayer grid\n"
              << "  -g1,--green1        Create a file with data only from first green channel on the Bayer grid\n"
              << "  -g2,--green2        Create a file with data only from second green channel on the Bayer grid\n"
              << "  -b,--blue           Create a file with blue channel only data on the Bayer grid\n"
              << "  -s R,--scale R      Scale pixel values by factor R\n"
              << "  -p,--prefix PATH    Use PATH as file path prefix instead of \"outfile-\"\n"
              << "  -wb,--white-balance {as-shot|daylight|none}  Use the white balance mode specified. 'as-shot' means the white\n"
                 "                       balance chosen by the camera (cam_mul in libraw), 'daylight' is the daylight WB (pre_mul\n"
                 "                       in libraw), and 'none' means the coefficients will be all equal to one.\n"
              << "  --cam2srgb M11,M12,M13,M21,M22,M23,M31,M32,M33\n"
                 "                       Use the custom camera-to-sRGB matrix. White balance options will be ignored.\n"
              ;
    return returnValue;
}

bool parseMatrix(float* data, const unsigned size, std::string const& line)
{
    std::istringstream s(line);
    for(unsigned i=0;i<size;++i)
    {
        s >> data[i];
        if(!s) return false;
        if(i+1<size && s.get()!=',') return false;
    }
    return true;
}

void writeF32(LibRaw& libRaw, ushort (*img)[4], const uint16_t w, const uint16_t h, const unsigned blackLevel)
{
    const auto filename=filePathPrefix+".f32";
    std::cerr << "Writing float32 data to file...";
    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&w), sizeof w);
    file.write(reinterpret_cast<const char*>(&h), sizeof h);
    for(int y=0;y<h;++y)
    {
        for(int x=0;x<w;++x)
        {
            const auto colIndex=libRaw.FC(y,x);
            const float pixelRaw=img[x+y*w][colIndex];
            const float pixel=pixelRaw-blackLevel;
            file.write(reinterpret_cast<const char*>(&pixel), sizeof pixel);
        }
    }
    if(file.flush())
        std::cerr << " written to \"" << filename << "\"\n";
    else
        std::cerr << " failed to write to \"" << filename << "\"\n";
}

#pragma pack(push,1)
struct BitmapHeader
{
    uint16_t signature; // 0x4d42
    uint32_t fileSize;
    uint32_t zero;
    uint32_t dataOffset;
    uint32_t bitmapInfoHeaderSize; // 40
    uint32_t width;
    uint32_t height;
    uint16_t numOfPlanes; // 1
    uint16_t bpp;
    uint32_t compressionType; // none is 0
    uint32_t dataSize;
    uint32_t horizPPM;
    uint32_t vertPPM;
    uint32_t numOfColors;
    uint32_t numOfImportantColors;
};
#pragma pack(pop)

class ByteBuffer
{
    using size_type=std::vector<uint8_t>::size_type;
    std::vector<uint8_t> bytes;
    size_type offset=0;
public:
    ByteBuffer(size_type count)
        : bytes(count)
    {
    }
    void resize(size_type count)
    {
        bytes.resize(count);
    }
    void write(const void* data, size_type count)
    {
        assert(offset+count<=bytes.size());
        std::memcpy(bytes.data()+offset,data,count);
        offset+=count;
    }
    size_type size() const { return bytes.size(); }
    size_type tellp() const { return offset; }
    const char* data() const { return reinterpret_cast<const char*>(bytes.data()); }
};

float clampRGB(float x){return std::max(0.f,std::min(1.f,x));}
float toSRGB(float x){return std::pow(x,1/2.2f)*255;}
void writeImagePlanesToBMP(ushort (*data)[4], const int w, const int h, const float (&rgbCoefs)[4], libraw_colordata_t const& colorData, unsigned whiteLevel)
{
    const unsigned black=colorData.black, white=whiteLevel;
    BitmapHeader header={};
    header.signature=0x4d42;
    header.fileSize=((w+3)&~3)*h*3+sizeof header;
    header.dataOffset=sizeof header;
    header.bitmapInfoHeaderSize=40;
    header.width=w;
    header.height=-h;
    header.numOfPlanes=1;
    header.bpp=24;

    const auto col=[black,white](float p)->uint8_t
        {
            return toSRGB(clampRGB(pixelScale*p/(white-black)));
        };
    const auto alignScanLine=[](ByteBuffer& bytes)
    {
        constexpr auto scanLineAlignment=4;
        const char align[scanLineAlignment-1]={};
        const auto alignSize=(sizeof header-bytes.tellp())%scanLineAlignment;
        bytes.write(align,alignSize);
    };
    const auto clampAndSubB=[black,white](ushort p, bool& overexposed)
        {return (p>white-10 ? overexposed=true,white : p<black ? black : p)-black; };
    const auto rgbCoefR =rgbCoefs[0];
    const auto rgbCoefG1=rgbCoefs[1];
    const auto rgbCoefB =rgbCoefs[2];
    const auto rgbCoefG2=rgbCoefs[3];
#define WRITE_BMP_DATA_TO_FILE(ANNOTATION,FILENAME,BLUE,GREEN,RED)  \
    do {                                                            \
        std::cerr << ANNOTATION;                                    \
        ByteBuffer bytes(header.fileSize);                          \
        bytes.write(&header,sizeof header);                         \
        for(int y=0;y<h;++y)                                        \
        {                                                           \
            for(int x=0;x<w;++x)                                    \
            {                                                       \
                bool overexposed=false;                             \
                const auto pixelR =rgbCoefR *clampAndSubB(data[x+y*w][0],overexposed);    \
                const auto pixelG1=rgbCoefG1*clampAndSubB(data[x+y*w][1],overexposed);    \
                const auto pixelB =rgbCoefB *clampAndSubB(data[x+y*w][2],overexposed);    \
                const auto pixelG2=rgbCoefG2*clampAndSubB(data[x+y*w][3],overexposed);    \
                const uint8_t vals[3]={overexposed?uint8_t(255):BLUE,   \
                                       overexposed?uint8_t(255):GREEN,  \
                                       overexposed?uint8_t(255):RED};   \
                bytes.write(vals,sizeof vals);                      \
            }                                                       \
            alignScanLine(bytes);                                   \
        }                                                           \
        std::ofstream file(FILENAME,std::ios::binary);              \
        file.write(bytes.data(),bytes.size());                      \
        std::cerr << " written to \"" << FILENAME << "\"\n";        \
    } while(0)

#define WRITE_TIFF_DATA_TO_FILE(ANNOTATION,FILENAME,WIDTH,HEIGHT)                                                           \
    do {                                                                                                                    \
        std::cerr << ANNOTATION;                                                                                            \
        const int stride=WIDTH;                                                                                             \
        const auto W=WIDTH/2, H=HEIGHT/2;                                                                                   \
        cimg_library::CImg<float> image(W,H, 1,3);                                                                          \
        float* pixels=image.data();                                                                                         \
        const float identity[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};                                                         \
        const auto subBlack=[black](ushort p) {return (p<black ? black : p)-black; };                                       \
        for(int y=0;y<H;++y)                                                                                                \
            for(int x=0;x<W;++x)                                                                                            \
            {                                                                                                               \
                const auto X=x*2, Y=y*2;                                                                                    \
                bool overexposed=false;                                                                                     \
                const ushort pixelTopLeft    =rgbCoefR *subBlack(data[X+0+(Y+0)*stride][BAYER_RED]   );                     \
                const ushort pixelTopRight   =rgbCoefG1*subBlack(data[X+1+(Y+0)*stride][BAYER_GREEN1]);                     \
                const ushort pixelBottomLeft =rgbCoefG2*subBlack(data[X+0+(Y+1)*stride][BAYER_GREEN2]);                     \
                const ushort pixelBottomRight=rgbCoefB *subBlack(data[X+1+(Y+1)*stride][BAYER_BLUE]  );                     \
                const auto green=(pixelTopRight+pixelBottomLeft)/2.;                                                        \
                const auto red=pixelTopLeft, blue=pixelBottomRight;                                                         \
                const auto& cam2srgb = needUnweightedTIFF ? identity : colorData.rgb_cam;                                   \
                const auto srgblR=cam2srgb[0][0]*red+cam2srgb[0][1]*green+cam2srgb[0][2]*blue;                              \
                const auto srgblG=cam2srgb[1][0]*red+cam2srgb[1][1]*green+cam2srgb[1][2]*blue;                              \
                const auto srgblB=cam2srgb[2][0]*red+cam2srgb[2][1]*green+cam2srgb[2][2]*blue;                              \
                pixels[W*H*0+(x+y*W)] = pixelScale*srgblR/(white-black);                                                    \
                pixels[W*H*1+(x+y*W)] = pixelScale*srgblG/(white-black);                                                    \
                pixels[W*H*2+(x+y*W)] = pixelScale*srgblB/(white-black);                                                    \
            }                                                                                                               \
        if(!image.save((FILENAME).c_str()))                                                                                 \
            std::cerr << "failed to save \"" << (FILENAME) << "\"\n";                                                       \
        else                                                                                                                \
            std::cerr << "written to \"" << (FILENAME) << "\"\n";                                                           \
    } while(0);

    enum {BAYER_RED,BAYER_GREEN1,BAYER_BLUE,BAYER_GREEN2};
    if(needFakeSRGB || needTrueSRGB || needChromaOnlyFile)
    {
        std::cerr << "Writing merged-color sRGB image to file" << (needFakeSRGB && needTrueSRGB ? "s" : "") << "...";

        const int stride=w;
        const int w_=w/2, h_=h/2;
        const int w=w_, h=h_;
        BitmapHeader header={};
        header.signature=0x4d42;
        header.fileSize=((w+3)&~3)*h*3+sizeof header;
        header.dataOffset=sizeof header;
        header.bitmapInfoHeaderSize=40;
        header.width=w;
        header.height=h;
        header.numOfPlanes=1;
        header.bpp=24;

        // Raw RGB data mapped to sRGB pixels in the output image
        ByteBuffer bytes(header.fileSize);
        if(needFakeSRGB)
            bytes.write(&header,sizeof header);
        // Raw RGB data converted to sRGB and written to sRGB pixels in another output image
        ByteBuffer bytes_sRGB(header.fileSize);
        if(needTrueSRGB)
            bytes_sRGB.write(&header,sizeof header);
        // Raw RGB data converted to sRGB and written to sRGB pixels in another output image, but without brightness information
        ByteBuffer bytes_chroma(header.fileSize);
        if(needChromaOnlyFile)
            bytes_chroma.write(&header,sizeof header);
        for(int y=h-1;y>=0;--y)
        {
            for(int x=0;x<w;++x)
            {
                const auto X=x*2, Y=y*2;
                bool overexposed=false;
                const ushort pixelTopLeft    =rgbCoefR *clampAndSubB(data[X+0+(Y+0)*stride][BAYER_RED]   ,overexposed);
                const ushort pixelTopRight   =rgbCoefG1*clampAndSubB(data[X+1+(Y+0)*stride][BAYER_GREEN1],overexposed);
                const ushort pixelBottomLeft =rgbCoefG2*clampAndSubB(data[X+0+(Y+1)*stride][BAYER_GREEN2],overexposed);
                const ushort pixelBottomRight=rgbCoefB *clampAndSubB(data[X+1+(Y+1)*stride][BAYER_BLUE]  ,overexposed);
                const auto green=(pixelTopRight+pixelBottomLeft)/2.;
                const auto red=pixelTopLeft, blue=pixelBottomRight;
                const uint8_t vals[3]={overexposed?uint8_t(255):col(blue),
                                       overexposed?uint8_t(255):col(green),
                                       overexposed?uint8_t(255):col(red)};
                if(needFakeSRGB)
                    bytes.write(vals,sizeof vals);

                const auto& cam2srgb=colorData.rgb_cam;
                const auto srgblR=cam2srgb[0][0]*red+cam2srgb[0][1]*green+cam2srgb[0][2]*blue;
                const auto srgblG=cam2srgb[1][0]*red+cam2srgb[1][1]*green+cam2srgb[1][2]*blue;
                const auto srgblB=cam2srgb[2][0]*red+cam2srgb[2][1]*green+cam2srgb[2][2]*blue;
                const uint8_t vals_sRGB[3]={overexposed?uint8_t(255):col(srgblB),
                                            overexposed?uint8_t(255):col(srgblG),
                                            overexposed?uint8_t(255):col(srgblR)};
                if(needTrueSRGB)
                    bytes_sRGB.write(vals_sRGB,sizeof vals_sRGB);

                const auto chromaR=(white-black)*srgblR/(srgblR+srgblG+srgblB);
                const auto chromaG=(white-black)*srgblG/(srgblR+srgblG+srgblB);
                const auto chromaB=(white-black)*srgblB/(srgblR+srgblG+srgblB);
                const uint8_t vals_chroma[3]={overexposed?uint8_t(255):col(chromaB),
                                              overexposed?uint8_t(255):col(chromaG),
                                              overexposed?uint8_t(255):col(chromaR)};
                if(needChromaOnlyFile)
                    bytes_chroma.write(vals_chroma,sizeof vals_chroma);
            }
            if(needFakeSRGB)
                alignScanLine(bytes);
            if(needTrueSRGB)
                alignScanLine(bytes_sRGB);
            if(needChromaOnlyFile)
                alignScanLine(bytes_chroma);
        }
        if(needFakeSRGB)
        {
            const auto filename=filePathPrefix+"merged.bmp";
            std::ofstream file(filename,std::ios::binary);
            file.write(bytes.data(),bytes.size());
            std::cerr << " written to \"" << filename << "\"\n";
        }
        if(needTrueSRGB)
        {
            const auto filename=filePathPrefix+"merged-srgb.bmp";
            std::ofstream file_sRGB(filename,std::ios::binary);
            file_sRGB.write(bytes_sRGB.data(),bytes_sRGB.size());
            std::cerr << " written to \"" << filename << "\"\n";
        }
        if(needChromaOnlyFile)
        {
            const auto filename=filePathPrefix+"merged-chroma-only.bmp";
            std::ofstream file(filename,std::ios::binary);
            file.write(bytes_chroma.data(),bytes_chroma.size());
            std::cerr << " written to \"" << filename << "\"\n";
        }
    }
    if(needCombinedFile)
        WRITE_BMP_DATA_TO_FILE("Writing combined-channel data to file...",
                               filePathPrefix+"combined.bmp",
                               col(pixelB),
                               col((pixelG1+pixelG2)*0.5),
                               col(pixelR));
    if(needRedFile)
        WRITE_BMP_DATA_TO_FILE("Writing red channel to file...",filePathPrefix+"Red.bmp",col(0),col(0),col(pixelR));
    if(needBlueFile)
        WRITE_BMP_DATA_TO_FILE("Writing blue channel to file...",filePathPrefix+"Blue.bmp",col(pixelB),col(0),col(0));
    if(needGreen1File)
        WRITE_BMP_DATA_TO_FILE("Writing green1 channel to file...",filePathPrefix+"Green1.bmp",col(0),col(pixelG1),col(0));
    if(needGreen2File)
        WRITE_BMP_DATA_TO_FILE("Writing green2 channel to file...",filePathPrefix+"Green2.bmp",col(0),col(pixelG2),col(0));

    if(needTIFFFile || needUnweightedTIFF)
        WRITE_TIFF_DATA_TO_FILE("Writing combined-channel data to TIFF file...", filePathPrefix+"merged.tiff", w,h);

}

int main(int argc, char** argv)
{
    std::string filename;
    enum class WhiteBalance
    {
        Default,
        Daylight,
        AsShot,
        None,
    } whiteBalance=WhiteBalance::Default;
    unsigned customWhiteLevel=0;
    constexpr auto cam2sRGBsize=9;
    float cam2sRGB[cam2sRGBsize];
    bool customCam2sRGBmatrix=false;
    for(int i=1;i<argc;++i)
    {
        const std::string arg(argv[i]);
        if(arg=="-srgb" || arg=="--srgb") needTrueSRGB=true;
        else if(arg=="--fake-srgb")       needFakeSRGB=true;
        else if(arg=="--chroma"||arg=="-chroma")   needChromaOnlyFile=true;
        else if(arg=="--combined" || arg=="-comb") needCombinedFile=true;
        else if(arg=="--tiff" || arg=="-tif") needTIFFFile=true;
        else if(arg=="--tiff-unw")
        {
            needUnweightedTIFF=true;
            whiteBalance=WhiteBalance::None;
        }
        else if(arg=="--f32" || arg=="-f32") needF32=true;
        else if(arg=="-r" || arg=="--red") needRedFile=true;
        else if(arg=="-g1" || arg=="--green1") needGreen1File=true;
        else if(arg=="-g2" || arg=="--green2") needGreen2File=true;
        else if(arg=="-b" || arg=="--blue") needBlueFile=true;
        else if(arg=="-w" || arg=="--white-level")
        {
            if(++i==argc)
            {
                std::cerr << "Option " << arg << " requires parameter\n";
                return usage(argv[0],1);
            }
            const std::string arg(argv[i]);
            try { customWhiteLevel=std::stoul(arg); } catch(...) {}
            if(!customWhiteLevel)
            {
                std::cerr << "Bad value for custom white level\n";
                return 1;
            }
        }
        else if(arg=="-wb" || arg=="--white-balance")
        {
            if(++i==argc)
            {
                std::cerr << "Option " << arg << " requires parameter\n";
                return usage(argv[0],1);
            }
            const std::string arg(argv[i]);
            if(arg=="as-shot") whiteBalance=WhiteBalance::AsShot;
            else if(arg=="daylight") whiteBalance=WhiteBalance::Daylight;
            else if(arg=="none") whiteBalance=WhiteBalance::None;
            else
            {
                std::cerr << "Unknown white balance mode \"" << arg << "\"\n";
                return 1;
            }
        }
        else if(arg=="--cam2srgb")
        {
            if(++i==argc)
            {
                std::cerr << "Option " << arg << " requires parameter\n";
                return usage(argv[0],1);
            }
            const std::string arg(argv[i]);
            if(!parseMatrix(cam2sRGB, cam2sRGBsize, arg))
            {
                std::cerr << "Matrix must be specified as a sequence of " << cam2sRGBsize  << " comma-separated values (in row-major order).\n";
                return usage(argv[0],1);
            }
            customCam2sRGBmatrix=true;
        }
        else if(arg=="-h" || arg=="--help") return usage(argv[0],0);
        else if(arg=="-s" || arg=="--scale")
        {
            if(++i==argc)
            {
                std::cerr << "Option " << arg << " requires parameter\n";
                return usage(argv[0],1);
            }
            const std::string arg(argv[i]);
            std::size_t pos=0;
            try { pixelScale=std::stof(arg,&pos); } catch(...) {}
            if(pos!=arg.length())
            {
                std::cerr << "Failed to parse pixel value multiplier\n";
                return 1;
            }
        }
        else if(arg=="-p" || arg=="--prefix")
        {
            if(++i==argc)
            {
                std::cerr << "Option " << arg << " requires parameter\n";
                return usage(argv[0],1);
            }
            filePathPrefix=argv[i];
        }
        else if(!arg.empty() && arg[0]!='-') filename=arg;
        else
        {
            std::cerr << "Unknown option " << arg << "\n";
            return usage(argv[0],1);
        }
    }
    if(filename.empty()) return usage(argv[0],1);

    LibRaw libRaw;
    libRaw.open_file(filename.c_str());
    const auto& sizes=libRaw.imgdata.sizes;

    std::cerr << "Unpacking raw data...\n";
    if(const auto error=libRaw.unpack())
    {
        std::cerr << "Failed to unpack: error " << error << "\n";
        return 2;
    }

    const auto& idata=libRaw.imgdata.idata;

    std::cerr << "Convering raw data to image...\n";
    libRaw.raw2image();
    const auto& cam_mul=libRaw.imgdata.color.cam_mul;
    const auto& pre_mul=libRaw.imgdata.color.pre_mul;
    const float camMulMax=*std::max_element(std::begin(cam_mul),std::end(cam_mul));
    const float asShotWBCoefs[4]={cam_mul[0]/camMulMax,cam_mul[1]/camMulMax,cam_mul[2]/camMulMax,cam_mul[3]/camMulMax};
    const float preMulMax=*std::max_element(std::begin(pre_mul),std::end(pre_mul));
    const float daylightWBCoefs[4]={pre_mul[0]/preMulMax,pre_mul[1]/preMulMax,pre_mul[2]/preMulMax,(pre_mul[3]==0 ? pre_mul[1] : pre_mul[3])/preMulMax};
    const float noWBCoefs[4]={1,1,1,1};

    if(customCam2sRGBmatrix)
    {
        if(whiteBalance!=WhiteBalance::Default)
            std::cerr << "Warning: white balance option is ignored when custom camera-to-sRGB matrix is specified\n";
        whiteBalance=WhiteBalance::None;
        for(unsigned row=0;row<3;++row)
            for(unsigned col=0;col<3;++col)
                libRaw.imgdata.rawdata.color.rgb_cam[row][col]=cam2sRGB[row*3+col];
    }
    else if(whiteBalance==WhiteBalance::Default)
        whiteBalance=WhiteBalance::Daylight;

    if(needF32)
    {
        writeF32(libRaw, libRaw.imgdata.image,sizes.iwidth,sizes.iheight, libRaw.imgdata.rawdata.color.black);
    }
    else
    {
        writeImagePlanesToBMP(libRaw.imgdata.image,sizes.iwidth,sizes.iheight,
                              whiteBalance==WhiteBalance::Daylight ? daylightWBCoefs :
                               whiteBalance==WhiteBalance::AsShot ? asShotWBCoefs :
                                noWBCoefs,
                              libRaw.imgdata.rawdata.color,
                              customWhiteLevel ? customWhiteLevel : libRaw.imgdata.rawdata.color.maximum);
    }
}
