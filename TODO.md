# TODO

Wish list. Things that could be improved or added.


## Parsing

- [ ] string scanning.
      [See co1](https://github.com/rsms/co/blob/master/src/scanner.ts#L894).
- [ ] floating point literal number scanning.
      [See co1](https://github.com/rsms/co/blob/master/src/scanner.ts#L1205)
- [ ] character literal scanning.
      [See co1](https://github.com/rsms/co/blob/master/src/scanner.ts#L823)


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
- [ ]


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
