---
title: Arrays
---
# {{title}}


## When and why do we need arrays?

Okay so it's pretty nice to be able to make lists of stuff:

```co
fun make_stuff(count uint) [Stuff]
  var stuffs [Stuff] = calloc(count, Stuff)
  for i = 0; i < stuffs.cap; i++
    stuffs[stuffs.len++].somefield = i
  stuffs
```

In this example, it's important to...
- allocate memory on the heap as `count` might be very large
- avoid copying the returned data; we should return a pointer
- make owership of heap memory clear. In this case `stuffs` is initially owned
  by the `make_stuff` body block and then when it returns, ownership is transferred
  to the caller.


### Fixed-size arrays

Fixed-size arrays is contiguous memory the size of multiple instances
of its elemental type. For example `[i32 3]` is 12 bytes of memory.

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

```bnf
FixedArrayType = "[" Type UIntLit "]"
```


### Variably-sized arrays

These are useful when defining a function that accepts a variable number of items

```co
fun sum(xs &[f64]) f64
  var sum f64
  for i = 0; i < xs.len; i++
    sum += xs[i]
  return i
```

Here the function only really needs two things:
1. a pointer to (possibly read-only) memory that contains f64 data
2. total number of f64 values at that pointer (length of array)

Co uses a "slice reference" type for this, `&[T]`.


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

For this Co has slicing which works on all kinds of arrays.
Slicing a fixed-size array does not copy it but yields a reference
with a pointer to the array memory, number of valid entries (length)
and the capacity of the underlying array.


An array reference `&[T]` or `mut&[T]`  is represented at runtime as
a structure with the following fields:

```co
struct const_slice_ref {
  ptr _ptrtype
  len uint // number of valid entries at ptr
}
struct mutable_slice_ref {
  ptr _ptrtype
  len uint // number of valid entries at ptr
  cap uint // number of entries that can be stored at ptr
}
```


The syntax for array references in Co is as follows:

```bnf
ArrayRef = "mut"? "&" ArrayType
```

In fact, references is a generic feature of Co and so its syntax is
more correctly described as:

```bnf
Ref = "mut"? "&" Type
```


### Dynamic "growing" arrays

Sometimes arrays need to grow by bounds only known at runtime.
For example we might parse a CSV file into an array of row structures:

```co
type CSVRow [&[u8]]
fun parse_csv(csvdata &[u8], nrows_guess uint)
  rows = calloc(nrows_guess, CSVRow) // [CSVRow] heap-allocated array
  for csvdata.len > 0
    row, csvdata = parse_next_row(csvdata)
    if row.isValid
      rows.append(row)
  log("parsed {rows.len} rows")
```

Co accomplishes this with dynamic, growable arrays allocated on the heap
using the heap allocator function `alloc<T type>(typ T, count uint) T`.

This also enables us to return large arrays as function results without
the overhead of copying an array to the caller:

```co
type CSVRow [&[u8]]
fun parse_csv(csvdata &[u8], nrows_guess uint) [CSVRow]
  rows = alloc(nrows_guess, CSVRow) // [CSVRow] heap-allocated array
  for csvdata.len > 0
    row, csvdata = parse_next_row(csvdata)
    if row.isValid
      rows.append(row)
  rows
```



### Resource ownership

Resource ownership rules in Co are simple:
- Storage locations own their data.
- Ownership is transferred only for heap arrays. All other values are copied.
- References are pointers to data owned by someone else.

When a storage location goes out of scope it relinquishes its ownership by bing "dropped".
When a value is dropped, any heap arrays are deallocated.
Any lingering references to a dropped value are invalid.
Accessing such a reference causes a "safe crash" by panicing in "safe" builds
and has undefined behavior in "fast" builds.

A "storage location" is a variable, struct field, tuple element,
array element or function parameter.

All data is passed by value in Co.
Note that references are memory addresses (an integer) and thus technically
copied when passed around.


---
[WIP]

Heap allocations are relatively expensive and so it should be
possible to make use of the stack even for arrays that might grow.
For example the Co compiler makes use of the following pattern:

- allocate a small but common number of items on the stack
- initialize an handle struct with a pointer to that memory and its capacity
- append items
  - when the capacity is reached, allocate more memory
    - if memory points to the stack:
      - allocate heap memory
      - copy existing data to it
    - else the memory points to the heap:
      - realloc
- if memory points to the heap: free

It may look something like this:

```c
// C
struct tmparray { StuffResult* p; int cap; int len; };
StuffResult* append(tmparray* a) {}
int process_stuff(Stuff stuff, StuffResult** resv) {
  struct { StuffResult* p; int cap; int len; } a;
  StuffResult a_st[3];
  a.p = a_st;
  a.cap = 3;
  while (stuff_next(&stuff)) {
    StuffResult* result = append(&a);
    if (!stuff_dequeue(&stuff, result))
      a.len--;
  }
}
```



### Arrays as function results [WIP]

Sometimes we need to produce an array as the result of a function.
If we return a pointer to the callees stack its value will likely be
over written and thus be undefined

```co
fun main()
  numbers = make_numbers()
  bar()
  // 'numbers' invalid here!

fun make_numbers() [int]
  var numbers [StuffResult 3]
  numbers[0] = 1
  numbers[1] = 2
  numbers[2] = 3
  numbers
```

So we need to make sure that only arrays allocated on the stack can escape a
function's body

```co
fun make_numbers() [int]
  numbers = calloc(3, int) // allocate 3 ints on the heap
  numbers[0] = 1
  // ...
  numbers
```



----------------
WORK IN PROGRESS


```co
fun process_stuff(stuff Stuff) [StuffResult]
  var results [StuffResult] = calloc(3, StuffResult)
  for work in stuff.next()
    if result = work()
      results.append(result)
  results
```



It might seem common to pass temporary arrays as arguments ...

```co
fun createFooConfig(things &[Thing]) FooConfig
fun main()
  config = createFooConfig([Thing(), Thing()])
```

... but in practice "rest" argument syntax covers most of these cases:

```co
fun createFooConfig(things ...&[Thing]) FooConfig
fun main()
  config = createFooConfig(Thing(), Thing())
```


## Notes

- lit `[1,2,3]` might be better as a slice than array?

- lits may be better as type constructors, ie `[int](1,2,3)` which would allow
  expressing an array type context-free, ie `[x]` is unambiguously an array type
  rather than "array literal in some places and array type in other places."


## Type model ideas

| Owning T | Borrowed T | Description
|----------|------------|-----------------------
| [int 3]  | &[int 3]   | Fixed Array
| [int]    | &[int]     | Slice of fixed Array
| [int \*] | &[int \*]  | Dynamic array


| Owning T | Borrowed T | Description
|----------|------------|-----------------------
| [int 3]  | &[int 3]   | Fixed Array or slice thereof
| [int]    | &[int]     | Dynamic array or slice thereof
