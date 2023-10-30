// f/x literal extractor for C++ preprocessor.
// By bengt.gustafsson@beamways.com
// MIT license.

#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <cassert>
#include <format>

class FxExtractor {
public:
    class EarlyEnd : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    class ParsingError : public std::runtime_error {
    public:
        ParsingError(int lineno, const char* msg) : std::runtime_error(std::format("Line {}: {}", lineno, msg)) {}
    };

    FxExtractor(std::ostream& outFile, std::istream& inFile, const std::string& fname) : m_outFile(outFile), m_inFile(inFile), fname(fname) {}

    bool process() {
        try {
            tryProcess();
            return true;
        }

        catch (const std::runtime_error& ex) {
            std::cerr << ex.what() << "\n";
        }

        return false;
    }

    void tryProcess() {
        std::string outLine;

        while (getLine(nullptr)) {
            // Scan for string literals, skipping preprocessor directives and comments. For now don't skip #ifdef'ed out lines
            const char* s = m_inLine.c_str();       // We use \0 to see the end. This way we can always access s[1] to see two character tokens such as // and /*
            while (*s != '\0') {
                switch (*s) {
                case '"':
                    outLine += processStringLiteral(outLine, s);
                    break;

                case '\'':
                    outLine += processCharLiteral(s);
                    break;

                case '/':               // Check for comments.
                    if (s[1] == '*') {
                        outLine += processCComment(s);  //  C comments don't need \ last on lines even if this expression is inside a non-raw literal.
                        break;
                    }
                    else if (s[1] == '/') {
                        outLine += processCPPComment(s);  //  C++ comments support \ last on lines regardless of if the enclosing literal is raw or not.
                        break;
                    }
                    [[fallthrough]];  

                default:
                    outLine += *s++;
                    break;      // Nothing to do for other characters.
                }
            }

            m_outFile << outLine;
            if (!m_inFile.eof())            // Preserve lack of last \n char in input.
                m_outFile << "\n";

            outLine.clear();
        }
    }

private:
    bool getLine(const char* err) {
        if (!m_inFile || m_inFile.eof()) {
            if (err != nullptr)
                throw EarlyEnd(err);
            else
                return false;
        }

        getline(m_inFile, m_inLine);
        m_lineNo++;
        return true;
    }

    bool checkContinuation(const char* p)
    {
        while (p > m_inLine.c_str() && std::isspace(static_cast<unsigned char>(*--p)))
            ;

        return *p != '\\';
    }

    std::string processCPPComment(const char*& s) {
        assert(*s == '/' && s[1] == '/');
        auto end = m_inLine.c_str() + m_inLine.size();
        std::string ret(s, end);                                 // The comment starts with the rest of the current line.
        while (true) {                                           // But can have continuation lines...
            if (checkContinuation(end))
                break;

            // Line ended with \ so the next line is also part of the comment. Rinse and repeat.
            getLine("Input ends with a line ending in \\.");

            ret += "\n" + m_inLine;
            end = m_inLine.c_str() + m_inLine.size();
        }

        s = end;
        return ret;
    }

    std::string processCComment(const char*& s) {
        assert(*s == '/' && s[1] == '*');
        std::string ret;
        
        const char* start = s;
        s += 2;
        while (*s == 0 || *s != '*' || s[1] != '/') {
            if (*s == '\0') {
                // Line ended in comment. output line and reload buffer.
                ret.append(start, s);
                ret += "\n";
                getLine("/* unmatched to the end of the input.");

                s = m_inLine.c_str();
                start = s;
            }
            else
                s++;
        }

        s += 2;
        ret.append(start, s);       // Append last part of C comment including trailing */
        return ret;
    }

    // Move char literal starting at s to out without touching any of its contents.
    std::string processCharLiteral(const char*& s) {
        return processLiteral(s, false, 0, '\'');
    }

    // Process a string literal including its prefix.
    std::string processStringLiteral(std::string& out, const char*& s) {
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

    // Process a char or string literal according to raw mode, f/x mode and terminator.
    std::string processLiteral(const char*& s, bool raw, char fx, char terminator = '"') {
        std::string ret;

        std::string prefix;
        assert(*s == terminator);
        s++;

        // Output the std::format( for f literals only
        if (fx == 'f')
            ret = fname + "(";

        // Handle start of literal
        if (raw) {      // Collect prefix
            auto begin = s;
            while (*s != '(') {
                if (*s == 0)
                    throw ParsingError(m_lineNo, " ends in a raw literal prefix. There must be a ( before the end of line after R\".");

                s++;
            }
            prefix = std::string(begin, s);
            s++;        // Pass (
            ret += "R\"" + prefix + "(";
        }
        else
            ret += terminator;

        // Process the actual literal contents.
        std::vector<std::string> fields;       // Can't be string_views as R literals span lines and we only store the last line.
        while (true) {
            if (raw) {      // Pass raw line ends and try to find prefix
                if (*s == 0) {
                    getLine("Input ends in raw literal.");

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
                    if (p - s == prefix.size() + 1 && *p == terminator) {
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
                            getLine("Input ends with a \\ last on a line inside a char or string literal.");

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
                else if (*s == terminator)
                    break;          // Literal ended
                else if (*s == '\0') 
                    throw ParsingError(m_lineNo, "Input line ends inside a char or string literal.");
            }
            
            if (fx != 0 && *s == '{') {
                s++;      // Transfer the { to the resulting string
                if (*s != '{') {
                    // Find the end of the inserted expression. Basically scan for : or } but ignore as many colons as there are ? and
                    // skip through all parentheses, except in string literals.
                    std::string field = processField(s, raw);

                    size_t pos = field.size();
                    while (pos > 0 && std::isspace(static_cast<unsigned char>(field[pos - 1])))
                        pos--;

                    if (field[pos - 1] == '=') {  // Debug style field where expression ends in =
                        ret += field;
                        field.erase(pos - 1);   // Remove = and trailing spaces from field.
                    }
                    ret += '{';

                    fields.push_back(std::move(field));
                    // If : check for nested fields in the format-spec
                    if (*s == ':') {
                        ret += *s++;      // Transfer the : to the resulting string
                        while (*s != '}') {
                            if (*s == 0) {
                                // If not raw the line must end by a \ as we're now in the part of the literal that's preserved to the output literal.
                                if (!raw) {
                                    if (!checkContinuation(s))
                                        throw ParsingError(m_lineNo, "Found unescaped end of line inside format-spec in non-raw literal");
                                }
                                getLine("Input ends inside format-spec");

                                s = m_inLine.c_str();
                            }
                            if (*s == '{') {    // Nested field starts
                                s++;    // Pass {

                                // Find the end of the expression-field. Basically scan for : or } but ignore as many colons as there are ? and
                                // skip through all parentheses, except in string literals.
                                std::string field = processField(s, raw);
 
                                ret += '{';      // Transfer a { to the resulting string

                                fields.push_back(std::move(field));
                                if (*s != '}')   // Colon not allowed inside nested expression-field.
                                    throw ParsingError(m_lineNo, "Found nested expression-field ending in :. This is not allowed.");
                            }

                            ret += *s++;      // Transfer the } or other formatting argument char to the resulting string
                        }
                    }
                    ret += *s++;   // Push the } to the output. Must be done separately as other right braces need to be doubled.
                }
                else {
                    ret += '{';
                    s++;        // Pass the second { in a double brace quote situation.
                }
            }
            else if (fx != 0 && *s == '}') {
                ret += *s++;      // Transfer the first } to the resulting string
                if (*s++ != '}')
                    throw ParsingError(m_lineNo, "All right braces have to be doubled in f/x string literals.");
            }
            else
                ret += *s++;
        }

        ret += *s++;      // Transfer the ending quote

        // Emit all extracted field expressions.
        for (auto& field : fields)
            ret += ", " + field;

        if (fx == 'f')
            ret += ")";

        return ret;
    }

    // Note: As an expression-field may span multiple input lines using \ or if raw we have to collect the entire expression-field in ret.
    std::string processField(const char*& s, bool raw) {
        std::string ret;            // Field string to return
        std::vector<char> parens;   // Nested parens of different kinds
        int ternaries = 0;          // Number of ? operators that need : before we accept a format argument starting :

        auto lineEnds = [&](const char* err) {
            assert(*s == '\0');

            ret += '\n';
            getLine(err);

            s = m_inLine.c_str();
        };

        // Check if a \ is a line continuation and if so add the rest of the line to ret and position s at the start of next line.
        // If not append \ and return false
        auto isContinuation = [&]() {
            assert(*s == '\\');
            ret += *s++;        // Output the backslash
            const char* p = s;
            while (true) {
                if (*p == '\0') {     // Continuation line detected, go get next line.
                    lineEnds("Input ends with a \\ last on a line inside an expression-field.");
                    return true;   // Continue parsing 
                }
                else if (std::isspace(static_cast<unsigned char>(*p)))
                    p++;
                else
                    return false;      // There was some non-space character, the \ was something else to be handled later.
            }
        };

        while (parens.size() > 0 || ternaries > 0 || *s != '}') {
            if (parens.size() > 0 && *s == parens.back()) {     // Check for matching right parens
                ret += *s++;
                parens.pop_back();
                continue;
            }

            switch (*s) {
            case '\0':
                lineEnds("Input ends inside an expression-field in a raw literal.");
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
                    if (!isContinuation())        // It was not a continuation line, s has not been reset.
                        ret += *s++;                // Pass the escaped character unless it was a proper line ending.
                }
                break;

            case '"': {
                ret += processStringLiteral(ret, s);
                break;
            }

            case '\'': {
                ret += processCharLiteral(s);
                break;
            }

            case '/':               // Check for comments.
                if (s[1] == '*') {
                    ret += processCComment(s);  //  C comments don't need \ last on lines even if this expression is inside a non-raw literal.
                    break;
                }
                else if (s[1] == '/') {
                    ret += processCPPComment(s) + "\n";;  //  C++ comments support \ last on lines regardless of if the enclosing literal is raw or not.
                    getLine("Input ends with a // comment inside a expression-field");

                    s = m_inLine.c_str();
                    break;
                }
                [[fallthrough]];  

            default:
                ret += *s++;
                break;      // Nothing to do for other characters.
            }
        }

        return ret;
    }

    std::ostream& m_outFile;
    std::istream& m_inFile;
    std::string fname;

    std::string m_inLine; 
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
    { R"in(#x = y\ 
" c"\n)in" },
    { R"in(#x = y\ 
foo \
" c"\n)in" },
    { R"in(xx // foo)in" },                // C++ comment
    { R"in(xx // foo \ 
c ")in" },                                 // C++ comment with continuation line containing mismatched " is ok.
    { R"in(xx /* " */ yy)in" },            // C comment containing mismatched " is ok
    { R"in(xx /* ss
 " */ yy)in" },                            // #10 C comment containing mismatched " on line 2 is ok
    { R"in(xx /* ss)in", nullptr, false }, // C comment which doesn't end is not ok.
    { R"in(xx /* ss
 "/ yy *)in", nullptr, false },            // Multiline C comment which doesn't end is not ok.
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

    // Test char literals
    { R"in('x')in" },
    { R"in('\'')in" },
    { R"in('\\')in" },
    { R"in('"')in" },
    { R"in('"and"')in" },     // Long char literals are acceptd, for some reason.
    { R"in('\u1234')in" },    // Test escapes
    { R"in('\x0A')in" },      // Test escapes
    { R"in('\
x')in", },                             // Test continuation line inside char literal
    { R"in('x)in", nullptr, false },   // Test error that file ends inside char literal
    { R"in('\)in", nullptr, false },   // Test error that file ends inside char literal after a continuation char.

    // Test field extraction
    { R"in(f"The number is: {3 * 5}")in",                                  R"out(std::format("The number is: {}", 3 * 5))out" },
    { R"in(x"The numbers are: {a} and {b}")in",                            R"out("The numbers are: {} and {}", a, b)out" },
    { R"in(x"The numbers are: {a:x} and {b:5}")in",                        R"out("The numbers are: {:x} and {:5}", a, b)out" },
    { R"in(f"The number is: {a:{b}}")in",                                  R"out(std::format("The number is: {:{}}", a, b))out" },
    { R"in(f"The number is: {a:x{b}d}")in",                                R"out(std::format("The number is: {:x{}d}", a, b))out" },

    // Test ternary operators on top level.
    { R"in(f"The number is: {a ? b : c :4d}")in",                          R"out(std::format("The number is: {:4d}", a ? b : c ))out" },
    { R"in(f"The number is: {a ? b ? c : d : c :4d}")in",                  R"out(std::format("The number is: {:4d}", a ? b ? c : d : c ))out" },
    { R"in(f"The number is: {a ? b : c ? d : e :4d}")in",                  R"out(std::format("The number is: {:4d}", a ? b : c ? d : e ))out" },
    { R"in(f"The number is: {MyType{}}")in",                               R"out(std::format("The number is: {}", MyType{}))out" },

    // Test escaping with double braces.
    { R"in(f"Just braces {{a}} {a}")in",                                   R"out(std::format("Just braces {a} {}", a))out" },
    { R"in(f"Use colon colon {std::rand()}")in",                           R"out(std::format("Use colon colon {}", std::rand()))out" },
    { R"in(f"Use colon colon {std::rand():fmt}")in",                       R"out(std::format("Use colon colon {:fmt}", std::rand()))out" },

    // Test expression-fields with line breaks
    { R"in(f"The number is: {3
* 5}")in",                                                                 R"out(std::format("The number is: {}", 3
* 5))out" },

    // Test expressions ending in } followed by } of the expression-field.
    { R"in(f"Construction {MyClass{1, 2}}")in",                            R"out(std::format("Construction {}", MyClass{1, 2}))out" },
    
    // Test C comments in expression-field
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

    // Test C++ comments in expression-fields in raw and non-raw x-literals.
    { R"in(f"The number is: {3 // comment
 * 5}")in",                                                                R"out(std::format("The number is: {}", 3 // comment
 * 5))out" },
    { R"in(fR"xy(The number is: {3 // comment
 * 5})xy")in",                                                             R"out(std::format(R"xy(The number is: {})xy", 3 // comment
 * 5))out" },

    // Negative tests
    { R"in(f"Just braces {{} {a}")in", nullptr, false },                   // } have to be doubled when not ending an expression-field
    { R"in(f"The number is: {a:x{b:x}d}")in", nullptr, false },            // Colon in nested expression-field
    { R"in(f"The number is: {3 * 5")in", nullptr, false },                 // Literal ends inside expression-field expression
    { R"in(fR"xy(The number is: {3 * 5)xy")in", nullptr, false },          // Literal ends inside expression-field expression in raw literal
    { R"in(f"The number is: {3 * 5: a")in", nullptr, false },              // Literal ends inside formatter args
    { R"in(fR"xy(The number is: {3 * 5: a)xy")in", nullptr, false },       // Literal ends inside formatter args in raw literal
    { R"in(f"The number is: {3 * 5:{3")in", nullptr, false },              // Literal ends inside nested expression-field
    { R"in(fR"xy(The number is: {3 * 5:{3)xy")in", nullptr, false },       // Literal ends inside nested expression-field in raw literal
    { R"in(f"The number is: {3 * 5 /*comment ")in", nullptr, false },      // Literal ends inside comment in an expression-field
    { R"in(fR"x(The number is: {3 * 5 /*comment )x")in", nullptr, false }, // Literal ends inside comment in an expression-field in a raw literal
    { R"in(f"The number is: {3 * 5 /*comment\)in", nullptr, false },       // Input ends inside comment in an expression-field

    // Test literals in expression-field expressions.
    { R"in(f"The number is: {std::strchr("He{ } j", '"')}")in",            R"out(std::format("The number is: {}", std::strchr("He{ } j", '"')))out" },
    { R"in(f"The number is: {std::strchr(R"(Hej)", '\'')}")in",            R"out(std::format("The number is: {}", std::strchr(R"(Hej)", '\'')))out" },
    { R"in(f"The number is: {std::strchr(R"xy(Hej
{{}})xy", '\x0a')}")in",                                                   R"out(std::format("The number is: {}", std::strchr(R"xy(Hej
{{}})xy", '\x0a')))out" },
    
    // f literal in f literal expression-field
    { R"in(f"The number is: {f"Five: {5}"} end")in",                       R"out(std::format("The number is: {} end", std::format("Five: {}", 5)))out" },
    { R"in(f"The number is: {f"Fi\
ve: {5}"}")in",                                                            R"out(std::format("The number is: {}", std::format("Fi\
ve: {}", 5)))out" },
    { R"in(f"The number is: {fR"xy(Five: {5})xy"}")in",                    R"out(std::format("The number is: {}", std::format(R"xy(Five: {})xy", 5)))out" },
    { R"in(f"The number is: {fR"xy(Fi
ve: {5})xy"}")in",                                                         R"out(std::format("The number is: {}", std::format(R"xy(Fi
ve: {})xy", 5)))out" },

    // Test trailing = for debug expressions:
    { R"in(f"{foo=}")in",                                                  R"out(std::format("foo={}", foo))out" },
    { R"in(f"{foo =}")in",                                                 R"out(std::format("foo ={}", foo ))out" },
    { R"in(f"{foo= }")in",                                                 R"out(std::format("foo= {}", foo))out" },
    { R"in(f"{foo = }")in",                                                R"out(std::format("foo = {}", foo ))out" },
};


bool runOneTest(const char* input, const char* truth, bool expectOk, int ix)
{
    std::istringstream in(input);
    std::ostringstream out;
    FxExtractor extractor(out, in, "std::format");

    if (expectOk) {
        if (!extractor.process()) {
            std::cerr << std::format("ERROR in test {}: The error string above was unexpected when processing input:\n{}\n", ix, input);
            return false;
        }
        if (out.str() != truth) {
            std::cerr << std::format("ERROR in test {}: Extraction produced erroneous output:\n{}\nWhen expected output is:\n{}\n", ix, out.str(), truth);
            return false;
        }
    }
    else {
        if (extractor.process()) {
            std::cerr << std::format("ERROR in test {}: The input below should have produced an error string.\n{}\nExtraction however produced output:\n{}\n", ix, input, out.str());
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
    if (argc == 2 && strcmp(argv[1], "-h") == 0 || argc > 5) {
        std::cerr << "Usage: extractfx [<inFile> [<outFile>]].\nIf no parameters are given reads from stdin, if no outFile is given writes to stdout.\nIf --test is the only parameter does a self test.\n";
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--test") == 0) {
        std::cerr << "Performing self test\nNote: Negative testing will produce some printout here. Actual errors start with 'ERROR'\n";
        return test();
    }

    std::istream* is = &std::cin;
    std::ostream* os = &std::cout;

    std::string name = "std::format";

    if (argc > 1) {
        int argn = 1;
        if (std::strncmp(argv[1], "--name", 6) == 0) {
            argn++;
            if (argv[1][6] == '=' || argv[1][6] == ':') {
                name = argv[1] + 7;
            }
            else if (argc < 3) {
                std::cout << "--name must be followed by a function name to surround f literals with. Default is std::format()\n.";
                return 1;
            }
            else {
                name = argv[2];
                argn++;
            }
        }

        if (argc > argn) {
            static std::ifstream inFile(argv[argn++]);
            if (!inFile) {
                std::cerr << "Could not open input file " << argv[1];
                return 1;
            }
            is = &inFile;
        }

        if (argc > argn) {
            static std::ofstream outFile(argv[argn]);
            if (!outFile) {
                std::cerr << "Could not open output file " << argv[2];
                return 1;
            }
            os = &outFile;
        }
    }

    FxExtractor extractor(*os, *is, name);
    return extractor.process() ? 0 : 1;
}

