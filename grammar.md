The grammar of string-literal described in [lex.string](https://eel.is/c++draft/lex.string) would be extended by new for extraction
literals. Unfortunately new char categories x-char, y-char, e-char and f-char had to be added to work in the style of the current
preprocessor grammar, where each clause is mutually exclusive within a production. The only difference between these char categories
(as well as other char categories) is which character set they start from and which small set of characters they exclude to ensure
that clauses that use those characters after a char sequence are not ambiguous.

The only part of the grammar below that doesn't adhere to these principles is the double brace quoting clauses which are formally
ambiguous with the corresponding single brace followed by extraction-field clauses. This is not easily solvable so I left it like
this with the understanding that if there are two braces without intervening space it is a braced quote. The specification of a
format string for std::format [here](https://en.cppreference.com/w/cpp/utility/format/format) is not on this level, instead the
quoting mechanism is described verbally. If we allow that the grammar below could be reduced, and even more so if nested parentheses
could be handled in a similar way.

Here is the complete grammar as implemented by extract_fx. Note that expression-field is used from both raw and non-raw
extraction-literals: As there is no escaping of quotes in non-raw literals there is no difference in syntax between
extraction-fields in regular and raw ltierals.

```
string-literal:
    encoding-prefix/opt " s-char-sequence/opt "
    encoding-prefix/opt R raw-string
    encoding-prefix/opt extraction-prefix " extraction-literal-contents "
    encoding-prefix/opt extraction-prefix R raw-extraction-string

# Do we want both upper and lower case? If not maybe choose upper as both L and R are upper and u and u8 are not usable with f anyway.
extraction-prefix:
    f
    x
    F
    X

extraction-literal-contents:
    x-char-sequence/opt
    extraction-literal-contents x-char-sequence
    extraction-literal-contents { extraction-field }
    extraction-literal-contents {{
    extraction-literal-contents }}

x-char-sequence:
    x-char
    x-char-sequence x-char

x-char:
    basic-x-char
    escape-sequence
    universal-character-name
    
basic-x-char
    any member of the translation character set except \, ", {, }, \n.

raw-extraction-string:
    " d-char-sequence/opt ( raw-extration-literal-contents } d-char-sequence/opt "

raw-extraction-literal-contents:
    y-char-sequence/opt
    raw-extraction-literal-contents y-char-sequence
    raw-extraction-literal-contents { extraction-field }
    raw-extraction-literal-contents {{
    raw-extraction-literal-contents }}

y-char-sequence:
    y-char
    y-char-sequence y-char

y-char:
    any member of the translation character set except {, } and ) followed by the initial d-char-sequence followed by "
    
extraction-field:
    expression-field
    expression-field : format-spec

expression-field:
    simple-expression-field
    simple-expression-field ? expression-field : expression-field

simple-expression-field:
    e-char-sequence/opt
    simple-expression-field e-char-sequence
    simple-expression-field ( simple-expression-field )
    simple-expression-field [ simple-expression-field ]
    simple-expression-field { simple-expression-field }
    simple-expression-field string-literal
    simple-expression-field char-literal
    simple-expression-field ::

e-char-sequence:
    e-char
    e-char-sequence x-char

e-char:
    any member of the translation character set except (. [, {, ", ', ), ], }, ?, :. Comments are replaced with single spaces as outside literals.
    
format-spec:
    f-char-sequence/opt
    format-spec f-char-sequence
    format-spec { expression-field }

f-char-sequence:
    f-char
    f-char-sequence f-char

f-char:
    any member of the translation character set except {, }.
```

