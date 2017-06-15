#include <libraw/libraw.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstdint>
#include <vector>
#include <limits>
#include <cmath>

using std::uint8_t;

inline int usage(const char* argv0, int returnValue)
{
    std::cerr << "Usage: " << argv0 << " filename\n";
    return returnValue;
}

void formatHistogram(std::vector<int> const& histogramRed,
                     std::vector<int> const& histogramGreen1,
                     std::vector<int> const& histogramGreen2,
                     std::vector<int> const& histogramBlue)
{
    std::cerr << "Formatting histogram...\n";

    std::ostringstream str;
    str << "histogram={\n{";
    for(auto v : histogramRed)
        str << int(v) << ",";
    str.seekp(-1,str.cur);
    str << "},\n{";
    for(auto v : histogramGreen1)
        str << int(v) << ",";
    str.seekp(-1,str.cur);
    str << "},\n{";
    for(auto v : histogramBlue)
        str << int(v) << ",";
    str.seekp(-1,str.cur);
    str << "},\n{";
    for(auto v : histogramGreen2)
        str << int(v) << ",";
    str.seekp(-1,str.cur);
    str << "}\n};\n";
    std::cout << str.str();
}

void printImageHistogram(const ushort (*img)[4], int size)
{
    constexpr auto histSize=std::numeric_limits<std::remove_reference_t<decltype(img[0][0])>>::max();
    std::vector<int> histogramRed(histSize);
    std::vector<int> histogramGreen1(histSize);
    std::vector<int> histogramGreen2(histSize);
    std::vector<int> histogramBlue(histSize);
    std::cerr << "Computing histogram...\n";
    for(int i=0;i<size;++i)
    {
        const auto*const pixel=img[i];
        ++histogramRed[pixel[0]];
        ++histogramGreen1[pixel[1]];
        ++histogramBlue[pixel[2]];
        ++histogramGreen2[pixel[3]];
    }
    formatHistogram(histogramRed,histogramGreen1,histogramGreen2,histogramBlue);
}

#ifndef DISABLE_BMP_OUTPUT
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

void writeImagePlanesToBMP(ushort (*data)[4], int w, int h, int max)
{
    BitmapHeader header={};
    header.signature=0x4d42;
    header.fileSize=((w+3)&~3)*h*3+sizeof header;
    header.dataOffset=sizeof header;
    header.bitmapInfoHeaderSize=40;
    header.width=w;
    header.height=h;
    header.numOfPlanes=1;
    header.bpp=24;

    const auto col=[max](ushort p)->uint8_t{return std::min(255l,std::lround(255.*p/max));};
    const auto alignScanLine=[](std::ofstream& file)
    {
        constexpr auto scanLineAlignment=4;
        const char align[scanLineAlignment-1]={};
        const auto alignSize=(sizeof header-unsigned(file.tellp()))%scanLineAlignment;
        file.write(align,alignSize);
    };

    {
        std::cerr << "Writing red channel to file...\n";
        std::ofstream file("/tmp/outfileRed.bmp",std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header),sizeof header);
        for(int y=h-1;y>=0;--y)
        {
            for(int x=0;x<w;++x)
            {
                const auto pixel=data[x+y*w];
                const uint8_t vals[3]={col(0),col(0),col(pixel[0])};
                file.write(reinterpret_cast<const char*>(vals),sizeof vals);
            }
            alignScanLine(file);
        }
    }
    {
        std::cerr << "Writing blue channel to file...\n";
        std::ofstream file("/tmp/outfileBlue.bmp",std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header),sizeof header);
        for(int y=h-1;y>=0;--y)
        {
            for(int x=0;x<w;++x)
            {
                const auto pixel=data[x+y*w];
                const uint8_t vals[3]={col(pixel[2]),col(0),col(0)};
                file.write(reinterpret_cast<const char*>(vals),sizeof vals);
            }
            alignScanLine(file);
        }
    }
    {
        std::cerr << "Writing green1 channel to file...\n";
        std::ofstream file("/tmp/outfileGreen1.bmp",std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header),sizeof header);
        for(int y=h-1;y>=0;--y)
        {
            for(int x=0;x<w;++x)
            {
                const auto pixel=data[x+y*w];
                const uint8_t vals[3]={col(0),col(pixel[1]),col(0)};
                file.write(reinterpret_cast<const char*>(vals),sizeof vals);
            }
            alignScanLine(file);
        }
    }
    {
        std::cerr << "Writing green2 channel to file...\n";
        std::ofstream file("/tmp/outfileGreen2.bmp",std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header),sizeof header);
        for(int y=h-1;y>=0;--y)
        {
            for(int x=0;x<w;++x)
            {
                const auto pixel=data[x+y*w];
                const uint8_t vals[3]={col(0),col(pixel[3]),col(0)};
                file.write(reinterpret_cast<const char*>(vals),sizeof vals);
            }
            alignScanLine(file);
        }
    }
    {
        std::cerr << "Writing combined-channel data to file...\n";
        std::ofstream file("/tmp/outfile-combined.bmp",std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header),sizeof header);
        for(int y=h-1;y>=0;--y)
        {
            for(int x=0;x<w;++x)
            {
                const auto pixel=data[x+y*w];
                const uint8_t vals[3]={col(pixel[2]),
                                       uint8_t(col(pixel[3])+col(pixel[1])),
                                       col(pixel[0])};
                file.write(reinterpret_cast<const char*>(vals),sizeof vals);
            }
            alignScanLine(file);
        }
    }
}
#else
#define writeBMP(d,w,h,m)
#endif

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
    std::cerr << "Make: " << idata.make << "\n";
    std::cerr << "Model: " << idata.model << "\n";
    std::cerr << "Colors: " << idata.colors << "\n";
    std::cerr << "Raw size: " << sizes.raw_width << "x" << sizes.raw_height << "\n";
    std::cerr << "Visible size: " << sizes.width << "x" << sizes.height << "\n";
    std::cerr << "Pixel value maximum: " << libRaw.imgdata.rawdata.color.maximum << "\n";
    std::cerr << "Margins{left: " << sizes.left_margin << ", top: " << sizes.top_margin << "}\n";
    std::cerr << "iSize: " << sizes.iwidth << "x" << sizes.iheight << "\n";
    std::cerr << "Pixel aspect: " << sizes.pixel_aspect << "\n";

    std::cerr << "Convering raw data to image...\n";
    libRaw.raw2image();
    printImageHistogram(libRaw.imgdata.image,sizes.iwidth*sizes.iheight);
    writeImagePlanesToBMP(libRaw.imgdata.image,sizes.iwidth,sizes.iheight,libRaw.imgdata.rawdata.color.maximum);
}
