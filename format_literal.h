
#pragma once

#include <print>
#include <format>
#include <memory>

// This class just exists to avoid _any_ string from being passable to print. I don't know why this would be important, but it seems that this was the initiator of the f/x subdivision.
class extracted_string : public std::string {
private:
    extracted_string(std::string&& src) : std::string(std::move(src)) {}
    template<typename... Args> friend extracted_string format_literal(std::format_string<Args...> fmt, Args&&... args);
};


template<typename... Args> extracted_string format_literal(std::format_string<Args...> fmt, Args&&... args) { return std::format(std::move(fmt), std::forward<Args>(args)...); }


// Stupid implementation... Has to be repeated for each other function we want f literals to work with, and which does not take a
// std::string today... there should not be many.
void print(const extracted_string& str) { std::print("{}", static_cast<const std::string&>(str)); }
void println(const extracted_string& str) { std::println("{}", static_cast<const std::string&>(str)); }


