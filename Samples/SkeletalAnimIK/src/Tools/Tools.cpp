#include <cerrno>  // errno
#include <cstring> // strerror()
#include <fmt/base.h>
#include <fmt/color.h>
#include <fstream>
#include <iostream>

#include <Tools/Tools.h>

std::string Tools::getFilenameExt(std::string filename)
{
    size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos)
    {
        return filename.substr(pos + 1);
    }
    return std::string();
}

std::string Tools::loadFileToString(std::string fileName)
{
    std::ifstream inFile(fileName, std::ios::binary);
    std::string str;

    if (inFile.is_open())
    {
        str.clear();
        // allocate string data (no slower realloc)
        inFile.seekg(0, std::ios::end);
        str.reserve(inFile.tellg());
        inFile.seekg(0, std::ios::beg);

        str.assign((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        inFile.close();
    }
    else
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: could not open file {}\n", __FUNCTION__, fileName);
        fmt::print(stderr, fg(fmt::color::red), "{} error: system says '{}'\n", __FUNCTION__, strerror(errno));
        return std::string();
    }

    if (inFile.bad() || inFile.fail())
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: error while reading file {}\n", __FUNCTION__, fileName);
        inFile.close();
        return std::string();
    }

    inFile.close();
    fmt::print("{}: file {} successfully read to string\n", __FUNCTION__, fileName);
    return str;
}
