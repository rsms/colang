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
  - [ ] Parsing array and slice reads e.g. `a[3]`, `a[:3]`
  - [ ] Anaysis (to consider: are array types inferred? Is there literal syntax?)
  - [ ] Codegen
- [ ] Consider using Co IR in between AST and LLVM IR.
      This would make it much easier to generate code with alternative backends like JS.
      Could keep it (much) simpler than Co1 IR, without regalloc or instruction selection.
- [ ] Rename basic primitive types like `int32` to LLVM/Rust/Zig style `i32`, `u16` etc.


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


## Misc

- [ ] Actual full build of packages _(need to expand the details of this here)_
- [ ] Packages/modules
  - [ ] Resolution and importing
  - [ ] What are the cached artifacts? Objects? Object archives? LLVM BC? LLVM IR? Nothing..?
- [ ] Comptime eval — a simple interpreter that can evaluate code at compile time.
      There's some very basic code for array size eval in `src/co/parse/eval.c`.
- [ ] Explore building WASM for web browsers (without LLVM)
  - [ ] Code generation without LLVM to WASM or JS (is there 3rd part stuff I can use?)


## Documentation

- [ ] Uhm... documentation?!
- [ ] Language
  - [ ] Syntax
  - [ ] Semantics
  - [ ] Examples
