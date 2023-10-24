// f/x literal extractor for C++ preprocessor.
// By bengt.gustafsson@beamways.com
// MIT license.

#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <cassert>

class FxExtractor {
public:
    FxExtractor(std::ostream& outFile, std::istream& inFile) : m_outFile(outFile), m_inFile(inFile) {}

    bool process() {
        while (getLine()) {
            // Scan for string literals, skipping preprocessor directives and comments. For now don't skip #ifdef'ed out lines
            const char* s = m_inLine.c_str();       // We use \0 to see the end. This way we can always access s[1] to see two character tokens such as // and /*
            while (*s != '\0') {
                if (*s == '#') {            // Just copy input line if it is an include directive
                    m_outLine = m_inLine;
                    if (!handleContinuations())
                        return false;

                    break;
                }
                else if (*s == '/' && s[1] == '/') {     // Add the rest of the line and output it if a // comment starts
                    m_outLine.append(s, m_inLine.c_str() + m_inLine.size());
                    if (!handleContinuations())
                        return false;

                    break;
                }
                else if (*s == '/' && s[1] == '*') {     // C comment starts. Copy character by character until it ends, possibly getting more lines as we go.
                    const char* start = handleLongComment(s);
                    if (start == nullptr)
                        return false;
                    
                    m_outLine.append(start, s);
                }
                else if (*s == '"') {      // String literal.
                    std::string lit = processLiteral(m_outLine, s);
                    if (lit.empty())
                        return false;
                    m_outLine += lit;
                }
                else
                    m_outLine += *s++;
            }

            putLine();
        }

        return true;
    }

private:
    bool getLine() {
        if (!m_inFile || m_inFile.eof())
            return false;

        getline(m_inFile, m_inLine);
        m_lineNo++;
        return true;
    }
    void putLine() {
        m_outFile << m_outLine;
        if (!m_inFile.eof())
            m_outFile << "\n";

        m_outLine.clear();
    }
    bool nextLine() {
        putLine();
        return getLine();
    }

    // Used for preprocessor directives and // comments that may continue with continuations.
    // The first line is supposed to be in m_outLine already.
    // Finish by loading the first line that is NOT a continuation. Also handle that there is no continuation.
    bool handleContinuations() {
        while (true) {
            const char* p = m_outLine.c_str() + m_outLine.size();     // Point at the \0 initially
            while (p > m_outLine.c_str() && std::isspace(static_cast<unsigned char>(*--p)))
                ;

            if (*p != '\\')
                break;

            if (!nextLine()) {
                std::cerr << "Input ends with a line ending in \\.\n";
                return false;
            }

            m_outLine = m_inLine;
        }

        return true;
    }

    // Pass by a C comment, outputting any complete lines encountered. Return start which points in the last line (or same line if
    // comment doesn't span multiple lines.
    const char* handleLongComment(const char*& s) {
        assert(*s == '/' && s[1] == '*');
        const char* start = s;
        s += 2;
        while (*s == 0 || *s != '*' || s[1] != '/') {
            if (*s == '\0') {
                // Line ended in comment. output line and reload buffer.
                m_outLine.append(start, s);
                if (!nextLine()) {
                    std::cerr << "/* unmatched to the end of the input.\n";
                    return nullptr;
                }

                s = m_inLine.c_str();
                start = s;
            }
            else
                s++;
        }

        s += 2;
        return start;
    }

    std::string processLiteral(std::string& out, const char*& s) {
        bool raw = false;
        char fx = 0;       // Can be 0, f or x
        const char* f = s;
        if (f > m_inLine.c_str() && f[-1] == 'R') {
            raw = true;
            f--;
        }
        if (f > m_inLine.c_str()) {
            char c = std::tolower(static_cast<unsigned char>(f[-1]));
            if (c == 'f' || c == 'x') {
                fx = c;
                f--;
            }
        }

        size_t n = s - f;       // 0 - 2 characters to remove as we have to remove the f or x which may be followed by R
        out.erase(out.size() - n, n);
        return processLiteral(s, raw, fx);
    }

    std::string processLiteral(const char*& s, bool raw, char fx) {
        std::string ret;

        std::string prefix;
        assert(*s == '"');
        s++;

        // Output the std::format( for f literals only
        if (fx == 'f')
            ret = "std::format(";

        // Handle start of literal
        if (raw) {      // Collect prefix
            auto begin = s;
            while (*s != '(') {
                if (*s == 0) {
                    std::cerr << "Line " << m_lineNo << " ends in a raw literal prefix. There must be a ( before the end of line after R\".\n";
                    return "";
                }
                s++;
            }
            prefix = std::string(begin, s);
            s++;        // Pass (
            ret += "R\"" + prefix + "(";
        }
        else
            ret += '"';

        // Process the actual literal contents.
        std::vector<std::string> inserts;       // Can't be string_views as R literals span lines and we only store the last line.
        while (true) {
            if (raw) {      // Do raw line ends and try to find prefix
                if (*s == 0) {
                    if (!getLine()) {
                        std::cerr << "Input ends in raw literal.\n";
                        return "";
                    }
                    ret += '\n';
                    s = m_inLine.c_str();
                }
                if (*s == ')') {       // Look for prefix after ) and " after that (even first on a line!)
                    auto c = prefix.c_str();
                    auto p = s + 1;  // Pass )
                    while (*c != '\0') {
                        if (*p++ != *c++)
                            break;
                    }
                    if (p - s == prefix.size() + 1 && *p == '"') {
                        // Raw literal ended
                        ret += ")" + prefix;
                        s = p;
                        break;
                    }
                }
            }
            else {      // Handle continuation lines and escaped quotes in non-raw literals
                if (*s == '\\') {
                    // Quoted something or continuation line, which can now have spaces and tabs before the \0. So after \ we must check if
                    // rest of line is whitespace and if so go to next line.
                    ret += *s++;        // Pass backslash, it is always copied to the output.
                    while (true) {
                        const char* start = s;
                        if (*s == '\0') {     // Continuation line detected, go get next line.
                            ret.append(start, s);  // Include the spaces in the output.
                            ret += '\n';           // And a newline
                            if (!getLine()) {
                                std::cerr << "Input ends with a \\ last on a line inside a string literal.\n";
                                return "";
                            }
                            s = m_inLine.c_str();
                            break;   // Continue parsing literal on the next line
                        }
                        else if (std::isspace(static_cast<unsigned char>(*s)))
                            s++;
                        else {
                            s = start;
                            ret += *s++;    // A regular escaped character. We don't check if it is one of the escapable ones, that's left for later.
                            break;      // There was some no-space character, so continue scanning
                        }
                    }
                    continue;       // After the backslash + spaces + endline _or_ backslash + spaces + non-space we have to continue checking for backslash.
                }
                else if (*s == '"')
                    break;          // Literal ended
                else if (*s == '\0') {
                    std::cerr << "Input line ends inside a string literal.\n";
                    return "";
                }
            }
            
            if (fx != 0 && *s == '{') {
                ret += *s++;      // Transfer the { to the resulting string
                if (*s != '{') {
                    // Find the end of the inserted expression. Basically scan for : or } but ignore as many colons as there are ? and
                    // skip through all parentheses, except in string literals.
                    std::string insert = processInsert(s, raw);
                    if (insert.empty())
                        return insert;

                    inserts.push_back(std::move(insert));
                    // If : check for nested inserts in the formatting arguments
                    if (*s == ':') {
                        ret += *s++;      // Transfer the : to the resulting string
                        while (*s != '}') {
                            if (*s == '{') {    // Nested insert starts
                                ret += *s++;      // Transfer the { to the resulting string
                                // Find the end of the inserted expression. Basically scan for : or } but ignore as many colons as there are ? and
                                // skip through all parentheses, except in string literals.
                                std::string insert = processInsert(s, raw);
                                if (insert.empty())
                                    return "";

                                inserts.push_back(std::move(insert));
                                if (*s != '}') {  // Colon not allowed inside nested insert expression.
                                    std::cerr << "Found nested insert expression ending in :. This is not allowed.\n";
                                    return "";
                                }
                            }

                            ret += *s++;      // Transfer the } or other formatting argument char to the resulting string
                        }
                    }
                    ret += *s++;   // Push the } to the output. Must be done separately as other right braces need to be doubled.
                }
                else
                    s++;        // Pass the second { in a double brace quote situation.
            }
            else if (fx != 0 && *s == '}') {
                ret += *s++;      // Transfer the first } to the resulting string
                if (*s++ != '}') {
                    std::cerr << "All right braces have to be doubled in f/x string literals.\n";
                    return "";
                }
            }
            else
                ret += *s++;
        }

        ret += *s++;      // Transfer the ending quote
        for (auto& insert : inserts)
            ret += ", " + insert;

        if (fx == 'f')
            ret += ")";

        return ret;
    }

    // Note: As an insert may span multiple input lines using \ or if raw we have to collect the entire insert expression in ret.
    std::string processInsert(const char*& s, bool raw) {
        std::string ret;            // Insert string to return
        std::vector<char> parens;   // Nested parens of different kinds
        int ternaries = 0;          // Number of ? operators that need : before we accept a format argument starting :

        auto lineEnds = [&]()->bool {
            assert(*s == '\0');

            ret += '\n';
            if (!getLine())
                return false;

            s = m_inLine.c_str();
            return true;
        };

        // Check if a \ is a line continuation and if so add the rest of the line to ret and position s at the start of next line.
        // If not append \ and at least one more character (more if the backslash was followed by whitespace and then _not_ an end
        // of  line).
        auto checkContinuation = [&]()->bool {
            assert(*s == '\\');
            ret += *s++;        // Output the backslash
            const char* start = s;
            while (true) {
                if (*s == '\0') {     // Continuation line detected, go get next line.
                    if (!lineEnds())
                        return false;

                    break;   // Continue parsing 
                }
                else if (std::isspace(static_cast<unsigned char>(*s)))
                    s++;
                else {
                    s = start;
                    break;      // There was some no-space character, the \ was something else to be handled later.
                }
            }

            return true;
        };

        while (parens.size() > 0 || ternaries > 0 || *s != '}') {
            if (parens.size() > 0 && *s == parens.back()) {     // Check for matching right parens
                ret += *s++;
                parens.pop_back();
                continue;
            }

            switch (*s) {
            case '\0':
                if (!raw) {
                    std::cerr << "End of line inside insert expression.\n";
                    return "";
                }

                if (!lineEnds()) {
                    std::cerr << "Input ends inside an insert expression in a raw literal.\n";
                    return "";
                }

                break;

            case '(':
                parens.push_back(')');
                ret += *s++;
                break;

            case '{':
                parens.push_back('}');
                ret += *s++;
                break;

            case '[':
                parens.push_back(']');
                ret += *s++;
                break;

            case '?':
                if (parens.empty())
                    ternaries++;

                ret += *s++;
                break;

            case ':':
                if (s[1] == ':')
                    ret += *s++; // Double colon is always a scoping operator
                else {
                    if (parens.empty() && ternaries == 0)
                        return ret;

                    if (parens.empty())
                        ternaries--;
                }
                
                ret += *s++;
                break;

            case '\\':          // Check for continuation line. If found add previous contents to ret.
                if (raw)        // No continuation lines in raw literals.
                    ret += *s++;
                else {
                    if (!checkContinuation()) {
                        std::cerr << "Input ends with a \\ last on a line inside an insert expression.\n";
                        return "";
                    }
                        
                    if (s > m_inLine.c_str())       // If we used exception this test would be unnecessary, the return value could indicate if it was a continuation.
                        ret += *s++;        // Pass the escaped character unless it was a proper line ending.
                }
                break;

            case '"': {
                std::string lit = processLiteral(ret, s);
                if (lit.empty())
                    return lit;
                ret += lit;
                break;
            }

            case '/':               // Check for C comment. // comments not allowed
                if (s[1] == '*') {
                    ret += *s++;
                    ret += *s++;
                    while (*s != '*' || s[1] != '/') {
                        if (raw) {      // Just restart when lines end and continue checking for */
                            if (*s == '\0') {
                                if (!lineEnds()) {
                                    std::cerr << "Input ends inside a comment in an insert expression in a raw literal.\n";
                                    return "";
                                }
                            }
                        }
                        else {
                            if (*s == '\\') {       // Possible continuation line inside comment
                                if (!checkContinuation()) {
                                    std::cerr << "Input ends after \\ inside a comment in an insert expression.\n";
                                    return "";
                                }
                            }
                            else if (*s == '\0') {
                                std::cerr << "Input ends inside a comment in an insert expression.\n";
                                return "";
                            }
                        }

                        ret += *s++;
                    }

                    ret += *s++;
                    ret += *s++;
                }
                break;      // No * after / or the entire comment is passed.

            default:
                ret += *s++;
                break;      // Nothing to do for other characters.
            }
        }

        return ret;
    }

    std::ostream& m_outFile;
    std::istream& m_inFile;
    std::string m_inLine, m_outLine; 
    int m_lineNo = 0;
};


struct {
    const char* in;
    const char* truth = nullptr;
    bool expectOk = true;
} tests[] = {
    // Test basic functionality
    { R"in()in" },
    { R"in(x = y)in" },
    { R"in(x = y
)in" },
    { R"in(#x = y
)in" },
    { R"in(#x = y"
)in" },                                    // preprocessor directive with mismatched " is ok.
{ R"in(#x = y\ 
" c"\n)in" },                              // preprocessor directive with mismatched " on continuation line is ok.
{ R"in(#x = y\ 
foo \
" c"\n)in" },                              // preprocessor directive with mismatched " on second continuation line is ok.
    { R"in(xx // foo)in" },                // C++ comment
    { R"in(xx // foo \ 
c ")in" },                                 // C++ comment with continuation line containing mismatched " is ok.
    { R"in(xx /* " */ yy)in" },            // C comment containing mismatched " is ok
    { R"in(xx /* ss
 " */ yy)in" },                            // #10 C comment containing mismatched " on line 2 is ok
    { R"in(xx /* ss)in", nullptr, false }, // C comment which doesn't end is not ok.
{ R"in(xx /* ss
 "/ yy *)in", nullptr, false },            // Multiline C comment which doesn't end is not ok.
    { R"in(#x = y \)in", nullptr, false }, // Ends when the directive continuation line is expected (Note: getline() finishes with an empty line if the last char is \n, so there is no trailing \n in the test cases)
    { R"in(xx //  \)in", nullptr, false }, // Ends after a continuation line in a // comment
    { R"in()in" },
    
     // Test non-raw literals.
    { R"in("")in" },
    { R"in("foo.bar")in" },
    { R"in("foo\"bar")in" },               // If the first " ends the literal we get an error at the line end...
    { R"in("foo\\bar")in" },
    { R"in("foo\
\"bar")in" },                              // Continuation line for non-raw literal
    { R"in(foo ")in", nullptr, false },
    { R"in(foo
")in", nullptr, false },
    { R"in("foo\ 
bar)in", nullptr, false },
{ R"in("foo\)in", nullptr, false },

    // Test raw literals.
    { R"in(R"()")in" },
    { R"in(R"xy()xy")in" },
    { R"in(R"xy(foo.bar)xy")in" },
    { R"in(R"xy(foo".bar)xy")in" },        // Quote in raw literal
    { R"in(R"xy(foo\"bar)xy")in" },        // Note: Here the \ is transferred verbatim to output, but the result is still the same as the input.
    { R"in(R"xy(foo\\bar)xy")in" },        // Here too.
    { R"in(R"xy(foo)"bar)yx"fum)xy")in" }, // Mismatched raw prefixed ends followed by real ending.
    { R"in(R"xy(foo
"bar)xy")in" },                            // Continuation line for non-raw literal
{ R"in(R"xy(foo
)xy")in" },                                // Continuation line for non-raw literal that stops in column 1 of the second line
    { R"in(R"abc)in", nullptr, false },    // Line ending in R literal prefix, if it is the last line
    { R"in(R"abc
d))in", nullptr, false },                  // Line ending in R literal prefix, if it is not the last line
    { R"in(foo R"xy()in", nullptr, false },// Unterminated raw literal on the last line
    { R"in(foo
R"(xy)z")in", nullptr, false },            // Unterminated raw literal on the second line
    { R"in(foo R"(xy)z")in", nullptr, false },          // Unterminated raw literal due to prefix mismatch none vs z
    { R"in(foo R"w(xy)z")")in", nullptr, false },       // Unterminated raw literal due to prefix mismatch w vs z
    { R"in(R"(foo 
bar)in", nullptr, false },                 // Unterminated raw literal spanning two lines, no prefix
    { R"in(R"xy(foo 
bar)in", nullptr, false },                 // Unterminated raw literal spanning two lines, xy prefix
    { R"in(R"xy(foo 
bar)yx")in", nullptr, false },             // Mismatched raw literal spanning two lines, xy prefix to yx suffix

    // Test insert extraction
    { R"in(f"The number is: {3 * 5}")in",                                  R"out(std::format("The number is: {}", 3 * 5))out" },
    { R"in(x"The numbers are: {a} and {b}")in",                            R"out("The numbers are: {} and {}", a, b)out" },
    { R"in(x"The numbers are: {a:x} and {b:5}")in",                        R"out("The numbers are: {:x} and {:5}", a, b)out" },
    { R"in(f"The number is: {a:{b}}")in",                                  R"out(std::format("The number is: {:{}}", a, b))out" },
    { R"in(f"The number is: {a:x{b}d}")in",                                R"out(std::format("The number is: {:x{}d}", a, b))out" },

    { R"in(f"The number is: {a ? b : c :4d}")in",                          R"out(std::format("The number is: {:4d}", a ? b : c ))out" },
    { R"in(f"The number is: {a ? b ? c : d : c :4d}")in",                  R"out(std::format("The number is: {:4d}", a ? b ? c : d : c ))out" },
    { R"in(f"The number is: {a ? b : c ? d : e :4d}")in",                  R"out(std::format("The number is: {:4d}", a ? b : c ? d : e ))out" },
    { R"in(f"The number is: {MyType{}}")in",                               R"out(std::format("The number is: {}", MyType{}))out" },

    { R"in(f"Just braces {{a}} {a}")in",                                   R"out(std::format("Just braces {a} {}", a))out" },
    { R"in(f"Use colon colon {std::rand()}")in",                           R"out(std::format("Use colon colon {}", std::rand()))out" },
    { R"in(f"Use colon colon {std::rand():fmt}")in",                       R"out(std::format("Use colon colon {:fmt}", std::rand()))out" },

    // Test C comments in insert expressions
    { R"in(f"The number is: {3 /* comment */ * 5}")in",                    R"out(std::format("The number is: {}", 3 /* comment */ * 5))out" },
    { R"in(f"The number is: {3 /* : ignored */ * 5:fmt}")in",              R"out(std::format("The number is: {:fmt}", 3 /* : ignored */ * 5))out" },
    { R"in(f"The number is: {3 /* } ignored */ * 5:f{m}t}")in",            R"out(std::format("The number is: {:f{}t}", 3 /* } ignored */ * 5, m))out" },
    { R"in(f"The number is: {3 /* comment \
continues */ * 5}")in",                                                    R"out(std::format("The number is: {}", 3 /* comment \
continues */ * 5))out" },

    { R"in(xR"(The numbers are: {a} and {b})")in",                         R"out(R"(The numbers are: {} and {})", a, b)out" },
    { R"in(xR"xy(The numbers are: {a} and {b})xy")in",                     R"out(R"xy(The numbers are: {} and {})xy", a, b)out" },
    { R"in(fR"(The number is: {3 /* comment
continues */ * 5})")in",                                                   R"out(std::format(R"(The number is: {})", 3 /* comment
continues */ * 5))out" },
    { R"in(fR"xy(The number is: {3 /* comment
xy) )" yx)" continues */ * 5})xy")in",                                     R"out(std::format(R"xy(The number is: {})xy", 3 /* comment
xy) )" yx)" continues */ * 5))out" },

    // Negative tests
    { R"in(f"Just braces {{} {a}")in", nullptr, false },                   // } have to be doubled when not ending an insert
    { R"in(f"The number is: {a:x{b:x}d}")in", nullptr, false },            // Colon in nested insert
    { R"in(f"The number is: {3
* 5}")in", nullptr, false },                                               // End of line inside insert expression
    { R"in(f"The number is: {3 * 5")in", nullptr, false },                 // Literal ends inside insert expression
    { R"in(fR"xy(The number is: {3 * 5)xy")in", nullptr, false },          // Literal ends inside insert expression in raw literal
    { R"in(f"The number is: {3 * 5: a")in", nullptr, false },              // Literal ends inside formatter args
    { R"in(fR"xy(The number is: {3 * 5: a)xy")in", nullptr, false },       // Literal ends inside formatter args in raw literal
    { R"in(f"The number is: {3 * 5:{3")in", nullptr, false },              // Literal ends inside nested insert
    { R"in(fR"xy(The number is: {3 * 5:{3)xy")in", nullptr, false },       // Literal ends inside nested insert in raw literal
    { R"in(f"The number is: {3 * 5 /*comment ")in", nullptr, false },      // Literal ends inside comment in an insert
    { R"in(fR"x(The number is: {3 * 5 /*comment )x")in", nullptr, false }, // Literal ends inside comment in an insert in a raw literal
    { R"in(f"The number is: {3 * 5 /*comment\)in", nullptr, false },       // Input ends inside comment in an insert

    // Test literals in insert expressions.
    { R"in(f"The number is: {std::strlen("He{ } j")}")in",                 R"out(std::format("The number is: {}", std::strlen("He{ } j")))out" },
    { R"in(f"The number is: {std::strlen(R"(Hej)")}")in",                  R"out(std::format("The number is: {}", std::strlen(R"(Hej)")))out" },
    { R"in(f"The number is: {std::strlen(R"xy(Hej
{{}})xy")}")in",                                                           R"out(std::format("The number is: {}", std::strlen(R"xy(Hej
{{}})xy")))out" },
    
    // f literal in f literal insert
    { R"in(f"The number is: {f"Five: {5}"} end")in",                           R"out(std::format("The number is: {} end", std::format("Five: {}", 5)))out" },
    { R"in(f"The number is: {f"Fi\
ve: {5}"}")in",                                                            R"out(std::format("The number is: {}", std::format("Fi\
ve: {}", 5)))out" },
    { R"in(f"The number is: {fR"xy(Five: {5})xy"}")in",                    R"out(std::format("The number is: {}", std::format(R"xy(Five: {})xy", 5)))out" },
    { R"in(f"The number is: {fR"xy(Fi
ve: {5})xy"}")in",                                                         R"out(std::format("The number is: {}", std::format(R"xy(Fi
ve: {})xy", 5)))out" },

};


bool runOneTest(const char* input, const char* truth, bool expectOk, int ix)
{
    std::istringstream in(input);
    std::ostringstream out;
    FxExtractor extractor(out, in);

    if (expectOk) {
        if (!extractor.process()) {
            std::cerr << "ERROR: The error string above was unexpected when processing input:\n" << input << " --- In test # " << ix << std::endl;
            return false;
        }
        if (out.str() != truth) {
            std::cerr << "ERROR: Extraction produced erroneous output:\n" << out.str() << "\nWhen expected output is:\n" << truth << std::endl;
            return false;
        }
    }
    else {
        if (extractor.process()) {
            std::cerr << "ERROR: The input below should have produced an error string.\n" << input << "\nExtraction however produced output:\n" << out.str() << std::endl;
            return false;
        }
    }

    return true;
}


int test()
{
    int ret = 0;
    int total = 0;
    for (auto& test : tests) {
        if (!runOneTest(test.in, test.truth != nullptr ? test.truth : test.in, test.expectOk, total))
            ret++;
        total++;
    }
   
    std::cerr << ret << " tests of " << total << " failed." << std::endl;
    return ret;
}

// Process stdin -> stdout, file -> stdout or file -> file.
int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "-h") == 0 || argc > 3) {
        std::cerr << "Usage: extractfx [<inFile> [<outFile>]].\nIf no parameters are given reads from stdin, if no outFile is given writes to stdout.\nIf --test is the only parameter does a self test.\n";
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--test") == 0) {
        std::cerr << "Performing self test\nNote: Negative testing will produce some printout here. Actual errors start with 'ERROR'\n";
        return test();
    }

    std::istream* is = &std::cin;
    std::ostream* os = &std::cout;

    if (argc > 1) {
        static std::ifstream inFile(argv[1]);
        if (!inFile) {
            std::cerr << "Could not open input file " << argv[1];
            return 1;
        }
        is = &inFile;

        if (argc > 2) {
            static std::ofstream outFile(argv[2]);
            if (!outFile) {
                std::cerr << "Could not open output file " << argv[2];
                return 1;
            }
            os = &outFile;
        }
    }

    FxExtractor extractor(*os, *is);
    return extractor.process() ? 0 : 1;
}

