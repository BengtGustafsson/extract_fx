
// Note: This file has to be processed with extract_fx before it can be compiled. This is handled by the supplied CMakeLists.txt


#include"format_literal.h"
#include <iostream>


int main()
{
    println(extracted_string("Number: {}", 1));
    std::cout << extracted_string("Number: {}", 2) << std::endl;

    int value = 17;
    println(extracted_string("value={}", value));
}

