# Type Identity

> This document is out of date. `typeid` has been changed to encode types using ASCII characters
> and to express compound types with both opening and closing tokens.

A type's identity is effectively its shape. Examples:

    int == int != float
    (int,float) == (int,float) != (float,int)
    [int] == [int] != [float]
    {name str; age int} != {foo str; bar int}  // field names are part of the shape

Named types are just alises; its identity is their shape, not their name:

    type GameObject = { id int; velocity Vec2 }
    type SpaceShip = { id int; velocity Vec2 }
    fun draw(obj GameObject) { ... }
    draw(SpaceShip(id=1, velocity=(0.1, 0.0)))
    // this is ok SpaceShip == GameObject

Types are equal when their identity is the same.

If types are _compatible_ is a more complex matter. Say we change the example above to add
a color field to the SpaceShip struct. This makes GameObject and SpaceShip distinct types:

    type GameObject = { id int; velocity Vec2 }
    type SpaceShip = { id int; velocity Vec2; color Vec3 }
    fun draw(obj GameObject) { ... }
    draw(SpaceShip(id=1, velocity=(0.1, 0.0), color=RED))
    // this still works since SpaceShip >= GameObject

Compatibility depends on the specific type:

- scalar: never compatible. E.g. float and int are never compatible.
- list: compatible if their element type is. E.g. [SpaceShip] >= [GameObject]
- tuple: compatible by prefix. E.g. (float,int,bool) >= (float,int); (int,int) <= (int,int,int).
- struct: compatible by field names. E.g. {name:str;id:int;admin:bool} >= {id:int;name:str}

The identity of a struct is the union of its _sorted_ fields. I.e:

    {a:int; b:int; c:int} == {c:int; b:int; a:int} == {c:int; c:int; a:int}

Really, implementation for struct identity is similar to tuples:

    {a:int; b:int; c:int} -> (("a" int) ("b" int) ("c" int))

So, the identity of any type is a list:

    int                       => 0
    float                     => 1
    str                       => 2
    struct                    => 3
    list                      => 4
    bool                      => 5
    fun                       => 6
    funret                    => 7
    tuple                     => 8
    (int,float)               => 8, 0, 1
    (int,float,float,int)     => 8, 0, 1, 1, 0
    ((int,float),(float,int)) => 8, 8, 0, 1, 8, 1, 0
    [int]                     => 4, 0
    [(int,float)]             => 4, 8, 0, 1
    fun(int,[float])bool      => 6, 0, 4, 1, 7, 5


## Structs

Struct types too could effectively be identified by a list, but as we will see in a few lines,
this is not enough for structs, so structs are compared differently from other types.
First, let's see what happens if we just flatten struct to a list of IDs:
Since all identifiers (Syms) are interned, field names are identified by intptr.

    1. {name str; age int}                         => (struct,(Sym("age"),int),(Sym("name"),str))
    2. (struct,(Sym("age"),int),(Sym("name"),str)) => (struct,(0xDEAD,int),(0xBEEF,str))
    3. (struct,(0xDEAD,int),(0xBEEF,str))          => 3, 0xDEAD, 0, 0xBEEF, 2

This is nice and elegant but matching structs with varying fields is now inconvenient:

    {name str; age int}             => 3, 0xDEAD, 0, 0xBEEF, 2
    {name str; age int; admin bool} => 3, 0xAD41, 5, 0xDEAD, 0, 0xBEEF, 2

Oops! `{name str; age int} <= {name str; age int; admin bool}` is not true here since
`{... admin bool}` (`0xAD41, 5`) is sorted before age.
Performing a lookup or testing for compatibility between these two structs will be expensive
in practice as we'd need to scan.

Instead, records are identified the way as is described above, but are additionally book-keeped in
a separate data structure indexed in fields:

    // identity:
    typeid({name str; age int})             => 3, 0xDEAD, 0, 0xBEEF, 2
    typeid({name str; age int; admin bool}) => 3, 0xAD41, 5, 0xDEAD, 0, 0xBEEF, 2
    typeid({name str; age int}) => (3, 0xDEAD, 0, 0xBEEF, 2)  // identity
    // struct index mappings:
    (0xDEAD, 0) => (3, 0xDEAD, 0, 0xBEEF, 2), (3, 0xAD41, 5, 0xDEAD, 0, 0xBEEF, 2)
    (0xBEEF, 2) => (3, 0xDEAD, 0, 0xBEEF, 2), (3, 0xAD41, 5, 0xDEAD, 0, 0xBEEF, 2)
    (0xAD41, 5) => (3, 0xAD41, 5, 0xDEAD, 0, 0xBEEF, 2)

Wrapping this up in an example:

    type Account = { id int }
    type TwitterAccount = { handle str; id int }
    fun lookup(account Account) { dothing(account.id) }
    lookup(TwitterAccount { handle: "rsms", id: 4911 })

When we see the call to `lookup(Account)`, we do the following:

```py
ok = typeIsAssignable(param0, arg0)

def typeIsAssignable(L, R):  # L <= R
  if typeid(L) == typeid(R):
    return True
  if typeid(L)[0] == typeid(R)[0]:
    if typeid(L)[0] == structTypeID:
      for field in L:
        if typeid(R) not in structIndex.lookup(typeid(field)):
          # R does not have field
          return False
      return True # all fields found
    if typeid(L)[0] == listTypeID:
      return typeIsAssignable(L.listElementType, R.listElementType)
  return False
```

There are likely better, more efficient ways to match on arbitrary structural shapes.


### Challenge with efficient struct field access

Allowing structs to be "interfaces" means that we can't trivially optimize structs into arrays,
as is common. For example:

    type User = { name string; age int } => (int, string)
    // access is efficient; just by offset:
    doThing({ name: "Sam", age: 30 })
    fun printName(u User) {
      print(u.name)
    }

The implementation of `printName` can be compiled to this effective code:

    fun printName(u User) {
      // u.name  =>  a[1]  =>  ptr(a)+offsetof(a.name) =>
      tmp = LEA addr_of_u, 4  // 4 = byte offst of name field in struct.
      call print tmp
    }

However, this approach does not work when the structs have different fields; different shapes:

    type User    = { name string; age int }        => (int, string)
    type Account = {name str; age int; admin bool} => (bool, int, string)
    u = { name: "Sam", age: 30 }
    a = { name: "Robin", age: 24, admin: true }
    fun printName(u User) {
      print(u.name)  // offsetof(u.name) != offsetof(a.name)
    }

Any creative ideas for how to do this?

Go solves this by:

1. only allowing functions in interfaces, and
2. using a special call with vtable-like indirection on any interfaces.

Go's approach works but is inconvenient:

- Requires a distinction between struct and interface.
- Requires you to write function implementations for any field of a struct
  which needed through an interface.


#### Idea 1: Convert at call site

If the type of a parameter is identical to an argument, then no conversion is needed.
Otherwise create a temporary struct:

    if typeid(param0) == typeid(arg0):
      call printName a
    else:
      tmp = alloca sizeof(User)
      tmp[0] = a[1]  // remap field
      tmp[1] = a[2]  // remap field
      call printName tmp

In this approach we could further optimize this by tracking what fields a function needs access to:

    fun printName(u User) {
      // compiler meta data: (struct_access User (minField name) (maxField name))
      print(u.name)
    }

At the call sites we can generate just the code needed:

    if typeid(param0) == typeid(a):
      call printName a
    else:
      tmp = alloca sizeof( offsetof(User.maxField)
                         + offsetof(User.minField)
                         + sizeof(User.maxField) )
      tmp[0] = a[1]  // remap field
      call printName tmp



## Listing of identities

> Out of date

    id(nil)                    = 0
    id(bool)                   = 10
    id(int8)                   = 20
    id(int16)                  = 21
    id(int32)                  = 22
    id(int64)                  = 23
    id(uint8)                  = 24
    id(uint16)                 = 25
    id(uint32)                 = 26
    id(uint64)                 = 27
    id(float32)                = 30
    id(float64)                = 31
    id(tuple)                  = 40
    id(list)                   = 41
    id(struct)                 = 42
    id(fun)                    = 43
    id(funret)                 = 44
    id(str)                    = 45
    // compound types:
    id(int,float)              = id(tuple), id(int), id(float)
    id((int,float),(bool,int)) = id(tuple), id(tuple), id(int,float),
                                            id(tuple), id(bool,int)
    id([int])                  = id(list), id(int)
    id({name str; age int})    = id(struct) ("age", id(int))
                                            ("name", id(str))
    id(fun(int,float)->bool)   = id(fun)

