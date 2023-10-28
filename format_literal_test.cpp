
// Note: This file has to be processed with extract_fx before it can be compiled. This is handled by the supplied CMakeLists.txt


#include "format_literal.h"
#include <iostream>


int main()
{
    println(format_literal("Number: {}", 1));
    std::cout << format_literal("Number: {}", 2);
    std::runtime_format("");
}

