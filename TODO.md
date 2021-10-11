# TODO

Wish list. Things that could be improved or added.


## Parsing, analysis and compilation

- [ ] String literals.
      [See co1](https://github.com/rsms/co/blob/master/src/scanner.ts#L894).
  - [ ] Scan & parse string literals
  - [ ] Scan & parse multiline string literals
- [ ] Scan & parse floating point literals.
      [See co1](https://github.com/rsms/co/blob/master/src/scanner.ts#L1205)
- [ ] Scan & parse character literals (what type is a char? u8? "rune"?).
      [See co1](https://github.com/rsms/co/blob/master/src/scanner.ts#L823)
- [ ] Arrays and slices
  - [x] Parsing array and slice types e.g. `[3][4]type`, `[]type`
  - [x] Parsing & analysis of array indexing e.g. `a[3]`
  - [x] IR gen of array indexing e.g. `a[3]`
  - [ ] Parsing & analysis of array slicing e.g. `a[:3]`
  - [ ] IR gen of array slicing e.g. `a[:3]`
- [x] Rename basic primitive types like `int32` to LLVM/Rust/Zig style `i32`, `u16` etc.
- [ ] Multi function dispatch e.g. `foo(int)` vs `foo(float32)`.


### IR

Use Co IR in between AST and LLVM IR.
This makes it easier to generate code with alternative backends like JS, makes the
LLVM backend simpler and removes language-specifics from most of the gnarly codegen stuff.

Keep it (much) simpler than Co1 IR, without regalloc or instruction selection.

- [x] Dedicated type system that does not rely on AST nodes (IRType)
- [ ] New LLVM backend that uses IR instead of AST as the source
- [ ] Interpreter for testing and comptime evaluation


## Testing & QA

- [ ] Parser test infra
  - [x] Scanner/tokenizer tests (`src/co/parse/scan_test.c`)
  - [ ] compile-time AST validator for development & debugging
    - [x] `@unres` unresolved hierarchy integrity
  - [x] AST repr that outputs easily-parsable S-expr
  - [ ] S-expr parser for parsing `#*!AST ...*#` comments in test sources
  - [ ] Diff the expected AST with the actual one. Complain on mismatch.
        Maybe just `text_diff(print(parse(actual)), print(parse(expected)))`.
        [See co1](https://github.com/rsms/co/blob/master/src/ast/test/ast_test.ts#L274)
        which may not be worth porting. Its ergonimics are not great.
- [ ] Codegen tests.
      I.e. build & run programs and verify their output, like Go examples.
- [ ] GitHub CI with builds
      See lobster for example: https://github.com/aardappel/lobster/blob/
      d8e2ce7f6ce2dd5b94e9bff92532c2e50c438582/.github/workflows/build.yml


## Misc

- [ ] Actual full build of packages _(need to expand the details of this here)_
- [ ] Packages/modules
  - [ ] Resolution and importing
  - [ ] What are the cached artifacts? Objects? Object archives? LLVM BC? LLVM IR? Nothing..?
- [ ] Comptime eval — a simple interpreter that can evaluate code at compile time.
      There's some very basic code for array size eval in `src/co/parse/eval.c`.
- [ ] Explore building WASM for web browsers (without LLVM)
  - [ ] Code generation without LLVM to WASM or JS (is there 3rd part stuff I can use?)
- [ ] Look into using [mimalloc](https://github.com/microsoft/mimalloc)
      for memory allocation


## Documentation

- [ ] Uhm... documentation?!
- [ ] Language
  - [ ] Syntax
  - [ ] Semantics
  - [ ] Examples
