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
unsigned getNumLength(T number)
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
    std::cout << "Make: " << idata.make << "\n";
    std::cout << "Model: " << idata.model << "\n";
    std::cout << "Colors: " << idata.colors << "\n";
    std::cout << "Color components: " << idata.cdesc << "\n";
    std::cout << "Raw size: " << sizes.raw_width << "x" << sizes.raw_height << "\n";
    std::cout << "Visible size: " << sizes.width << "x" << sizes.height << "\n";
    std::cout << "Margins{left: " << sizes.left_margin << ", top: " << sizes.top_margin << "}\n";
    std::cout << "iSize: " << sizes.iwidth << "x" << sizes.iheight << "\n";
    std::cout << "Pixel aspect: " << sizes.pixel_aspect << "\n";
    std::cout << "Black level: " << libRaw.imgdata.rawdata.color.black << "\n";
    std::cout << "White level: " << libRaw.imgdata.rawdata.color.maximum << "\n";

    std::cout << "cmatrix:\n"; printMatrix(std::cout,libRaw.imgdata.rawdata.color.cmatrix);
    std::cout << "rgb_cam:\n"; printMatrix(std::cout,libRaw.imgdata.rawdata.color.rgb_cam);
    std::cout << "cam_xyz:\n"; printMatrix(std::cout,libRaw.imgdata.rawdata.color.cam_xyz);
    std::cout << "white:\n";   printMatrix(std::cout,libRaw.imgdata.rawdata.color.white);
    std::cout << "cam_mul: "; printArray(std::cout,libRaw.imgdata.rawdata.color.cam_mul); std::cout << "\n";
    std::cout << "pre_mul: "; printArray(std::cout,libRaw.imgdata.rawdata.color.pre_mul); std::cout << "\n";
    std::cout << "cblack: ";  printArray(std::cout,libRaw.imgdata.rawdata.color.cblack); std::cout << "\n";
}

