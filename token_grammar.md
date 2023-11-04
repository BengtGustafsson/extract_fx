


string-literal:
    encoding-prefix/opt " s-char-sequence/opt "
    encoding-prefix/opt R raw-string
    encoding-prefix/opt extraction-prefix " extraction-literal-contents "
    encoding-prefix/opt extraction-prefix R raw-extraction-string

# Do we want both cases?
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
    any member of the translation character set except \, ", {, }, \n

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
    e-token-sequence/opt
    simple-expression-field e-token-sequence
    simple-expression-field ( simple-expression-field )
    simple-expression-field [ simple-expression-field ]
    simple-expression-field { simple-expression-field }

e-token-sequence:
    e-token
    e-char-sequence e-token

e-token:
    identifier
    pp-number
    character-literal
    character-literal
    user-defined-character-literal
    string-literal
    user-defined-string-literal
    preprocessing-op-or-punc except (. [, {, ), ], }, ?, :.
    each non-whitespace character that cannot be one of the above except (. [, {, ), ], }, ?, :.

    
format-spec:
    f-char-sequence/opt
    format-spec f-char-sequence
    format-spec { expression-field }

f-char-sequence:
    f-char
    f-char-sequence f-char

f-char:
    any member of the translation character set except {, }.





Note: As :: is not the same as two colons, due to maximum munch rule, a format-spec that starts with a colon can't be expressed. As
format-spec fields can start with a space or a colon (both are used when container formatting proposal is accepted) the programmer
can't reliably "fix" this by adding a space, and the format-spec defintion can't be changed to skip initial space either.
