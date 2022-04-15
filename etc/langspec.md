# Co language spec

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
as      continue   enum   import   switch
break   default    for    mut      type
case    defer      fun    return   var
const   else       if     struct

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

Literal = bool_lit | nil_lit | num_lit | str_lit | array_lit
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
  str_lit   = '"' (str_lit1 | str_litm) '"'
  str_lit1  = <TODO>
  str_litm  = <TODO>
  array_lit = "[" [ Expr (list_sep Expr)* list_sep? ] "]"

PrefixExpr = if | prefix_op | const_def | var_def | type_def | fun_def
           | tuple | group | block | unsafe
  if = "if" condExpr thenExpr [ "else" elseExpr ]
    condExpr = Expr
    thenExpr = Expr
    elseExpr = Expr
  prefix_op = prefix_operator Expr
    prefix_operator = "!" | "+" | "-" | "~" | "&" | "++" | "--"
  const_def = "const" Identifier Type? "=" Expr
  var_def   = "var" Identifier ( Type | Type? "=" Expr )
  type_def  = "type" Identifier Type
  fun_def   = "fun" Identifier? ( params Type? | params? )
    params = "(" [ (param list_sep)* paramt list_sep? ] ")"
      param  = Identifier Type?
      paramt = Identifier Type
  tuple  = "(" Expr ("," Expr)+ ","? ")"
  group  = "(" Expr ")"
  block  = "{" Expr (";" Expr)+ ";"? "}"
  unsafe = "unsafe" (fun_def | block)

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
