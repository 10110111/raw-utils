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

inline int usage(const char* argv0, int returnValue)
{
    std::cerr << "Usage: " << argv0 << " filename\n";
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
void writeImagePlanesToBMP(ushort (*data)[4], const int w, const int h, const unsigned black, const unsigned white, const float (&rgbCoefs)[4])
{
    BitmapHeader header={};
    header.signature=0x4d42;
    header.fileSize=((w+3)&~3)*h*3+sizeof header;
    header.dataOffset=sizeof header;
    header.bitmapInfoHeaderSize=40;
    header.width=w;
    header.height=-h;
    header.numOfPlanes=1;
    header.bpp=24;

    const auto col=[black,white](ushort p)->uint8_t{return toSRGB(clampRGB(std::lround(255.*p/(white-black))));};
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
    } while(0)

    WRITE_BMP_DATA_TO_FILE("Writing combined-channel data to file...\n",
                           "/tmp/outfile-combined.bmp",
                           col(pixelB),
                           col((pixelG1+pixelG2)*0.5),
                           col(pixelR));
    WRITE_BMP_DATA_TO_FILE("Writing red channel to file...\n","/tmp/outfileRed.bmp",col(0),col(0),col(pixelR));
    WRITE_BMP_DATA_TO_FILE("Writing blue channel to file...\n","/tmp/outfileBlue.bmp",col(pixelB),col(0),col(0));
    WRITE_BMP_DATA_TO_FILE("Writing green1 channel to file...\n","/tmp/outfileGreen1.bmp",col(0),col(pixelG1),col(0));
    WRITE_BMP_DATA_TO_FILE("Writing green2 channel to file...\n","/tmp/outfileGreen2.bmp",col(0),col(pixelG2),col(0));

}

int main(int argc, char** argv)
{
    if(argc!=2)
        return usage(argv[0],1);
    const char* filename=argv[1];
    LibRaw libRaw;
    libRaw.open_file(filename);
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
    writeImagePlanesToBMP(libRaw.imgdata.image,sizes.iwidth,sizes.iheight,libRaw.imgdata.rawdata.color.black,libRaw.imgdata.rawdata.color.maximum,rgbCoefs);
}
