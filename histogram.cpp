#include <libraw/libraw.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstddef>
#include <vector>
#include <limits>
#include <cassert>
#include <cmath>

using std::size_t;

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

void printImageHistogram(LibRaw& libRaw, const ushort (*img)[4], const int w, const int h, const unsigned black, const unsigned white, const float (&rgbCoefs)[4])
{
    const auto rgbCoefR =rgbCoefs[0];
    const auto rgbCoefG1=rgbCoefs[1];
    const auto rgbCoefB =rgbCoefs[2];
    const auto rgbCoefG2=rgbCoefs[3];

    const auto histSize=white-black+1;
    std::vector<int> histogramRed(histSize);
    std::vector<int> histogramGreen1(histSize);
    std::vector<int> histogramGreen2(histSize);
    std::vector<int> histogramBlue(histSize);
    std::cerr << "Computing histogram...\n";
    int tooBlackPixelCount=0, tooWhitePixelCount=0;
    for(int y=0;y<h;++y)
    {
        for(int x=0;x<w;++x)
        {
            const auto colIndex=libRaw.FC(y,x);
            auto pixelRaw=img[x+y*w][colIndex];
            if(pixelRaw<black)
            {
                ++tooBlackPixelCount;
                pixelRaw=black;
            }
            else if(pixelRaw>white)
            {
                ++tooWhitePixelCount;
                pixelRaw=white;
            }

            const auto pixel=pixelRaw-black;
            switch(colIndex)
            {
            case 0: ++histogramRed[std::lround(pixel*rgbCoefR)]; break;
            case 1: ++histogramGreen1[std::lround(pixel*rgbCoefG1)]; break;
            case 2: ++histogramBlue[std::lround(pixel*rgbCoefB)]; break;
            case 3: ++histogramGreen2[std::lround(pixel*rgbCoefG2)]; break;
            default: assert(!"Must not get here!");
            }
        }
    }
    if(tooBlackPixelCount)
        std::cerr << "Warning: " << tooBlackPixelCount << " pixels have values less than black level\n";
    if(tooWhitePixelCount)
        std::cerr << "Warning: " << tooWhitePixelCount << " pixels have values greater than white level\n";
    formatHistogram(histogramRed,histogramGreen1,histogramGreen2,histogramBlue);
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
    printImageHistogram(libRaw,libRaw.imgdata.image,sizes.iwidth,sizes.iheight,libRaw.imgdata.color.black,libRaw.imgdata.color.maximum,rgbCoefs);
}
