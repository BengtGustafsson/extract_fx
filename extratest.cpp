
// Note: This file has to be processed with extract_fx before it can be compiled. This is handled by the supplied CMakeLists.txt


#include "format_literal.h"
#include <iostream>

using std::println;         // Add std::println to overload set.

void extratest()
{
    int value = 42;
    println(f"Extra {value=}");
}

