#ifndef INCLUDE_ONCE_09AE59ED_5EAC_4478_A4CD_D2F22F0E3300
#define INCLUDE_ONCE_09AE59ED_5EAC_4478_A4CD_D2F22F0E3300

#include <vector>
#include <string>
#include <string_view>

struct CmdLineOption
{
    std::vector<std::string> names;
    std::string valueName;
    std::string description;
    CmdLineOption(std::vector<std::string> const& names, std::string_view description)
        : names(names), description(description)
    {}
    CmdLineOption(std::vector<std::string> const& names, std::string const& valueName, std::string const& description)
        : names(names), valueName(valueName), description(description)
    {}
};
void showHelp(std::ostream& s, std::string_view argv0, std::vector<CmdLineOption> const& options, std::string_view const& positionalArgSyntax);

#endif
