#include <libraw/libraw.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstddef>
#include <vector>
#include <limits>
#include <cassert>

inline int usage(const char* argv0, int returnValue)
{
    std::cerr << "Usage: " << argv0 << " filename scanLineNum\n";
    return returnValue;
}

void printScanLineData(LibRaw& libRaw, const ushort (*img)[4], const int w, const int h, const int scanLineY)
{
    std::vector<unsigned> valuesRed;
    std::vector<unsigned> valuesGreen1;
    std::vector<unsigned> valuesGreen2;
    std::vector<unsigned> valuesBlue;
    int tooBlackPixelCount=0, tooWhitePixelCount=0;
    std::cerr << "Extracting scanline...\n";
    for(int y=scanLineY;y<=scanLineY+1;++y)
    {
        for(int x=0;x<w;++x)
        {
            const auto colIndex=libRaw.FC(y,x);
            const auto pixel=img[x+y*w][colIndex];

            switch(colIndex)
            {
            case 0: valuesRed   .emplace_back(pixel); break;
            case 1: valuesGreen1.emplace_back(pixel); break;
            case 2: valuesBlue  .emplace_back(pixel); break;
            case 3: valuesGreen2.emplace_back(pixel); break;
            default: assert(!"Must not get here!");
            }
        }
    }
    assert(valuesRed.size()==valuesGreen1.size());
    assert(valuesGreen1.size()==valuesGreen2.size());
    assert(valuesGreen2.size()==valuesBlue.size());
    for(unsigned i=0;i<valuesRed.size();++i)
        std::cout << i << ',' << valuesRed[i] << ',' << valuesGreen1[i] << ',' << valuesGreen2[i] << ',' << valuesBlue[i] << '\n';
}

int main(int argc, char** argv)
{
    if(argc!=3)
        return usage(argv[0],1);
    try
    {
        const char*const filename=argv[1];
        std::size_t pos=0;
        const auto scanLineNum=std::stoul(argv[2], &pos, 0);
        if(argv[2][pos]!=0)
        {
            std::cerr << "Invalid trailing characters after scan line number\n";
            return 1;
        }
        LibRaw libRaw;
        libRaw.open_file(filename);
        const auto& sizes=libRaw.imgdata.sizes;
        if(scanLineNum>=sizes.iheight)
        {
            std::cerr << "Too large scan line number: image height is " << sizes.iheight << "\n";
            return 1;
        }

        std::cerr << "Unpacking raw data...\n";
        if(const auto error=libRaw.unpack())
        {
            std::cerr << "Failed to unpack: error " << error << "\n";
            return 2;
        }

        const auto& idata=libRaw.imgdata.idata;

        std::cerr << "Convering raw data to image...\n";
        libRaw.raw2image();
        printScanLineData(libRaw, libRaw.imgdata.image, sizes.iwidth, sizes.iheight, scanLineNum);
    }
    catch(...)
    {
        std::cerr << "Can't parse scan line number\n";
        return 1;
    }
}
