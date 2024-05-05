#include "config.h"

namespace meha
{

std::ostream& operator<<(std::ostream& out, const ConfigVarBase& cvb)
{
    out << cvb.getName() << ": " << cvb.toString();
    return out;
}
} // namespace meha
