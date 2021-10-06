# Type context

Type resolver
"requested types" version 2

The idea is that there's always an expected type context.
For example, a call to a function `(int bool)->int` has the type context `(int bool)` (a tuple),
while an assignment to a var e.g. `var x int = ` has the type context `int`.

Now, with basic types this is pretty straight-forward; when encountering an ideally-typed value
simply convert it to the basic type (if possible.) E.g. `x int = y` => `x int = y as int`.
However for complex types like tuples it's a little trickier. Consider the following:

    fun add(x int, y uint) -> x + y
    fun main {
      a, b = 2, 4
      # add's parameters => (int uint)
      # input arguments  => (ideal ideal)
      add(a, b)
    }

We need to map the input type (ideal ideal) to the context type (int uint).
This is essentially tree matching as the input type and context type may be arbitrarily
complex. For example:

    input:   (ideal (ideal {foo=ideal bar=ideal} ideal) ideal) =>
    context: (int (float64 {foo=int bar=int64} int) float32)

Same data as tree views:

    input:            =>    context:
      ideal           =>      int
      tuple:          =>      tuple:
        ideal         =>        float64
        struct:       =>        struct:
          foo=ideal   =>          foo=int
          bar=ideal   =>          bar=int64
        ideal         =>        int
      ideal           =>      float32

So what we do is to decompose complex type contexts as we descend AST nodes.
Taking our example from earlier with the call:

    context_type_stack.push( NTupleType(int uint) )   # <-- 1
    call resolve_type tuplenode
      call resolve_tuple_type tuplenode
        ct = context_type_stack.top()
        if ct is not NTupleType
          error "type mismatch: got tuple where ${ct} is expected"
        if ct.len != tuplenode.len
          error "type mismatch: ${tuplenode.len} items where ${ct.len} are expected"
        for (i = 0; i < tuplenode.len; i++)
          itemnode = tuplenode[i]
          ct2 = ct[i]
          context_type_stack.push(ct2)   # <-- 2
          call resolve_type itemnode
            call resolve_ideal_type itemnode
              ct = context_type_stack.top()
              if ct is not NBasicType
                error "type mismatch: got ideal type where ${ct} is expected"
              convert_ideal_type(itemnode, ct)
          context_type_stack.pop()       # <-- 3

    context_type_stack.pop( NTupleType(int uint) )   # <-- 4
