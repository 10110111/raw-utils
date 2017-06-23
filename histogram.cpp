#include <libraw/libraw.h>
#include <iostream>
#include <sstream>
#include <cstddef>
#include <vector>
#include <limits>

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
    printImageHistogram(libRaw.imgdata.image,sizes.iwidth*sizes.iheight);
}
