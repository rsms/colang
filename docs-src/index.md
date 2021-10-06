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
  B.engine.output # => 500
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
  B.engine.output; # => 500
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
Defining a variable is done in one of two ways:

```co
x = 1       // defines a new mutable variable x
x = 2       // assigns to x (since it is defined)
mut x = 3   // defines a new mutable variable x, shadows previous
const y = 4 // defines a new constant variable y
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
const a = 128    // type: ideal
v i8             // defines a variable of type i8
v = x            // x is interpreted as an i8 literal
v = a            // error: constant 128 overflows i8
```


### Automatic constants

Variables in Co are optimistically constant, meaning that if a variable is never
stored to, it is treated as defined immutable with the `const` keyword.

```co
fun main(v int) int
  const a [int 3] = [1, 2, 3]
  b [int 3] = [10, 20, 30]
  c [int 3] = [100, 200, 300]
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
  leal  202(%rdi), %eax
  retq
```




### Arrays

An array is a fixed-length sequence of data in one contiguous memory segment.
The length of an Array may be known at compile time.

Some examples:

```co
a [u8 10]                // array of 10 bytes, all 0
b [i64 3] = [10, 20, 30] // 3 64-bit integers
c = [i64(10), 20, 30]    // type inferred to [i64 3]
d = [10, 20, 30]         // type inferred to [int 3]
e = d                    // copy of d. Type [int 3]
f = &d                   // immutable reference to d
g = mut&d                // mutable reference to d
h = d[:2]                // copy of slice of d. Type [int 2]
i = &d[:2]               // immutable reference to slice of d
k = mut&d[1:]            // mutable reference to slice of d
k[1] = e[0]              // modify 2nd element of d
d.len                    // 3
k.len                    // 2

// still undecided: array types with runtime-varying length
// Alt A:
s1 [int] = d             // copy of d with length
s2 &[int] = d            // immutable ref to d with length
s3 mut&[int] = d         // mutable ref to d with length

// Alt B:
s1 [int] = d             // immutable ref to d with length
s2 mut[int] = d          // mutable ref to d with length
```



## Open Source

<https://github.com/rsms/co2>
