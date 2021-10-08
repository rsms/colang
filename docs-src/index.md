---
title: The Co programming language
---

# {{title}}

Co is a simple programming language developed as a hobby.
Co programs can be compiled to native executables and WebAssembly modules,
or run directly through its JIT compiler.


## Syntax

The language looks similar to Python and Go.
Its formal grammar uses semicolons to terminate expressions and curly braces
to delimit scope along with a set of rules for automatic semicolon insertion
and curly-braces "push and pop".

The following are equivalent:

```co
type Spaceship
  shields u8 = 10
  engine
    fuel   u64 = 1000
    output int = int(fuel / 2)

B = Spaceship()

fun main() int
  B.engine.output // => 500
```

```co
type Spaceship {
  shields u8 = 10;
  engine {
    fuel   u64 = 1000;
    output int = int(fuel / 2);
  }
}
B = Spaceship();
fun main() int {
  s = "thi\ng"
  B.engine.output; // => 500
}
```

Semicolons can be omitted using the following two rules:

1. When the input is broken into tokens, a semicolon is automatically inserted
   into the token stream immediately after a line's final token if that token is
    - an identifier
    - an integer, floating-point, char or string literal
    - one of the keywords `break`, `continue`, `fallthrough`, or `return`
    - one of the operators and delimiters `++`, `--`, `)`, `]`, or `}`

2. To allow complex statements to occupy a single line, a semicolon may be
   omitted before a closing `)` or `}`.

Curly braces can be omitted using the following two rules:

1. After a line break where an automatic semicolon would be inserted, if the
   indentation of the following line is _greater_ than the preceding line then
   a opening curly brace `{` is automatically inserted into the token stream.
   The automatic block is recorded on a stack.

2. After a line break where an automatic semicolon would be inserted, if the
   indentation of the following line is _less_ than the preceding line and the
   the preceding line is subject to rule 1, then
   a closing curly brace `}` is automatically inserted into the token stream
   for each recorded "automatic" block on a stack.



### Variables

Variables serve as both names for values and storage locations for mutable data.
Defining a variable is done in one of two ways: automatically if undefined when assigned
or explicitly with a keyword.

```co
x = 1       // defines a new variable x
x = 2       // assigns to x (since it is defined)
mut x = 3   // defines a new variable x, shadows previous
mut z int   // defines variable z with explicit type and 0 value
const y = 4 // defines constant y
y = 7       // error: cannot assign to constant y
```



### Ideally-typed literals

Literal numbers in Co are ideally typed, meaning an expession like `4` is not
of a particular type of number until it is used.

```co
4                // numeric literal of "ideal" type
const x = 4      // type: ideal
const y int = 4  // type: int
const z = y      // type: int
a = 128          // type: ideal (automatic constant)
mut v i8         // defines a variable of type i8
v = x            // x is interpreted as an i8 literal
v = a            // error: constant 128 overflows i8
```



### Automatic constants

Variables in Co are optimistically constant, meaning that if a variable is never
stored to, it is treated as defined immutable with the `const` keyword.

```co
fun main(v int) int
  const a [int 3] = [1, 2, 3]
  mut b [int 3] = [10, 20, 30]
  mut c [int 3] = [100, 200, 300]
  b[1] = v  // b promoted to mutable
  a[1] + b[1] + c[1]
```

In the above example `a` and `c` are constants; `c` is demoted to `const`
as it is never written to, while `b` is promoted to `mut` since we store to it.
The difference is mainly in the generated code: Co is able to generate less and more
efficient code this way. Only stack memory for `b` is allocated in the above example.
Access to `a` and `c` are resolved at compile time.

x86_64 code generated _without_ optimizations:

```asmx86
_main:
  ; initialize data for variable b on stack
  movl  $30, -4(%rsp)
  movl  $20, -8(%rsp)
  movl  $10, -12(%rsp)
  movl  %edi, -8(%rsp)    ; b[1] = v
  movl  l_a+4(%rip), %eax ; r1 = a[1]
  movl  -8(%rsp), %ecx    ; r2 = b[1]
  addl  l_c+4(%rip), %ecx ; r2 = c[1] + r2
  addl  %ecx, %eax        ; r1 = r2 + r1
  retq
l_a: ; constant data of variable a
  .long 1
  .long 2
  .long 3
l_c: ; constant data of variable c
  .long 100
  .long 200
  .long 300
```

x86_64 code generated _with_ optimizations:

```asmx86
_main:
  leal  202(%rdi), %eax ; add 202 to arg1
  retq
```




### Arrays

An array is a fixed-length sequence of data in one contiguous memory segment.
The length of an Array may be known at compile time.

Some examples:

```co
mut a [u8 10]                // array of 10 bytes, all 0
mut b [i64 3] = [10, 20, 30] // 3 64-bit integers
c = [i64(10), 20, 30]        // type inferred to [i64 3]
d = [10, 20, 30]             // type inferred to [int 3]
e = d                        // copy of d. Type [int 3]
const f = &d                 // immutable ref to d. Type &[int 3]
g = &d                       // mutable ref to d. Type mut&[int 3]
h = d[:2]                    // copy of slice of d. Type [int 2]
const i = &d[:2]             // immutable ref to slice of d
k = &d[1:]                   // mutable ref to slice of d
k[1] = e[0]                  // modify 2nd element of d
d.len                        // 3
k.len                        // 2

// still undecided: array types with runtime-varying length
// Alt A:
mut s1 [int] = d      // copy of d with length
mut s2 &[int] = d     // immutable ref to d with length
mut s3 mut&[int] = d  // mutable ref to d with length

// Alt B:
mut s1 [int] = d      // immutable ref to d with length
mut s2 mut[int] = d   // mutable ref to d with length
```




## Grammar

```bnf
// Unicode character classes
newline        = /* the Unicode code point U+000A */
unicode_char   = /* an arbitrary Unicode code point except newline */
unicode_letter = /* a Unicode code point classified as "Letter" */
unicode_digit  = /* a code point classified as "Number, decimal digit" */

// Letters and digits
letter        = unicode_letter | "_" | "$"
decimal_digit = "0" ... "9"
octal_digit   = "0" ... "7"
hex_digit     = "0" ... "9" | "A" ... "F" | "a" ... "f"

// Keywords
as      const     defer  for   import   struct
break   continue  else   fun   mut      switch
case    default   enum   if    return   type

// Operators, delimiters, and other special tokens
+    &     +=    &=     &&    ==    !=    (    )
-    |     -=    |=     ||    <     <=    [    ]
*    ^     *=    ^=     <-    >     >=    {    }
/    <<    /=    <<=    ->    =     :=    ,    ;
%    >>    %=    >>=    ++    !     ...   .    :
&^         &^=          --          ..

list_sep = "," | ";"

comment = line_comment | block_comment
  line_comment  = "//" /* anything except newline */ newline
  block_comment = "/*" /* anything except the terminator: */ "*/"

TranslationUnit = Statement ( ";" Statement )* ";"*
Statement       = Import | Expr
Import          = "import" str_lit

Expr = Identifier
     | Literal
     | TypeExpr
     | PrefixExpr
     | InfixExpr
     | SuffixExpr

TypeExpr   = NamedType | FunType
Identifier = letter (letter | unicode_digit | "-")*

Literal = bool_lit | nil_lit | num_lit | array_lit
  bool_lit = "true" | "false"
  nil_lit  = "nil"
  num_lit  = int_lit | float_lit
    int_lit = dec_lit | hex_lit | oct_lit | bin_lit
      dec_lit = decimal_digit+
      hex_lit = "0" ( "x" | "X" ) hex_digit+
      oct_lit = "0" ( "o" | "O")  octal_digit+
      bin_lit = "0" ( "b" | "B" ) ( "0" | "1" )+
    float_lit = decimals "." [ decimals ] [ exponent ]
              | decimals exponent
              | "." decimals [ exponent ]
      decimals = decimal_digit+
      exponent = ( "e" | "E" ) [ "+" | "-" ] decimals
  array_lit = "[" [ Expr (list_sep Expr)* list_sep? ] "]"

PrefixExpr = if | prefix_op | const_def | var_def | type_def | fun_def
           | tuple | group | block
  if = "if" condExpr thenExpr [ "else" elseExpr ]
    condExpr = Expr
    thenExpr = Expr
    elseExpr = Expr
  prefix_op = prefix_operator Expr
    prefix_operator = "!" | "+" | "-" | "~" | "&" | "++" | "--"
  const_def = "const" Identifier Type? "=" Expr
  var_def   = "mut" Identifier ( Type | Type? "=" Expr )
  type_def  = "type" Identifier Type
  fun_def   = "fun" Identifier? ( params Type? | params? )
    params = "(" [ (param list_sep)* paramt list_sep? ] ")"
      param  = Identifier Type?
      paramt = Identifier Type
  tuple = "(" Expr ("," Expr)+ ","? ")"
  group = "(" Expr ")"
  block = "{" Expr (";" Expr)+ ";"? "}"

InfixExpr = Expr ( binary_op | selector )
  binary_op = binary_operator Expr
  binary_operator = arith_op | bit_op | cmp_op | logic_op | assign_op
    arith_op  = "+"  | "-"  | "*" | "/" | "%"
    bit_op    = "<<" | ">>" | "&" | "|" | "~" | "^"
    cmp_op    = "==" | "!=" | "<" | "<=" | ">" | ">="
    logic_op  = "&&" | "||"
    assign_op = "="  | "+=" | "-=" | "*=" | "/=" | "%="
              | "<<=" | ">>=" | "&=" | "|=" | "~=" | "^="
  selector = Expr "." Identifier

SuffixExpr = Expr ( index | call | suffix_op | suffix_typecast )
  index           = "[" Expr "]"
  suffix_op       = "++" | "--"
  suffix_typecast = "as" Type
  call = Expr "(" args ")"
    args = positionalArgs* namedArgs* list_sep?
      positionalArgs = positionalArg (list_sep positionalArg)*
      namedArgs      = namedArg (list_sep namedArg)*
      namedArg       = Identifier "=" Expr
      positionalArg  = Expr

Type = NamedType
     | RefType
     | TupleType
     | ArrayType
     | StructType
     | FunType

NamedType = Identifier  // e.g. "int", "u32", "MyType"
RefType   = "mut"? "&" Type
TupleType = "(" Type ("," Type)+ ","? ")"
ArrayType = "[" Type size? "]"
  size = Expr?
StructType = "{" [ field ( ";" field )* ";"? ] "}"
  field = ( Identifier Type | NamedType ) ( "=" Expr )?
FunType = "fun" ( ftparams Type? | ftparams? )
  ftparams = params
           | "(" [ Type (list_sep Type)* list_sep? ] ")"

```


## Open Source

<https://github.com/rsms/co2>
