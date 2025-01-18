#pragma once

#include <string>
#include <sstream>

namespace meha::utils {

template<typename InputIterator>
std::string Join(InputIterator begin, InputIterator end, const std::string& delimiter)
{
    std::stringstream ss;
    for (InputIterator it = begin; it != end; ++it) {
        if (it != begin) {
            ss << delimiter;
        }
        ss << *it;
    }
    return ss.str();
}

}