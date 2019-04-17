#include <iostream>

#include <QApplication>
#include "MainWindow.h"

using std::size_t;

int usage(const char* argv0, int returnValue)
{
    std::cerr << "Usage: " << argv0 << " [directory]\n";
    return returnValue;
}

int requireParam(std::string const& opt)
{
    std::cerr << "Option " << opt << " requires parameter\nUse --help to see usage\n";
    return 1;
}

int main(int argc, char** argv)
{
    QApplication app(argc,argv);

    std::string dir;
    for(int i=1;i<argc;++i)
    {
        const auto arg=std::string(argv[i]);
        if(arg=="--help" || arg=="-h")
        {
            return usage(argv[0],0);
        }
        else if(arg.substr(0,1)!="-")
        {
            if(!dir.empty())
            {
                std::cerr << "Directory specified second time with \"" << arg << "\"\n";
                return 1;
            }

            dir=arg;
        }
        else
        {
            std::cerr << "Unknown option " << arg << "\n";
            return 1;
        }
    }

    MainWindow window(dir);
    window.show();
    app.processEvents();
    return app.exec();
}
