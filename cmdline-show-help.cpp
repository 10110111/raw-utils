#include "cmdline-show-help.hpp"
#include <cassert>
#include <climits>
#include <iomanip>
#include <iostream>
#ifdef __unix__
#   include <sys/ioctl.h>
#   include <unistd.h>
#   include <stdio.h>
#elif defined _WIN32
#   include <windows.h>
#endif

namespace
{

std::vector<std::string_view> split(std::string_view const str, char const sep)
{
    std::vector<std::string_view> strings;
    std::size_t sepPos, startPos=0;
    while((sepPos=str.find(sep, startPos)) != str.npos)
    {
        strings.emplace_back(str.substr(startPos,sepPos-startPos));
        startPos=sepPos+1;
    }
    strings.emplace_back(str.substr(startPos));
    return strings;
}

std::vector<std::string> wordWrap(std::string const& longLine, const int maxWidth)
{
    const auto words=split(longLine,' ');
    std::vector<std::string> lines;
    int col=0;
    std::string currentLine;
    const auto endCurrentLine=[&lines, &currentLine, &col, maxWidth]
    {
        while(!currentLine.empty() && currentLine.back()==' ')
            currentLine.pop_back();
        if(currentLine.length() < maxWidth)
            currentLine += '\n';
        lines.emplace_back(std::move(currentLine));
        currentLine.clear();
        col=0;
    };
    for(const auto& word : words)
    {
        if(col+word.length()+1 < maxWidth)
        {
            currentLine += word;
            currentLine += ' ';
            col += word.length()+1;
        }
        else if(col+word.length()+1 == maxWidth)
        {
            currentLine += word;
            endCurrentLine();
        }
        else if(col+word.length() == maxWidth)
        {
            currentLine += word;
            endCurrentLine();
        }
        else if(word.length()+1 < maxWidth)
        {
            if(!currentLine.empty())
                endCurrentLine();
            currentLine = std::string(word)+' ';
            col = word.length()+1;
        }
        else if(word.length()+1 == maxWidth)
        {
            if(!currentLine.empty())
                endCurrentLine();
            lines.emplace_back(std::string(word)+'\n');
        }
        else if(word.length() == maxWidth)
        {
            if(!currentLine.empty())
                endCurrentLine();
            lines.emplace_back(word);
        }
        else
        {
            if(!currentLine.empty())
                endCurrentLine();
            for(int i=0; i<word.length(); i+=maxWidth)
                lines.emplace_back(word.substr(i, maxWidth));
            if(lines.back().length() < maxWidth)
            {
                currentLine = lines.back()+' ';
                lines.pop_back();
                col=currentLine.length();
            }
        }
    }
    if(!currentLine.empty())
        endCurrentLine();
    return lines;
}

int getConsoleWidth(std::ostream& s)
{
    int width=INT_MAX; // fallback is to not wrap any output
#ifdef __unix__
    struct winsize w;
    if(ioctl(&s==&std::cout ? STDOUT_FILENO : STDERR_FILENO, TIOCGWINSZ, &w)<0)
        return width;
    width=w.ws_col;
#elif defined _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(&s==&std::cout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE), &csbi);
    width=csbi.dwSize.X;
#endif
    return width;
}

}

void showHelp(std::ostream& s, std::string_view argv0, std::vector<CmdLineOption> const& options, std::string_view const& positionalArgSyntax)
{
    s << "Usage: " << argv0 << " [options...] " << positionalArgSyntax << "\n";
    s << "\nOptions:\n";

    std::vector<std::pair<std::string,std::string>> allOptionsFormatted;
    int maxNameLen=0;
    for(const auto& option : options)
    {
        std::string namesFormatted="  ";
        for(const auto& name : option.names)
            namesFormatted += name + ", ";
        if(!namesFormatted.empty())
            namesFormatted.resize(std::max(0,int(namesFormatted.length())-2)); // remove trailing ", "
        if(!option.valueName.empty())
            namesFormatted += " " + option.valueName;
        if(namesFormatted.size() > maxNameLen)
            maxNameLen=namesFormatted.size();
        allOptionsFormatted.emplace_back(std::make_pair(namesFormatted, option.description));
    }

    const auto consoleWidth=getConsoleWidth(s);
    for(const auto& [name, explanation] : allOptionsFormatted)
    {
        const auto namesColumnWidth=maxNameLen+2;
        if(consoleWidth <= namesColumnWidth*3/2) // Attempts to wrap are useless in this case
        {
            s << name.substr(1) << "\n    " << explanation << '\n';
            continue;
        }
        s << std::setw(namesColumnWidth) << std::left << name;
        // We'll indent wrapped lines to make it easy to spot where the next option begins
        assert(!explanation.empty());
        s << explanation[0];
        const auto wrappedExplanation=wordWrap(explanation.substr(1), consoleWidth-namesColumnWidth-1);
        for(const auto& line : wrappedExplanation)
        {
            s << line;
            if(&line != &wrappedExplanation.back())
                s << std::string(namesColumnWidth+1, ' ');
        }
    }
}
