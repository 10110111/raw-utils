#include <libraw/libraw.h>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <vector>
#include <cmath>

using std::uint8_t;
using std::size_t;

bool needTrueSRGB=false;
bool needFakeSRGB=false;
bool needCombinedFile=false;
bool needRedFile=false;
bool needGreen1File=false;
bool needGreen2File=false;
bool needBlueFile=false;

inline int usage(const char* argv0, int returnValue)
{
    std::cerr << "Usage: [options...] " << argv0 << " filename\n";
    std::cerr << "Options:\n"
              << "  -srgb,--srgb        Create an sRGB image by merging RGGB data and applying the cam2rgb conversion matrix\n"
              << "  --fake-srgb         Similar to -srgb, but without applying the conversion matrix\n"
              << "  --combined          Create a file containing RGGB data on the Bayer grid, coded by sRGB colors\n"
              << "  -r,--red            Create a file with red channel only data on the Bayer grid\n"
              << "  -g1,--green1        Create a file with data only from first green channel on the Bayer grid\n"
              << "  -g2,--green2        Create a file with data only from second green channel on the Bayer grid\n"
              << "  -b,--blue           Create a file with blue channel only data on the Bayer grid\n";
    return returnValue;
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

double clampRGB(double x){return std::max(0.,std::min(255.,x));}
uint8_t toSRGB(uint8_t x){return std::pow(x/255.,1/2.2)*255;}
void writeImagePlanesToBMP(ushort (*data)[4], const int w, const int h, const float (&rgbCoefs)[4], libraw_colordata_t const& colorData)
{
    const unsigned black=colorData.black, white=colorData.maximum;
    BitmapHeader header={};
    header.signature=0x4d42;
    header.fileSize=((w+3)&~3)*h*3+sizeof header;
    header.dataOffset=sizeof header;
    header.bitmapInfoHeaderSize=40;
    header.width=w;
    header.height=-h;
    header.numOfPlanes=1;
    header.bpp=24;

    const auto col=[black,white](float p)->uint8_t{return toSRGB(clampRGB(std::lround(255.*p/(white-black))));};
    const auto alignScanLine=[](ByteBuffer& bytes)
    {
        constexpr auto scanLineAlignment=4;
        const char align[scanLineAlignment-1]={};
        const auto alignSize=(sizeof header-bytes.tellp())%scanLineAlignment;
        bytes.write(align,alignSize);
    };
    const auto clampAndSubB=[black,white](ushort p){return (p>white ? white : p<black ? black : p)-black; };
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
                const auto pixelR =rgbCoefR *clampAndSubB(data[x+y*w][0]);    \
                const auto pixelG1=rgbCoefG1*clampAndSubB(data[x+y*w][1]);    \
                const auto pixelB =rgbCoefB *clampAndSubB(data[x+y*w][2]);    \
                const auto pixelG2=rgbCoefG2*clampAndSubB(data[x+y*w][3]);    \
                const uint8_t vals[3]={BLUE,GREEN,RED};             \
                bytes.write(vals,sizeof vals);                      \
            }                                                       \
            alignScanLine(bytes);                                   \
        }                                                           \
        std::ofstream file(FILENAME,std::ios::binary);              \
        file.write(bytes.data(),bytes.size());                      \
        std::cerr << " written to \"" << FILENAME << "\"\n";        \
    } while(0)

    if(needFakeSRGB || needTrueSRGB)
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
        enum {RED,GREEN1,BLUE,GREEN2};
        for(int y=h-1;y>=0;--y)
        {
            for(int x=0;x<w;++x)
            {
                const auto X=x*2, Y=y*2;
                const ushort pixelTopLeft    =rgbCoefR *clampAndSubB(data[X+0+(Y+0)*stride][RED]);
                const ushort pixelTopRight   =rgbCoefG1*clampAndSubB(data[X+1+(Y+0)*stride][GREEN1]);
                const ushort pixelBottomLeft =rgbCoefG2*clampAndSubB(data[X+0+(Y+1)*stride][GREEN2]);
                const ushort pixelBottomRight=rgbCoefB *clampAndSubB(data[X+1+(Y+1)*stride][BLUE]);
                const auto green=(pixelTopRight+pixelBottomLeft)/2.;
                const auto red=pixelTopLeft, blue=pixelBottomRight;
                const uint8_t vals[3]={col(blue),col(green),col(red)};
                if(needFakeSRGB)
                    bytes.write(vals,sizeof vals);

                const auto& cam2srgb=colorData.rgb_cam;
                const auto srgblR=cam2srgb[0][0]*red+cam2srgb[0][1]*green+cam2srgb[0][2]*blue;
                const auto srgblG=cam2srgb[1][0]*red+cam2srgb[1][1]*green+cam2srgb[1][2]*blue;
                const auto srgblB=cam2srgb[2][0]*red+cam2srgb[2][1]*green+cam2srgb[2][2]*blue;
                const uint8_t vals_sRGB[3]={col(srgblB),col(srgblG),col(srgblR)};
                if(needTrueSRGB)
                    bytes_sRGB.write(vals_sRGB,sizeof vals_sRGB);
            }
            if(needFakeSRGB)
                alignScanLine(bytes);
            if(needTrueSRGB)
                alignScanLine(bytes_sRGB);
        }
        if(needFakeSRGB)
        {
            const char filename[]="/tmp/output-merged.bmp";
            std::ofstream file(filename,std::ios::binary);
            file.write(bytes.data(),bytes.size());
            std::cerr << " written to \"" << filename << "\"\n";
        }
        if(needTrueSRGB)
        {
            const char filename[]="/tmp/output-merged-srgb.bmp";
            std::ofstream file_sRGB(filename,std::ios::binary);
            file_sRGB.write(bytes_sRGB.data(),bytes_sRGB.size());
            std::cerr << " written to \"" << filename << "\"\n";
        }
    }
    if(needCombinedFile)
        WRITE_BMP_DATA_TO_FILE("Writing combined-channel data to file...",
                               "/tmp/outfile-combined.bmp",
                               col(pixelB),
                               col((pixelG1+pixelG2)*0.5),
                               col(pixelR));
    if(needRedFile)
        WRITE_BMP_DATA_TO_FILE("Writing red channel to file...","/tmp/outfileRed.bmp",col(0),col(0),col(pixelR));
    if(needBlueFile)
        WRITE_BMP_DATA_TO_FILE("Writing blue channel to file...","/tmp/outfileBlue.bmp",col(pixelB),col(0),col(0));
    if(needGreen1File)
        WRITE_BMP_DATA_TO_FILE("Writing green1 channel to file...","/tmp/outfileGreen1.bmp",col(0),col(pixelG1),col(0));
    if(needGreen2File)
        WRITE_BMP_DATA_TO_FILE("Writing green2 channel to file...","/tmp/outfileGreen2.bmp",col(0),col(pixelG2),col(0));

}

int main(int argc, char** argv)
{
    std::string filename;
    for(int i=1;i<argc;++i)
    {
        const std::string arg(argv[i]);
        if(arg=="-srgb" || arg=="--srgb") needTrueSRGB=true;
        else if(arg=="--fake-srgb")       needFakeSRGB=true;
        else if(arg=="--combined" || arg=="-comb") needCombinedFile=true;
        else if(arg=="-r" || arg=="--red") needRedFile=true;
        else if(arg=="-g1" || arg=="--green1") needGreen1File=true;
        else if(arg=="-g2" || arg=="--green2") needGreen2File=true;
        else if(arg=="-b" || arg=="--blue") needBlueFile=true;
        else if(arg=="-h" || arg=="--help") return usage(argv[0],0);
        else if(!arg.empty() && arg[0]!='-') filename=arg;
        else
        {
            std::cerr << "Unknown option " << arg << "\n";
            return usage(argv[0],1);
        }
    }
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
    const float camMulMax=*std::max_element(std::begin(cam_mul),std::end(cam_mul));
    const float rgbCoefs[4]={cam_mul[0]/camMulMax,cam_mul[1]/camMulMax,cam_mul[2]/camMulMax,cam_mul[3]/camMulMax};
    writeImagePlanesToBMP(libRaw.imgdata.image,sizes.iwidth,sizes.iheight,rgbCoefs,libRaw.imgdata.rawdata.color);
}
