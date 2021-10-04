- `T` — "owning value"
- `&T` — "ref to immutable value"
- `mut&T` — "ref to mutable value"

Examples with arrays:

- `[T N]` is "owning value of comptime-sized array"
- `[T]` is "owning value of runtime-sized array aka slice"
- `&[T N]` is "ref to immutable comptime-sized array"
- `&[T]` is "ref to immutable runtime-sized array aka slice"
- `mut&[T N]` is "ref to mutable comptime-sized array"
- `mut&[T]` is "ref to mutable runtime-sized array aka slice"

Example function signatures:

- `fun stash(xs [int])` — takes over ownership
- `fun stash(xs Foo)` — takes over ownership
- `fun join(xs &[int]) str` — borrows a read-only ref
- `fun join(xs &Foo) str` — borrows a read-only ref
- `fun reverse(xs mut&[int])` — borrows a mutable ref
- `fun reverse(xs mut&Foo)` — borrows a mutable ref
