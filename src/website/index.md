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
    fuel u64 = 1000;
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

## Open Source

<https://github.com/rsms/co2>
