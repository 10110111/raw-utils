#include <libraw/libraw.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstddef>
#include <cstring>
#include <vector>
#include <limits>
#include <cassert>
#include <cmath>

using std::size_t;

int usage(const char* argv0, int returnValue)
{
    std::cerr << "Usage: " << argv0 << " filename [--error-on-misexposure]\n";
    return returnValue;
}

bool errorOnMisexposure=false;

bool printAverageColor(LibRaw& libRaw, const ushort (*img)[4], const int w, const int h,
                       libraw_colordata_t const& colorData, const float (&rgbCoefs)[4])
{
    const auto rgbCoefR =rgbCoefs[0];
    const auto rgbCoefG1=rgbCoefs[1];
    const auto rgbCoefB =rgbCoefs[2];
    const auto rgbCoefG2=rgbCoefs[3];

    bool misexposure=false;
    double red=0, green1=0, green2=0, blue=0;
    int tooBlackPixelCount=0, tooWhitePixelCount=0;
    int blackPixelCount=0, whitePixelCount=0;
    for(int y=0;y<h;++y)
    {
        for(int x=0;x<w;++x)
        {
            const auto colIndex=libRaw.FC(y,x);
            auto pixelRaw=img[x+y*w][colIndex];
            if(pixelRaw<colorData.black)
            {
                ++tooBlackPixelCount;
                pixelRaw=colorData.black;
            }
            else if(pixelRaw>colorData.maximum)
            {
                ++tooWhitePixelCount;
                pixelRaw=colorData.maximum;
            }
            if(pixelRaw==colorData.black)
            {
                ++blackPixelCount;
            }
            else if(pixelRaw==colorData.maximum)
            {
                ++whitePixelCount;
            }

            const auto pixel=pixelRaw-colorData.black;
            switch(colIndex)
            {
            case 0: red+=pixel*rgbCoefR; break;
            case 1: green1+=pixel*rgbCoefG1; break;
            case 2: blue+=pixel*rgbCoefB; break;
            case 3: green2+=pixel*rgbCoefG2; break;
            default: assert(!"Must not get here!");
            }
        }
    }
    red/=w*h;
    green1/=w*h;
    green2/=w*h;
    blue/=w*h;
    if(tooBlackPixelCount)
    {
        std::cerr << "Warning: " << tooBlackPixelCount << " pixels have values less than black level\n";
        misexposure=true;
    }
    if(tooWhitePixelCount)
    {
        std::cerr << "Warning: " << tooWhitePixelCount << " pixels have values greater than white level\n";
        misexposure=true;
    }
    if(blackPixelCount && !tooBlackPixelCount)
    {
        std::cerr << "Warning: " << blackPixelCount << " pixels are underexposed\n";
        misexposure=true;
    }
    if(whitePixelCount && !tooWhitePixelCount)
    {
        std::cerr << "Warning: " << whitePixelCount << " pixels are overexposed\n";
        misexposure=true;
    }
    std::cout << "Mean camera R,G1,G2,B: " << red << ',' << green1 << ',' << green2 << ',' << blue << '\n';
    const auto green=(green1+green2)/2;
    const auto& cam2srgb=colorData.rgb_cam;
    const auto srgblR=cam2srgb[0][0]*red+cam2srgb[0][1]*green+cam2srgb[0][2]*blue;
    const auto srgblG=cam2srgb[1][0]*red+cam2srgb[1][1]*green+cam2srgb[1][2]*blue;
    const auto srgblB=cam2srgb[2][0]*red+cam2srgb[2][1]*green+cam2srgb[2][2]*blue;
    std::cout << "sRGBL: " << srgblR << ',' << srgblG << ',' << srgblB << "\n";

    const auto X=0.4124*srgblR + 0.3576*srgblG + 0.1805*srgblB;
    const auto Y=0.2126*srgblR + 0.7152*srgblG + 0.0722*srgblB;
    const auto Z=0.0193*srgblR + 0.1192*srgblG + 0.9505*srgblB;
    std::cout << "XYZ: " << X << ',' << Y << ',' << Z << '\n';
    std::cout << "xyY: " << X/(X+Y+Z) << ',' << Y/(X+Y+Z) << ',' << Y << '\n';

    return errorOnMisexposure && misexposure;
}

int main(int argc, char** argv)
{
    if(argc!=2 && argc!=3)
        return usage(argv[0],1);
    const char* filename=argv[1];
    if(argc==3)
    {
        if(!std::strcmp(argv[2],"--error-on-misexposure"))
            errorOnMisexposure=true;
        else
            return usage(argv[0],1);
    }
    LibRaw libRaw;
    libRaw.open_file(filename);
    const auto& sizes=libRaw.imgdata.sizes;

    std::cerr << "Unpacking raw data...\n";
    if(const auto error=libRaw.unpack())
    {
        std::cerr << "Failed to unpack: error " << error << "\n";
        return 2;
    }

    std::cerr << "Convering raw data to image...\n";
    libRaw.raw2image();
    const auto& cam_mul=libRaw.imgdata.color.cam_mul;
    const float camMulMax=*std::max_element(std::begin(cam_mul),std::end(cam_mul));
    const float rgbCoefs[4]={cam_mul[0]/camMulMax,cam_mul[1]/camMulMax,cam_mul[2]/camMulMax,cam_mul[3]/camMulMax};
    return printAverageColor(libRaw,libRaw.imgdata.image,sizes.iwidth,sizes.iheight,libRaw.imgdata.color,rgbCoefs);
}

