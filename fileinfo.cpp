#include <libraw/libraw.h>
#include <iostream>
#include <iomanip>
#include <cstddef>

using std::size_t;

int usage(const char* argv0, int returnValue)
{
    std::cerr << "Usage: " << argv0 << " filename\n";
    return returnValue;
}

template<typename T>
std::size_t getNumLength(T number)
{
    std::ostringstream s;
    s << number;
    return s.str().size();
}

template<typename T, size_t N, size_t M>
void printMatrix(std::ostream& stream, T(&matrix)[N][M])
{
    size_t colWidths[M]={};
    for(size_t i=0;i<N;++i)
        for(size_t j=0;j<M;++j)
            colWidths[j]=std::max(colWidths[j],getNumLength(matrix[i][j]));

    for(size_t i=0;i<N;++i)
    {
        for(size_t j=0;j<M;++j)
            stream << std::setw(colWidths[j]+2) << std::left << matrix[i][j];
        stream << "\n";
    }
}

template<typename T, size_t N>
void printArray(std::ostream& stream, T (&array)[N])
{
    for(size_t i=0;i<N;++i)
        stream << array[i] << "  ";
}

std::pair<ushort,ushort> calcMinMax(LibRaw& libRaw, const ushort (*data)[4], const int w, const int h)
{
    enum {BAYER_RED,BAYER_GREEN1,BAYER_BLUE,BAYER_GREEN2};
    const auto col00=libRaw.COLOR(0,0), col01=libRaw.COLOR(0,1), col10=libRaw.COLOR(1,0), col11=libRaw.COLOR(1,1);
    const int stride=w;
    ushort minPix=0xffff, maxPix=0;
    for(int Y=0; Y<h; Y+=2)
    {
        for(int X=0; X<w; X+=2)
        {
            const ushort pixelTopLeft    =data[X+0+(Y+0)*stride][col00];
            const ushort pixelTopRight   =data[X+1+(Y+0)*stride][col01];
            const ushort pixelBottomLeft =data[X+0+(Y+1)*stride][col10];
            const ushort pixelBottomRight=data[X+1+(Y+1)*stride][col11];
            if(pixelTopLeft     < minPix) minPix = pixelTopLeft;
            if(pixelTopRight    < minPix) minPix = pixelTopRight;
            if(pixelBottomLeft  < minPix) minPix = pixelBottomLeft;
            if(pixelBottomRight < minPix) minPix = pixelBottomRight;
            if(pixelTopLeft     > maxPix) maxPix = pixelTopLeft;
            if(pixelTopRight    > maxPix) maxPix = pixelTopRight;
            if(pixelBottomLeft  > maxPix) maxPix = pixelBottomLeft;
            if(pixelBottomRight > maxPix) maxPix = pixelBottomRight;
        }
    }
    return {minPix,maxPix};
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
    libRaw.raw2image();
    std::cout << "Make: " << idata.make << "\n";
    std::cout << "Model: " << idata.model << "\n";
    std::cout << "Colors: " << idata.colors << "\n";
    std::cout << "Color components: " << idata.cdesc << "\n";
    std::cout << "                ╭───╮\n";
    std::cout << "Color component │" << idata.cdesc[libRaw.COLOR(0,0)] << " " << idata.cdesc[libRaw.COLOR(0,1)] << "│\n"
                 "          order:│" << idata.cdesc[libRaw.COLOR(1,0)] << " " << idata.cdesc[libRaw.COLOR(1,1)] << "│\n"
                 "                ╰───╯\n";
    std::cout << "Raw size: " << sizes.raw_width << "×" << sizes.raw_height << "\n";
    std::cout << "Visible size: " << sizes.width << "×" << sizes.height << "\n";
    std::cout << "Margins{left: " << sizes.left_margin << ", top: " << sizes.top_margin << "}\n";
    std::cout << "iSize: " << sizes.iwidth << "×" << sizes.iheight << "\n";
    std::cout << "Pixel aspect: " << sizes.pixel_aspect << "\n";
    const auto minMax = calcMinMax(libRaw, libRaw.imgdata.image, sizes.iwidth, sizes.iheight);
    std::cout << "Black level: " << libRaw.imgdata.rawdata.color.black << ", actual min: " << minMax.first << "\n";
    std::cout << "White level: " << libRaw.imgdata.rawdata.color.maximum << ", actual max: " << minMax.second << "\n";

    std::cout << "cmatrix:\n"; printMatrix(std::cout,libRaw.imgdata.rawdata.color.cmatrix);
    std::cout << "rgb_cam:\n"; printMatrix(std::cout,libRaw.imgdata.rawdata.color.rgb_cam);
    std::cout << "cam_xyz:\n"; printMatrix(std::cout,libRaw.imgdata.rawdata.color.cam_xyz);
    std::cout << "white:\n";   printMatrix(std::cout,libRaw.imgdata.rawdata.color.white);
    std::cout << "cam_mul: "; printArray(std::cout,libRaw.imgdata.rawdata.color.cam_mul); std::cout << "\n";
    std::cout << "pre_mul: "; printArray(std::cout,libRaw.imgdata.rawdata.color.pre_mul); std::cout << "\n";
    std::cout << "cblack: ";  printArray(std::cout,libRaw.imgdata.rawdata.color.cblack); std::cout << "\n";
}

