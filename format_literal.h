
#pragma once

#include <print>
#include <format>
#include <memory>
#include <iostream>

// This class just exists to avoid _any_ string from being passable to print. I don't know why this would be important, but it seems that this was the initiator of the f/x subdivision.
class extracted_string : public std::string {
};

template<size_t Count, typename... Args> auto extract_string(std::format_string<Args...> fmt, Args&&... args) {
    static_assert(Count == sizeof...(Args), "Too many extracted expressions, did you use operator comma?");
    return extracted_string(std::format(std::move(fmt), std::forward<Args>(args)...));
}

// Has to be repeated for each other function we want f literals to work with, and which does not take a
// std::string today... there should not be many.
inline void print(const extracted_string& str) { std::cout << str; }
inline void println(const extracted_string& str) { std::cout << str << "\n"; }     // Or maybe endl?
