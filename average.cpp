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
    std::cerr << "Usage: " << argv0 << " filename [--xrange min..max] [--yrange min..max] [--error-on-misexposure]\n";
    return returnValue;
}

bool errorOnMisexposure=false;

bool printAverageColor(LibRaw& libRaw, const ushort (*img)[4], int xmin, int xmax, int ymin, int ymax,
                       libraw_colordata_t const& colorData, const float (&rgbCoefs)[4])
{
    const auto rgbCoefR =rgbCoefs[0];
    const auto rgbCoefG1=rgbCoefs[1];
    const auto rgbCoefB =rgbCoefs[2];
    const auto rgbCoefG2=rgbCoefs[3];

    const auto w=libRaw.imgdata.sizes.iwidth;

    bool misexposure=false;
    double red=0, green1=0, green2=0, blue=0;
    int tooBlackPixelCount=0, tooWhitePixelCount=0;
    int blackPixelCount=0, whitePixelCount=0;
    for(int y=ymin;y<ymax;++y)
    {
        for(int x=xmin;x<xmax;++x)
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
            case 0: red+=pixel; break;
            case 1: green1+=pixel; break;
            case 2: blue+=pixel; break;
            case 3: green2+=pixel; break;
            default: assert(!"Must not get here!");
            }
        }
    }
    const auto count=double(ymax-ymin)*(xmax-xmin)/4;
    red/=count;
    green1/=count;
    green2/=count;
    blue/=count;
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
    std::cout << "Mean raw R,G1,G2,B minus black level: " << red << ',' << green1 << ',' << green2 << ',' << blue << '\n';

    red*=rgbCoefR;
    green1*=rgbCoefG1;
    green2*=rgbCoefG2;
    blue*=rgbCoefB;
    std::cout << "Mean balanced R,G1,G2,B: " << red << ',' << green1 << ',' << green2 << ',' << blue << '\n';

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

int requireParam(std::string const& opt)
{
    std::cerr << "Option " << opt << " requires parameter\nUse --help to see usage\n";
    return 1;
}

std::pair<unsigned,unsigned> parseRange(std::string const& str)
{
    std::size_t pos;
    try
    {
        const auto min=std::stoul(str, &pos);
        if(pos+2>=str.size() || str.substr(pos,2)!="..")
            throw "bad format";

        const auto maxStr=str.substr(pos+2);
        const auto max=std::stoul(maxStr, &pos);
        if(pos!=maxStr.size())
            throw "bad format: trailing characters found";

        return {min,max};
    }
    catch(const char* err)
    {
        throw std::runtime_error(err);
    }
    catch(...)
    {
        throw std::runtime_error("can't parse a number");
    }
}

int main(int argc, char** argv)
{
    if(argc==1)
        return usage(argv[0],1);
    std::string filename;
    int xmin=0, ymin=0, xmax=INT_MAX, ymax=INT_MAX;
    for(int i=1;i<argc;++i)
    {
        const auto arg=std::string(argv[i]);
        if(arg=="--error-on-misexposure")
        {
            errorOnMisexposure=true;
        }
        else if(arg=="-xrange" || arg=="--xrange")
        {
            ++i;
            if(i>=argc) return requireParam(arg);
            const auto arg=std::string(argv[i]);
            try
            {
                std::tie(xmin,xmax)=parseRange(arg);
            }
            catch(std::exception const& e)
            {
                std::cerr << "Parsing x range failed: " << e.what() << "\n";
                return 1;
            }
        }
        else if(arg=="-yrange" || arg=="--yrange")
        {
            ++i;
            if(i>=argc) return requireParam(arg);
            const auto arg=std::string(argv[i]);
            try
            {
                std::tie(ymin,ymax)=parseRange(arg);
            }
            catch(std::exception const& e)
            {
                std::cerr << "Parsing y range failed: " << e.what() << "\n";
                return 1;
            }
        }
        else if(arg=="--help" || arg=="-h")
        {
            return usage(argv[0],0);
        }
        else if(arg.substr(0,1)!="-")
        {
            filename=arg;
        }
        else
        {
            std::cerr << "Unknown option " << arg << "\n";
            return 1;
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

    std::cerr << "Convering raw data to image...\n";
    libRaw.raw2image();
    const auto& pre_mul=libRaw.imgdata.color.pre_mul;
    const float preMulMax=*std::max_element(std::begin(pre_mul),std::end(pre_mul));
    const float rgbCoefs[4]={pre_mul[0]/preMulMax,pre_mul[1]/preMulMax,pre_mul[2]/preMulMax,
                             (pre_mul[3] ? pre_mul[3] : pre_mul[1])/preMulMax};
    if(xmax>sizes.iwidth ) xmax=sizes.iwidth;
    if(ymax>sizes.iheight) ymax=sizes.iheight;
    return printAverageColor(libRaw,libRaw.imgdata.image,xmin,xmax,ymin,ymax,libRaw.imgdata.color,rgbCoefs);
}

