
// Note: This file has to be processed with extract_fx before it can be compiled. This is handled by the supplied CMakeLists.txt


#include "format_literal.h"
#include <iostream>

#define FSTRING f"Number: {3}"

#define ABS(X) (X) < 0 ? \
                   -(X)  \
               :         \
                    (X)

using std::println;         // Add std::println to overload set.

extern void extratest();

int main()
{
    println(f"Number: {1, 2}, {2.718} and {std::sqrt(2):.{3+1}}");           // This calls the println overload in format_literal.
    println(x"Numbers: {1}, {3.1416} and {std::sqrt(3) *
    3  //TJo
    + 4:4e}");             // This calls a std::println overload specialization.

    std::cout << f"Number: {2}" << std::endl;
    std::cout << FSTRING << std::endl;
    std::cout << f"Number: {ABS(-4)}" << std::endl;
    std::cout << std::format(x"Compiling file: {__FILE__}") << std::endl;
    
    int value = 17;
    println(f"{value=}");
    extratest();
}

