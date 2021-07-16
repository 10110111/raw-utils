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
    std::cerr << "Usage: " << argv0 << " [--mma|--csv] [--white-balance] [--no-clip] filename\n";
    return returnValue;
}

enum class PrintFormat
{
    Mathematica,
    CSV,
};

void formatHistogram(std::vector<int> const& histogramRed,
                     std::vector<int> const& histogramGreen1,
                     std::vector<int> const& histogramGreen2,
                     std::vector<int> const& histogramBlue,
                     PrintFormat format)
{
    switch(format)
    {
    case PrintFormat::Mathematica:
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
        break;
    }
    case PrintFormat::CSV:
        std::cout << "value,red,green-1,green-2,blue\n";
        for(unsigned i=0;i<histogramRed.size();++i)
            std::cout << i << ',' << histogramRed[i] << ',' << histogramGreen1[i] << ',' << histogramGreen2[i] << ',' << histogramBlue[i] << '\n';
        break;
    }
}

void printImageHistogram(LibRaw& libRaw, const ushort (*img)[4], const int w, const int h,
                         const unsigned black, const unsigned white, const float (&rgbCoefs)[4],
                         PrintFormat format, const bool clip)
{
    const auto rgbCoefR =rgbCoefs[0];
    const auto rgbCoefG1=rgbCoefs[1];
    const auto rgbCoefB =rgbCoefs[2];
    const auto rgbCoefG2=rgbCoefs[3];

    const auto histSize = clip ? white-black+1 : white;
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
                if(clip)
                    pixelRaw=black;
            }
            else if(pixelRaw>white)
            {
                ++tooWhitePixelCount;
                if(clip)
                    pixelRaw=white;
            }

            const auto pixel = clip ? pixelRaw-black : pixelRaw;
            std::size_t index;
            switch(colIndex)
            {
            case 0:
                index = std::lround(pixel*rgbCoefR);
                if(index>=histogramRed.size())
                    histogramRed.resize(index+1);
                ++histogramRed[index];
                break;
            case 1:
                index = std::lround(pixel*rgbCoefG1);
                if(index>=histogramGreen1.size())
                    histogramGreen1.resize(index+1);
                ++histogramGreen1[index];
                break;
            case 2:
                index = std::lround(pixel*rgbCoefB);
                if(index>=histogramBlue.size())
                    histogramBlue.resize(index+1);
                ++histogramBlue[index];
                break;
            case 3:
                index = std::lround(pixel*rgbCoefG2);
                if(index>=histogramGreen2.size())
                    histogramGreen2.resize(index+1);
                ++histogramGreen2[index];
                break;
            default: assert(!"Must not get here!");
            }
        }
    }
    const auto maxLen = std::max({histogramRed.size(), histogramGreen1.size(), histogramGreen2.size(), histogramBlue.size()});
    histogramRed.resize(maxLen);
    histogramGreen1.resize(maxLen);
    histogramGreen2.resize(maxLen);
    histogramBlue.resize(maxLen);
    if(tooBlackPixelCount)
        std::cerr << "Warning: " << tooBlackPixelCount << " pixels have values less than black level\n";
    if(tooWhitePixelCount)
        std::cerr << "Warning: " << tooWhitePixelCount << " pixels have values greater than white level\n";
    formatHistogram(histogramRed,histogramGreen1,histogramGreen2,histogramBlue,format);
}

int main(int argc, char** argv)
{
    if(argc<2 || argc>4)
        return usage(argv[0],1);
    std::string filename=argv[1];
    PrintFormat format=PrintFormat::CSV;
    bool enableWhiteBalance=false;
    bool clipping=true;
    for(int i=1;i<argc;++i)
    {
        const auto arg=std::string(argv[i]);
        if(arg=="--mma")
        {
            format=PrintFormat::Mathematica;
        }
        else if(arg=="--csv")
        {
            format=PrintFormat::CSV;
        }
        else if(arg=="--white-balance")
        {
            enableWhiteBalance=true;
        }
        else if(arg=="--no-clip")
        {
            clipping=false;
        }
        else if(filename.empty() && !arg.empty() && arg[0]!='-')
        {
            filename=arg;
        }
        else if(arg=="-h" || arg=="--help")
        {
            return usage(argv[0],0);
        }
        else if(!arg.empty() && arg[0]=='-')
        {
            std::cerr << "Unknown option " << arg << "\n";
            return usage(argv[0],1);
        }
    }

    if(enableWhiteBalance)
        std::cerr << "Will use camera-supplied \"as-shot\" white balance coefficients\n";
    else
        std::cerr << "Will print unbalanced raw histogram\n";
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
    const float ones[4]={1,1,1,1};
    printImageHistogram(libRaw, libRaw.imgdata.image, sizes.iwidth,sizes.iheight, libRaw.imgdata.color.black,
                        libRaw.imgdata.color.maximum, enableWhiteBalance ? rgbCoefs : ones,format, clipping);
}
