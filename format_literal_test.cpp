
// Note: This file has to be processed with extract_fx before it can be compiled. This is handled by the supplied CMakeLists.txt


#include "format_literal.h"
#include <iostream>

#define FSTRING extracted_string("Number: {}", 3)

#define ABS(X) (X) < 0 ? -(X) : (X)

int main()
{
    println(extracted_string("Number: {}", 1));
    std::cout << extracted_string("Number: {}", 2) << std::endl;
    std::cout << FSTRING << std::endl;
    std::cout << extracted_string("Number: {}", ABS(-4)) << std::endl;
    std::cout << std::format("Compilinf file: {}", __FILE__) << std::endl;
    
    int value = 17;
    println(extracted_string("value={}", value));
}

