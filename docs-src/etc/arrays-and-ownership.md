---
title: Arrays
---
# {{title}}

Co has three constructs for dealing with arrays of values:

| Syntax  | Description
|---------|-----------------------------------
| `[T n]` | [Fixed-size arrays](fixed-size-arrays) for local and global data
| `[T]`   | [Dynamic arrays](#dynamic-arrays) that grow at runtime and can change owners. This is the foundation for—and the only way to—allocate heap memory.
| `&[T]`  | [Array references & slices](#array-references-slices) for referring to arrays

<!-- - `[T n] ` [Fixed-size arrays](fixed-size-arrays) for local and global data
- `[T]   ` [Dynamic arrays](#dynamic-arrays) that grow at runtime and can change owners
- `&[T]  ` [Array references & slices](#array-references-slices) for referring to arrays -->


#### A quick example

```co
fun make_stuff(count uint) [Stuff]
  stuffs = calloc(count, Stuff) // [Stuff]
  for i = 0; i < stuffs.cap; i++
    stuffs.append(Stuff(i))
  stuffs // transfers ownership to caller
```

In this example, it's important to...
- allocate memory on the heap as `count` might be very large
- avoid copying the returned data; we should return a pointer
- make owership of heap memory clear. In this case `stuffs` is initially owned
  by the `make_stuff` body block and then when it returns, ownership is transferred
  to the caller.


#### Language grammar

```bnf
ArrayType        = FixedArrayType | DynamicArrayType
FixedArrayType   = "[" Type ConstSizeExpr "]"
DynamicArrayType = "[" Type "]"
ArrayRefType     = "mut"? "&" ArrayType
ArrayLiteral     = "[" [ Expr (list_sep Expr)* list_sep? ] "]"
list_sep         = "," | ";"
```


## Fixed-size arrays

Fixed-size arrays is contiguous memory the size of multiple instances
of its elemental type. The size of a fixed-size array is part of its type and thus
a compile-time constant.
For example `[i32 3]` is 12 bytes of memory; three 32-bit integers.

Fixed-size arrays are useful as temporary storage for compile-time bounded loops
and for expressing uniform data like a vector.

```co
items = [1, 2, 3] // [int 3] in global memory

fun vec2(x, y f32) [f32 2]
  var p [f32 2]
  p[0], p[1] = x, y
  p // copied to caller

fun dot(p1 [f32 2], p2 [f32 2]) f32
  p1[0]*p2[0] + p1[1]*p2[1]

fun moving_avg(n int, f fun(i int)f64) f64
  var win [f64 10] // workset, 10× f64 allocated on stack
  var avg f64
  var sum f64
  for i = 0; i < n; i++
    val = f(i)
    sum = sum - win[i % win.len] + val
    win[i % win.len] = val
    avg = sum / f64(win.len)
  avg
```

Constant (immutable, read-only) fixed-size arrays are stored in global constant memory
and are in many cases elided at compile time, depending on use.
Mutable fixed-size arrays use local (e.g. stack) memory inside functions and global
writable memory when defined at the package level.

Assigning, returning or passing fixed-size arrays as function arguments creates
copies, just like with any other value in Co.
i.e. in `x = [1,2,3]; y = x`, y is a distinct copy of the array at `x`,
while `z` in `x = [1,2,3]; z = &x` is a reference (pointer) to the same array as `x`.

Fixed-size arrays in Co can be created both in function scope (local memory, e.g. stack)
and at the package level (global memory.)
They can be both constant and mutable.
The type of fixed-size arrays is written as `[T n]`,
for example `[int 3]` for "array of 3 ints"


## Array references & slices

Co features "references" as a way to share values without making copies.
References does not constitute ownership of data but is merely a borrowed handle.
References are like pointers in C with some additional compile-time semantics to
help you discern ownership.

References to arrays support slicing; the ability to share a smaller
range of an array.

```co
x = [1, 2, 3, 4, 5] // type [int 5]
            // RESULT              TYPE         VALUE
a = x       // copy of x           [int 5]      1,2,3,4,5
b = x[1:4]  // copy of slice of x  [int 3]      2,3,4
c = b[1:]   // copy of slice of x  [int 2]      3,4
d = &x      // reference to x      mut&[int 5]  1,2,3,4,5
e = &x[1:4] // ref to slice of x   mut&[int 3]  2,3,4
f = &e[1:]  // ref to slice of x   mut&[int 2]  3,4
```

Array references are useful when defining a function that accepts a variable
number of items which it only needs to read:

```co
fun sum(xs &[f64]) f64
  var sum f64
  for i = 0; i < xs.len; i++
    sum += xs[i]
  return i
```

The function in the above example receives a tuple of two values:
1. a pointer to memory that contains f64 data (array data)
2. count of valid values at the memory location (length of array)

Co uses a "slice reference" type for this, `&[T]`, which is a tuple
of pointer & length. More on this in a minute.

Variably-sized arrays are also useful locally, for example to drop
the first element under some condition only known at runtime:

```co
fun compute_stuff(nozero bool)
  values = [0, 10, 20] // [int 3] on stack
  xs = values[:]       // mut&[int] — pointer to 'values'
  if nozero
    xs = xs[1:] // drop first value
  for i = 0; i < xs.len; i++
    compute_one(xs[i])
```

Slicing works on all kinds of arrays.
Slicing a fixed-size array does not copy it but yields a reference
with a pointer to the array memory, number of valid entries (length)
and the capacity of the underlying array.


An array reference `&[T]` or `mut&[T]`  is represented at runtime as
a structure with the following fields:

```co
struct const_slice_ref {
  ptr memaddr // pointer to data
  len uint    // number of valid entries at ptr
}
struct mutable_slice_ref {
  ptr memaddr // pointer to data
  len uint    // number of valid entries at ptr
  cap uint    // number of entries that can be stored at ptr
}
```



## Dynamic arrays

Sometimes arrays need to grow by bounds only known at runtime.
Dynamic arrays has a length and capacity which can vary at runtime.
Dynamic arrays can grow and are allocated on the heap.
Dynamic array's data is not copied when passed around, instead its ownerhip transfers.

For example we might parse a CSV file into an array of row structures:

```co
type CSVRow [&[u8]]

fun parse_csv(csvdata &[u8], nrows_guess uint)
  rows = alloc(CSVRow, nrows_guess) // [CSVRow] heap-allocated array
  for csvdata.len > 0
    row, csvdata = parse_next_row(csvdata)
    if row.isValid
      rows.append(row)
  log("parsed {rows.len} rows")
  // 'rows' deallocated here as its storage goes out of scope
```

Co accomplishes this with dynamic, growable arrays allocated on the heap
using the heap allocator function `alloc<T type>(typ T, count uint) T`.

This also enables us to return large arrays as function results without
the overhead of copying an array to the caller:

```co
type CSVRow [&[u8]]

fun parse_csv(csvdata &[u8], nrows_guess uint) [CSVRow]
  rows = alloc(CSVRow, nrows_guess) // [CSVRow] heap-allocated array
  for csvdata.len > 0
    row, csvdata = parse_next_row(csvdata)
    if row.isValid
      rows.append(row)
  rows // ownership moves to caller

fun main
  rows = parse_csv(csvdata, 32)
  // 'rows' deallocated here as its storage goes out of scope
```



### Idea: Stack-storage optimization of dynamic arrays

Heap allocations are relatively expensive and so it should be
possible to make use of the stack even for arrays that grows.

The Co compiler, written in C, makes use of the following pattern:

- allocate a small but common number of items on the stack
- initialize a "handle struct" with a pointer to that memory and its capacity
- append items
  - when the capacity is reached, allocate more memory
    - if memory points to the stack:
      - allocate heap memory
      - copy existing data to it
    - else the memory points to the heap:
      - realloc
- if memory points to the heap: free

It looks something like this in C:

```c
// C
struct tmparray { Thing* p; void* initp; int cap; int len; };
void grow(tmparray* a) {
  a->cap *= 2;
  if (a->p == a->initp) {
    // move from stack to heap
    a->p = malloc(sizeof(Thing) * a->cap);
    memcpy(a->p, a->initp, sizeof(Thing) * a->len);
  } else {
    a->p = realloc(a->p, sizeof(Thing) * a->cap);
  }
}
void push(tmparray* a, Thing thing) {
  if (a->len == a->cap)
    grow(a);
  a->p[a->len++] = thing;
}
void build_a_thing(Thing* v, int c);
int process_stuff(Stuff stuff) {
  Thing a_st[3];
  struct tmparray a = { .p=a_st, .initp=a_st, .cap=3 };
  Thing thing;
  while (stuff_next(&thing)) {
    push(&a, thing);
  }
  // use array of values
  build_a_thing(a.p, a.len);
  if (a.p != a_st)
    free(a.p);
}
```

It would be nice if Co could somehow do this as an optimization for dynamic arrays.
Here's an example of what it could look like:

```co
fun process_stuff(stuff Stuff) [Thing]
  var a [Thing] // creates a "default" dynamic array
  // an implicit array of some small size is allocated on
  // the stack here and a is pointed to it.
  var thing Thing
  for stuff_next(&thing)
    a.append(thing) // may move a's data to heap
  build_a_thing(&a)
  // a dropped here; if a's data is on heap it is freed
```

The implementation struct of `[T]` could look like this:

```co
struct dynarray {
  ptr memaddr // pointer to data
  ish bool    // true if ptr is in the heap
  len uint    // number of valid entries at ptr
  cap uint    // number of entries that can be stored at ptr
}
```


------------------------------------------------------------------

## Notes & thoughts

- Should lit `[1,2,3]` yield a `mut&[int 3]` instead of `[int 3]`?
  May be more useful if it did..?

- Array lits may be better as type constructors, ie `[int](1,2,3)` which would allow
  expressing an array type context-free, i.e. `[x]` is unambiguously an array type
  rather than "array literal in some places and array type in other places."


### Rest parameters as syntactic sugar for fixed-size arrays

It nice to pass temporary arrays as arguments:

```co
fun create_config(somearg int, things &[Thing]) FooConfig
fun main()
  config = create_config(42, [Thing(), Thing()])
```

A (unimplemented) "rest" parameter syntax could be a nice syntactic sugar:

```co
fun create_config(somearg int, things ...&[Thing]) FooConfig
fun main()
  config = create_config(42, Thing(), Thing())
```

