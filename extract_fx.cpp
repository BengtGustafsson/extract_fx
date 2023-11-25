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
        ParsingError(int lineno, std::string_view msg) : std::runtime_error(std::format("Line {}: {}", lineno, msg)) {}
    };

    FxExtractor(std::ostream& outFile, std::istream& inFile, const std::filesystem::path& sourceFile, const std::string& functionName, bool lineDirectives) : m_outFile(outFile), m_inFile(inFile), m_sourceFile(sourceFile), m_functionName(functionName), m_lineDirectives(lineDirectives) {}

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

        if (!m_inFile)
            throw std::runtime_error("Could not open file.");
        
        getNextLine();
        m_outFile << makeLineDirective(1, 0);

        bool saved = m_lineDirectives;      // Save m_lineDirectives to be able to restore it at end of line.

        while (peek() != '\0') {        // Process all lines
            // Scan for string literals, skipping comments. For now don't skip #ifdef'ed out lines
            bool continuation = false;      // No non-whitespace character since last backslash (i.e. potential continuation line)
            bool first = true;              // No non-whitespace character since line start (i.e. potential preprocessor directive)
            while (peek() != '\n' && peek() != '\0') {
                switch (peek()) {
                case '"':
                    processStringLiteral();
                    break;

                case '\'':
                    processCharLiteral();
                    break;

                case '/':               // Check for comments.
                    if (peek(1) == '*') {
                        processCComment();  //  C comments don't need \ last on lines even if this expression is inside a non-raw literal.
                        break;
                    }
                    else if (peek(1) == '/') {
                        processCPPComment();  //  C++ comments support \ last on lines regardless of if the enclosing literal is raw or not.
                        break;
                    }
                    [[fallthrough]];    // No comment start

                case '\\':
                    continuation = true;
                    xfer();
                    break;

                case '#':
                    if (first)
                        m_lineDirectives = false;       // This is a preprocessor directive. We can't do #line in them even if enabled.

                    xfer();
                    break;
                    
                default:
                    if (!isspace()) {
                        first = false; 
                        continuation = false;
                    }
                    
                    xfer();
                }
            }

            m_outFile << m_outLines;
            if (!m_inFile.eof())            // Preserve lack of last \n char in input.
                m_outFile << next();

            m_outLines.clear();
            if (!continuation)              // Don't restore if the preprocessor directive continues
                m_lineDirectives = saved;
        }
    }

private:
    // Descriptor of each expression field including where it starts to be able to add #line directive
    // ensuring that C++ error messages are appointed to the right position.
    struct Field {
        int line;
        int col;
        std::string expression;
    };

    bool getNextLine() {
        getline(m_inFile, m_inLine);
        m_lineNo++;
        if (!m_inFile.eof())
            m_inLine += '\n';

        m_ptr = m_inLine.c_str();
        return true;
    }

    char peek() { return *m_ptr; }
    char peek(size_t offset) { return m_ptr[offset]; }

    char next() {
        if (*m_ptr == '\n' || *m_ptr == '\0') {
            getNextLine();
            return '\n';
        }

        return *m_ptr++;
    }

    void xfer() {
        char c = next();
        m_outLines += c;
    }

    bool isspace() { return std::isspace(static_cast<unsigned char>(peek())); }

    std::string makeLineDirective(int line, int col) {
        if (!m_lineDirectives)
            return {};
        
        return std::format("\n#line {} \"{}\"\n{}", line, m_sourceFile.string(), std::string(col, ' '));
    }

    void processCPPComment() {
        assert(peek() == '/' && peek(1) == '/');
        xfer();
        xfer();

        bool backslash = false;     // Set when the last non-whitespace character is a backslash.
        while (peek() != '\n' || backslash) {            // Comment can have continuation lines...
            if (peek() == '\0') {
                if (backslash)
                    throw EarlyEnd("Input ends with a // comment line ending in \\.");
                else
                    return;     // Comment line last in file. This is ok.
            }
            else if (peek() == '\\')
                backslash = true;
            else if (peek() == '\n' || !isspace())
                backslash = false;

            xfer();
        }
    }

    void processCComment() {
        assert(peek() == '/' && peek(1) == '*');
        xfer();
        xfer();
        
        while (peek() != '*' || peek(1) != '/') {
            if (peek() == '\0')
                throw EarlyEnd("/* unmatched to the end of the input.");

            xfer();
        }

        xfer();
        xfer();
    }

    // Move char literal starting at s to out without touching any of its contents.
    void processCharLiteral() { processLiteral(false, '\0', "", '\''); }

    // Process a string literal including its prefix.
    void processStringLiteral() {
        bool raw = false;
        size_t pos = m_outLines.size();
        if (pos > 0 && m_outLines[pos - 1] == 'R') {
            raw = true;
            pos--;
        }

        char fx = 0;
        std::string encoding;
        if (pos > 0) {
            char c = std::tolower(static_cast<unsigned char>(m_outLines[pos - 1]));
            if (c == 'f' || c == 'x') {
                fx = c;
                pos--;
            }

            // For f literals) we must be able to move the encoding prefix inside the std::format(
            // call.
            if (fx == 'f' && pos > 0) {
                switch (m_outLines[pos - 1]) {
                case 'L':
                case 'U':
                case 'u':
                    encoding += m_outLines[pos - 1];
                    pos--;
                    break;

                case '8':
                    if (pos > 1 && m_outLines[pos - 2] == 'u') {
                        encoding ="u8";
                        pos -= 2;
                    }
                };
            }
        }

        m_outLines.erase(pos);
        processLiteral(raw, fx, encoding, '"');
    }

    // Process a char or string literal according to raw mode, f/x mode and terminator, prepending encoding where appropriate.
    void processLiteral(bool raw, char fx, const std::string& encoding, char terminator) {
        std::string prefix;

        assert(peek() == terminator);

        std::string lit;

        auto toLit = [&] {
            lit += next();
        };

        // Handle start of literal
        if (raw) {
            lit += 'R';
            toLit();
            
            // Collect prefix
            while (peek() != '(') {
                if (peek() == '\0' || peek() == '\n')
                    throw ParsingError(m_lineNo, " ends in a raw literal prefix. There must be a ( before the end of line after R\".");

                prefix += next();
            }
            lit += prefix;
            toLit();
        }
        else
            toLit();         // add quote

        // Process the actual literal contents.
        std::vector<Field> fields;       // Can't be string_views as R literals span lines and we only store the last line.
        bool backslash = false;         // Only used in non-raw case
        while (true) {
            if (raw) {      // Pass raw line ends and try to find prefix
                if (peek() == 0)
                    throw EarlyEnd("Input ends in raw literal.");

                if (peek() == ')') {       // Look for prefix after ) and " after that.
                    size_t pix = 0;
                    while (pix < prefix.size()) {
                        if (peek(pix + 1) != prefix[pix])   // Note: as prefix can't contain \n or \0 we can always pre-see characters until comparison fails or prefix is complete.
                            break;
                        
                        pix++;
                    }
                    if (pix == prefix.size() && peek(pix + 1) == terminator) {
                        // Raw literal ended
                        toLit();  // The )
                        lit += prefix;
                        m_ptr += prefix.size();
                        break;
                    }
                }
            }
            else {      // Handle continuation lines and escaped quotes in non-raw literals
                if (peek() == '\\')
                    backslash = !backslash;
                else if (peek() == terminator) {
                    if (backslash)
                        backslash = false;
                    else
                        break;          // Literal ended
                }
                else if (peek() == '\0')
                    throw EarlyEnd("Input ends inside a char or string literal.");
                else if (!backslash && peek() == '\n')
                    throw ParsingError(m_lineNo, "Input line ends inside a char or string literal.");
                else if (peek() == '\n' || !isspace())
                    backslash = false;
            }
            
            if (fx != '\0') {
                if (peek() == '{') {
                    next();
                    if (peek() != '{')
                        processExtractionField(lit, fields);
                    else
                        lit += '{';   // The first { transferred for double left braces. The second is transferred as the "normal" literal character.
                }
                else if (peek() == '}') {
                    toLit();      // Transfer the first } to the resulting string
                    if (peek() != '}')
                        throw ParsingError(m_lineNo, "Right brace characters must be doubled in f/x string literals.");
                }
            }

            toLit();      // A regular literal character
        }

        toLit();      // Transfer the ending quote

        if (fx != '\0') {
            // Output the std::format( for f literals only
            if (fx == 'f') {
                if (m_functionName.back() == '*')
                    m_outLines += m_functionName.substr(0, m_functionName.size() - 1) + '<' + std::to_string(fields.size()) + '>';
                else
                    m_outLines += m_functionName;

                m_outLines += "(" + encoding;
            }

            m_outLines += lit;
            // Emit all extracted field expressions.
            for (auto& field : fields) {
                m_outLines += makeLineDirective(field.line, field.col - 2);   // Subtract 2 for the , we add before the field below.
                m_outLines += ", " + field.expression;
            }

            if (fx == 'f')
                m_outLines += ")";
        }
        else
            m_outLines += lit;
    }

    // Parse an extraction field and add its contained expression(s) to fields. Return the remaining literal part such as {} or {:xxx}
    void processExtractionField(std::string& lit, std::vector<Field>& fields) {
        Field field = processExpressionField();
        // Check for expression-field ending in = +optional spaces.
        size_t pos = field.expression.size();
        while (pos > 0 && std::isspace(static_cast<unsigned char>(field.expression[pos - 1])))
            pos--;

        if (field.expression[pos - 1] == '=') {  // Debug style field where expression ends in =
            lit += field.expression;
            field.expression.erase(pos - 1);   // Remove = and trailing spaces from field.
        }

        lit += '{';     // After automatically generated label, if any.

        auto toLit = [&] {
            lit += next();
        };

        fields.push_back(std::move(field));
        // If : check for nested fields in the format-spec
        if (peek() == ':') {
            toLit();      // Transfer the : to the resulting string
            while (peek() != '}') {
                if (peek() == '\0')
                    EarlyEnd("Input ends inside format-spec");

                if (peek() == '{') {    // Nested field starts
                    toLit();

                    // Find the end of the expression-field. Basically scan for : or } but ignore as many colons as there are ? and
                    // skip through all parentheses, except in string literals.
                    Field field = processExpressionField();
                    if (peek() != '}')   // Colon not allowed inside nested expression-field.
                        throw ParsingError(m_lineNo, "Found nested expression-field ending in :. This is not allowed.");

                    fields.push_back(std::move(field));
                }

                toLit();      // Transfer other formatting argument char to the resulting string
            }
        }
    }
    
    // Parse a top level part of an expression field until a colon or right brace is encountered. Ignore double colons, recurse on
    // ?, (, [, {.
    // Pre: just after {
    // Post: peek() == '}' or ':'
    Field processExpressionField() {
        Field ret;            // Field string to return
        ret.line = m_lineNo;
        ret.col = int(m_ptr - m_inLine.c_str());

        std::string save = std::move(m_outLines);
        m_outLines.clear();

        // Another level required to handle ?: ternaries without redoing the save operation.
        processExpression();

        ret.expression = std::move(m_outLines);
        m_outLines = std::move(save);
        return ret;
    }

    void processExpression()
    {
        while (true) {
            processNested();

            switch (peek()) {
            case ')':
                throw ParsingError(m_lineNo, "Extraneous ) in expression-field");

            case ']':
                throw ParsingError(m_lineNo, "Extraneous ] in expression-field");
                break;

            case '?':
                xfer();           // TODO: Use m_outLines anyway: We must keep a stack of ongoing outlines by saving it in parseLiteral or somewhere on the way. This is as parseLiteral is called recursively.
                processExpression();
                if (peek() != ':')
                    throw ParsingError(m_lineNo, "Mismatched ? in expression-field");

                xfer();      // Pass :
                processExpression();
                return;

            case '}':
                return;
                
            case ':':
                if (peek(1) != ':')
                    return;

                if (!std::isalpha(static_cast<unsigned char>(peek(2))))
                    return;

                xfer();
                xfer();
                break;
                
            default:
                xfer();
                break;      // Nothing to do for other characters.
            }
        }
    }

    // Pass over any number of nested comments, literals and matched parentheses, returning the text passed over, which may span lines.
    // Note: This helper must work on an existing 'ret' as processStringLiteral does so to be able to check for prefixes in the
    // already passed text. Fortunately prefixes are not allowed to span lines.
    void processNested()
    {
        while (true) {
            switch (peek()) {
            case '\0':
                throw EarlyEnd("Input ends inside an expression-field in a literal.");

            case '(':
            case '[':
            case '{':
                processNestedParenthesis();
                break;

            case '"':
                processStringLiteral();
                break;

            case '\'':
                processCharLiteral();
                break;

            case '/':               // Check for comments.
                if (peek(1) == '*') {
                    processCComment();  //  C comments don't need \ last on lines even if this expression is inside a non-raw literal.
                    break;
                }
                else if (peek(1) == '/') {
                    processCPPComment();  //  C++ comments support \ last on lines regardless of if the enclosing literal is raw or not.
                    if (peek() == '\0')
                        throw EarlyEnd("Input ends with a // comment inside a expression-field");
                }
                break;

            default:
                return;
            }
        }
    }

    void processNestedParenthesis() {
        static const std::string introducers = "([{";
        static const std::string terminators = ")]}";

        size_t intIx = introducers.find(peek());
        assert(intIx != std::string::npos);     // Must be found.

        xfer();
        while (true) {
            processNested();
            char c = peek();
            xfer();
            if (c == terminators[intIx])
                return;
            if (terminators.find(c) != std::string::npos)
                throw ParsingError(m_lineNo, std::format("Mismatched {}. A {} was found where a {} was expected.", introducers[intIx], c, terminators[intIx]));
        }
    }

    std::filesystem::path m_sourceFile;    // Path to file being compiled
    std::string m_functionName;            // Name of function to wrap f-literals in.
    bool m_lineDirectives;                 // True to output line directives. Set false in most unit tests.

    std::istream& m_inFile;         // Input stream
    std::string m_inLine;           // Current line in input stream
    int m_lineNo = 0;               // Line number (starts on 1)
    const char* m_ptr;              // Pointer to current character.

    std::ostream& m_outFile;        // Output stream.
    std::string m_outLines;         // Current output lines. Except inside multi-line literals or comments this is always just one line
};


struct TestSpec {
    const char* input;
    const char* truth = nullptr;
    bool expectOk = true;
    bool lineDirectives = false;
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
    { R"in(f"Just braces {{a}} {a}")in",                                   R"out(std::format("Just braces {{a}} {}", a))out" },
    { R"in(f"Use colon colon {std::rand()}")in",                           R"out(std::format("Use colon colon {}", std::rand()))out" },
    { R"in(f"Use colon colon {std::rand():fmt}")in",                       R"out(std::format("Use colon colon {:fmt}", std::rand()))out" },

    // Test expression-fields with line breaks
    { R"in(f"The number is: {3
* 5}")in",                                                                 R"out(std::format("The number is: {}", 3
* 5))out" },

    // Test expressions ending in } followed by } of the expression-field.
    { R"in(f"Construction {MyClass{1, 2}}")in",                            R"out(std::format("Construction {}", MyClass{1, 2}))out" },
    
    // Test nested parentheses in expression fields
    { R"in(f"Construction {a * (b + c)}")in",                              R"out(std::format("Construction {}", a * (b + c)))out" },
    { R"in(f"Construction {a * (b + p[3])}")in",                           R"out(std::format("Construction {}", a * (b + p[3])))out" },

    // Negative tests with mismathced parentheses
    { R"in(f"Construction {a * (b + c}")in", nullptr, false },
    { R"in(f"Construction {a * (b + c]}")in", nullptr, false },
    { R"in(f"Construction {a * [b + c}}")in", nullptr, false },

    // Test C comments in expression-field
    { R"in(f"The number is: {3 /* comment */ * 5}")in",                    R"out(std::format("The number is: {}", 3 /* comment */ * 5))out" },
    { R"in(f"The number is: {3 /* : ignored */ * 5:fmt}")in",              R"out(std::format("The number is: {:fmt}", 3 /* : ignored */ * 5))out" },
    { R"in(f"The number is: {3 /* } ignored */ * 5:f{m}t}")in",            R"out(std::format("The number is: {:f{}t}", 3 /* } ignored */ * 5, m))out" },
    { R"in(f"The number is: {3 /* comment \
continues */ * 5}")in",                                                    R"out(std::format("The number is: {}", 3 /* comment \
continues */ * 5))out" },
   { R"in(f"The number is: {3 /* comment
continues */ * 5}")in",                                                    R"out(std::format("The number is: {}", 3 /* comment
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
    { R"in(f"The number is: {3 // comment \
foo
 * 5}")in",                                                                R"out(std::format("The number is: {}", 3 // comment \
foo
 * 5))out" },
    { R"in(fR"xy(The number is: {3 // comment
 * 5})xy")in",                                                             R"out(std::format(R"xy(The number is: {})xy", 3 // comment
 * 5))out" },
    { R"in(fR"xy(The number is: {3 // comment \
fum
 * 5})xy")in",                                                             R"out(std::format(R"xy(The number is: {})xy", 3 // comment \
fum
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
    { R"in(f"The number is: {3 // comment * 5}")in",  nullptr, false },    // Input ends with C++ comment, * 5} and " not seen as they are part of the comment
    { R"in(f"The number is: {3 // comment \
 * 5}")in",  nullptr, false },                                             // Input ends with C++ comment continuing on next line, * 5} and " not seen as they are part of the comment

    // Test nested literals
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

    // Test encoding prefix handling for f-strings (only).
    { R"in(Lf"The number is: {3 * 5}")in",                                  R"out(std::format(L"The number is: {}", 3 * 5))out" },
    { R"in(Uf"The number is: {3 * 5}")in",                                  R"out(std::format(U"The number is: {}", 3 * 5))out" },
    { R"in(uf"The number is: {3 * 5}")in",                                  R"out(std::format(u"The number is: {}", 3 * 5))out" },
    { R"in(u8f"The number is: {3 * 5}")in",                                 R"out(std::format(u8"The number is: {}", 3 * 5))out" },
    { R"in(Wf"The number is: {3 * 5}")in",                                  R"out(Wstd::format("The number is: {}", 3 * 5))out" },
    { R"in(u9f"The number is: {3 * 5}")in",                                 R"out(u9std::format("The number is: {}", 3 * 5))out" },
    { R"in(LfR"xy(The number is: {3 * 5})xy")in",                           R"out(std::format(LR"xy(The number is: {})xy", 3 * 5))out" },

    // Test colon fill character
    { R"in(Lf"The number is: {3 * 5::<5}")in",                                  R"out(std::format(L"The number is: {::<5}", 3 * 5))out" },

    // Longer example from readme
    { R"in(std::cout << f"The number of large values is: {
    std::count_if(myContainer.begin(), myContainer.end(), [&](auto& elem) {
         return elem.value > largeVal;  // The value member is compared.
    })
}, where the limit is {largeVal}";)in",                                    R"out(std::cout << std::format("The number of large values is: {}, where the limit is {}", 
    std::count_if(myContainer.begin(), myContainer.end(), [&](auto& elem) {
         return elem.value > largeVal;  // The value member is compared.
    })
, largeVal);)out" },

    // Test line directive. 15 chars before the { so 16 spaces first on line following the line directive.
    { R"in(Lf"The number is: {3 * 5}")in",                                  R"out(
#line 1 "test"
std::format(L"The number is: {}"
#line 1 "test"
                 , 3 * 5))out", true, true },
};


bool runOneTest(const TestSpec& test, int ix)
{
    std::istringstream in(test.input);
    std::ostringstream out;
    FxExtractor extractor(out, in, "test", "std::format", test.lineDirectives);

    if (test.expectOk) {
        if (!extractor.process()) {
            std::cerr << std::format("ERROR in test {}: The error string above was unexpected when processing input:\n{}\n", ix, test.input);
            return false;
        }
        std::string truth = test.truth != nullptr ? test.truth : test.input;
        if (out.str() != truth) {
            std::cerr << std::format("ERROR in test {}: Extraction produced erroneous output:\n{}\nWhen expected output is:\n{}\nFor input:\n{}\n", ix, out.str(), truth, test.input);
            return false;
        }
    }
    else {
        if (extractor.process()) {
            std::cerr << std::format("ERROR in test {}: The input below should have produced an error string.\n{}\nExtraction however produced output:\n{}\n", ix, test.input, out.str());
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
        if (!runOneTest(test, total))
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

    std::string functionName = "std::format";

    std::filesystem::path inputPath  = "<stdin>";

    if (argc > 1) {
        int argn = 1;
        if (std::strncmp(argv[1], "--name", 6) == 0) {
            argn++;
            if (argv[1][6] == '=' || argv[1][6] == ':') {
                functionName = argv[1] + 7;
            }
            else if (argc < 3) {
                std::cout << "--name must be followed by a function name to surround f literals with. Default is std::format()\n.";
                return 1;
            }
            else {
                functionName = argv[2];
                argn++;
            }
        }

        if (argc > argn) {
            inputPath = argv[argn++];
            static std::ifstream inFile(inputPath);
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

    FxExtractor extractor(*os, *is, inputPath, functionName, true);
    return extractor.process() ? 0 : 1;
}

