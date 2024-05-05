#include "util.h"
#include <iostream>

int main()
{
    uint64_t tmp = 0;
    uint64_t begin = meha::GetCurrentMS();

    for (int i = 0; i < 655360; i++)
    {
        tmp += meha::GetCurrentMS();
        tmp %= 65536;
    }

    uint64_t end = meha::GetCurrentMS();
    std::cout << end - begin << std::endl;
    return 0;
}
