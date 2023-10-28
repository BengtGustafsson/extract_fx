# Pre-preprocessor handling f/x literals in C++ code

This program was written to support an upcoming proposal to introduce the idea of f-literals in C++ as a more or less textual transformation performed by the preprocessor..

Here is the proposal as of October 10: [Proposal by Hadriel Kaplan](http://api.csswg.org/bikeshed/?url=https://raw.githubusercontent.com/hadrielk/cpp-proposals/main/f-string/f-string-r2.bs&force=1)

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


```

## Implementation compared to the proposal

So far the proposal is not concrete enough to be able to tell if this implementation is conformant. This implementation is a proof
of concept for a variant of the proposal where expressions-fields can be written verbatim as if they were outside the literal, i.e.
with non-escaped string literals and C style comments.

expression-fields in regular f/x literals must use continuation lines ending in backslash if they span multiple lines.
Expression-fields in raw literals don't need continuation lines as they are part of the raw literal. *NOTE: It is not entirely
obvious (and* *not needed for parseability) to require backslashes to end lines in expression-fields in regular f/x literals.*

While tools that scan source code would be required to do more work to find the end of a f/x literal than a regular literal most
such tools would have to the full processing of the contents of f/x literals anyway as they contain executable code, subject to for
instance static analysis. so this is not a strong argument for requiring escaping of quotes inside expression-fields.

## Limitations

This implementation has some limitations as it is a pre-preprocessor which does not do any other preprocessor tasks:

1. As #include files are not actually included expression-fields in f/x literals in included files are not extracted.

2. f/x literals inside preprocessor macros are not processed (as lines starting with a # are just dumped to the output file).

3. When a f/x literal is written directly before another literal (f/x or not) the expression-fields are _not_ moved to the end of the sequence of literals. This
  could be fixed.

4. Any errors immediately stop the processing with an error message. No resynchronization/restart is attempted.


## Building extract_fx

Just compile the extract_fx.cpp file using a C++20 compiler. Older C++ versions may also work. Optionally use the supplied
CMakeLists.txt file to build it.

## Usage

Without command line arguments extract_fx works like a Unix filter reading from stdin and writing to stdout. Note that with long f/x
literals and/or expression-fields output will be withheld until enough input lines have been seen.

With one parameter extract_fx reads from this file any writes the result to stdout unless the parameter is --test which causes the built in
unit tests to run.

With two filename parameters extract_fx reads from the first file and writes to the second. Using the same filename for input and output is not allowed.