# Pre-preprocessor handling f/x literals in C++ code

This program was written to support an upcoming proposal to introduce the idea of f-literals in C++ as a more or less textual transformation performed by the preprocessor. It shows the possibility to embed regular C++ expressions in f-literals without much need for knowledge of C++ expressions in the preprocessor.

Here is the proposal as of October 25: [Proposal by Hadriel Kaplan](http://api.csswg.org/bikeshed/?url=https://raw.githubusercontent.com/hadrielk/cpp-proposals/main/f-string/f-string-r2.bs&force=1)

## Examples

```C++
// Basically a f-literal just lifts the contents of any {} pair inside it to a separate function 
// parameter expression after the literal, and encloses all of it in a std::format call.
f"Value: {std::sqrt(2.0)}"

->

std::format("Value: {}", std::sqrt(2.0))


// x-literals are used when std::format is not the intended target. x-literals don't enclose the 
// resulting parameter list in any function call.
std::print(xR"xy(The system works also with {"raw"} literals
with the R after the x or f.)xy");

->

std::print(R"xy(The system works also with {} literals
with the R after the x or f.)xy", "raw");

// expression-fields can end with a : followed by a format-spec. The format-spec can contain
// nested expression-fields (which can't end in colon).
f"Two decimals: {std::sqrt(2.0):.2}, N decimals: {std::sqrt(2.0):.{N}}"

->

std::format("Two decimals: {:.2}, N decimals: {std::sqrt(2.0):.{N}}", std::sqrt(2.0), std::sqrt(2.0), N);

// Comments and nested literals are available inside extraction-expressions without
// any special escaping of quotes.
std::cout << f"The number of large values is: {
    std::count_if(myContainer.begin(), myContainer.end(), [&](auto& elem) { 
         return elem.value > largeVal;  // The value member is compared.
    })
}, where the limit is {largeVal}";

```

## Implementation compared to the proposal

So far the proposal is not concrete enough to be able to tell if this implementation is conformant. This implementation is a proof
of concept for a variant of the proposal where expressions-fields can be written verbatim as if they were outside the literal, i.e.
with non-escaped string literals, with comments, ternary ?: operators and :: scoping operators.

A quick debug output feature available in Python f-literals is also implemented in extract_fx: If an expression ends with a =
character (apart from whitespace) the expression is output as a label too, so to easily print values of some variables just do:

```c++
std::print(f"{a=}, {b = }");

// Output:
a=17, b = 42
```

expression-fields in f/x-literals can span multiple lines without any special continuation lines.

While tools that scan source code would be required to do more work to find the end of a f/x literal than a regular literal most
such tools would have to the full processing of the contents of f/x literals anyway as they contain executable code, subject to for
instance static analysis. so this is not a strong argument for requiring escaping of quotes inside expression-fields.

Some tools, like the github C++ code renderer, however has a too limited parsing capability which is obvious from the coloring
above.

Here is a grammar for the string-literal [grammar](grammar.md)

## Limitations

This implementation has some limitations as it is a pre-preprocessor which does not do any other preprocessor tasks:

1. As #include files are not actually included expression-fields in f/x literals in included files are not extracted.
2. When a f/x literal is written adjacent to another literal (f/x or not) the expression-fields are _not_ moved to the end of the sequence of literals.
3. Any errors immediately stop the pre-processing with an error message. No resynchronization/restart is attempted. (Most errors are
    related to premature ending of the input anyway).
4. While #line directives are emitted to place any errors in the expression-fields in the correct position this can't be done in expression-fields in fx-literals in #defines.


## Building extract_fx

Just compile the extract_fx.cpp file using a C++20 compiler. Older C++ versions may also work. Optionally use the supplied
CMakeLists.txt file to build it. 

As the example in format_literal_test.cpp uses std::println the compiler must have this implemented, which is currently only true for the latest Visual Studio 2022 compilers.

The CMakeLists file includes a file `fx_file.cmake` which contains a function`target_fx_file` that can be called with a CMake target name and a number source files in the same way as CMake's own `target_sources`. This causes the files to be set up for pre-preprocessing with extract_fx and the resulting output file (which is written in the `extracted` subdirectory of the current build directory) to be added to the project in a special *source group* called Extracted. this provides a nearly invisible integration into Visual Studio and allows using f and x literals in source files which are added to their projects using the `target_fx_file` macro.

## Experimentation environment.

A file `format_literal.h` is supplied which contains a subclass of std::string called `extracted_string`. Overloads of `print` and
`println` are provided which take just this std::string subclass. With this the supplied demo program `format_literal_test.cpp` can
be compiled. It contains the following uses of f-literals. std::println is brought into of the overload set by a using declaration here to show how the new `println` overload interacts with the overloads already in std.

```C++
#include "format_literal.h"
#include <iostream>

int main()
{
    println(f"Number: {1}");
    std::cout << f"Number: {2}";
}
```

format_literal.h included above demonstrates one idea of how we could get rid of x-literals: Anytime the f-literal is used where a `std::string` or
`std::string_view` is required the subclass acts as a stand-in for the base class whereas in the cases where a `std::format_string<Args...>` is
the first parameter today an overload taking just an `extracted_string` can be introduced without interfering with the normal operation of
functions such as `std::print` and `std::println`. `std::format` should not have such an overload though, as calling std::format on a
f-literal is surely a mistake.

## extract_fx pre-preprocessor usage

Without command line arguments extract_fx works like a Unix filter reading from stdin and writing to stdout. Note that with long f/x
literals and/or expression-fields output will be withheld until enough input lines have been seen. This also happens for multiline comments.

An option **--name** is available for experimentation. It controls the name of the function that f-literals are wrapped in. It defaults to `std::format`. The CMakeLists macro `target_extract_file` adds a custom build step which has a **--name** parameter set to `extracted_string` but this can easily be changed.

An option **--test** causes the built in unit tests to run. This can't be combined with any other parameters.

With one filename parameter extract_fx reads from this file any writes the result to stdout.

With two filename parameters extract_fx reads from the first file and writes to the second. Using the same filename for input and output is not supported.
