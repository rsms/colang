#
# lambdas are really just functions with a few syntactical differences:
#
#   1. lambdas are always rvalues to an lvalue with a known function type.
#
#   2. lambdas do not start with "fun" because of point 1.
#
#   3. declaring argument types is optional for lambdas.
#
#
# Example; first we define a function that takes a function as an argument:

fun map(a [float64], f fun (float64,int) -> float64) {
  a2 = [float64]
  for v in a {
    a2.push(v)
  }
  a2
}

# Next we call that function, using function syntax for the f argument:

map([1.0, 2.0, 3.0], fun (value float64, _ int) { value * 10 }) # => [10.0, 20.0, 30.0]

# Using single-expression body syntax:

map([1.0, 2.0, 3.0], fun (value float64, ) int) -> value * 10)

# Using lambda syntax: (note the lack of argument types)

map([1.0, 2.0, 3.0], (value, _) -> value * 10)

# Lambdas can even drop any parameters at the end which it does not need:

map([1.0, 2.0, 3.0], (value) -> value * 10)

# Further, lambdas don't need parenthesis around a single argument:

map([1.0, 2.0, 3.0], value -> value * 10)

# Let's introduce a convention:
#
#   If the last parameter of a function is a function, then a lambda expression passed
#   as the corresponding argument can be placed outside the parentheses:
#
# With this in mind, we can further simplify the example:

map([1.0, 2.0, 3.0]) value -> value * 10

# We can write it with a block instead if we want:
map([1.0, 2.0, 3.0]) value { value * 10 }

# For lambdas without arguments, the arguments portion can be omitted:
on("event") { log("event occured") }
on("event") -> log("event occured")
on("event") log("event occured")


# Grammar:
#
# FunDecl = "fun" Ident params Type
# FunDef  = "fun" Ident? params? Type? ( Block | "->" Expr )
# Lambda  = ( params | Ident )? Expr
#
# LambdaApplication           = positionalLambdaApplication | trailingLambdaApplication
# positionalLambdaApplication = Call
# trailingLambdaApplication   = Call Lambda
#
# args    = "(" Expr ("," Expr)* ","? ")"
# params  = "(" param ("," param)* ","? ")"
# param   = Ident Type? | Type
#
# Examples:
#
#   fun foo (x, y int) int             Function declaration
#   fun foo (x, y int) int { x * y }   Function definition
#   fun foo (x, y int) { x * y }       Function definition with implicit result type
#   fun foo { 5 }                      Function definition without arguments
#   fun foo -> 5                       Function definition with single-expression body
#
#   fun (x, y int) int { x * y }       Anonymous function
#   fun (x, y int) { x * y }           Anonymous function with implicit result type
#   fun { 5 }                          Anonymous function without arguments
#   fun -> 5                           Anonymous function with single-expression body
#
#   f((x, y) -> x * y)                 Lambda application as argument
#   f((x, y) -> { x * y })             Lambda application with block body
#   f(x -> x * 2)                      Lambda application with single argument
#   f(x * 2)                           Lambda application with no arguments
#
#   f() (x, y) -> x * y                Trailing lambda application
#   f() (x, y) -> { x * y }            Trailing lambda application with block body
#   f() x -> x * 2                     Trailing lambda application with single argument
#   f() x * 2                          Trailing lambda application with no arguments
#
################################################################################################
#
# fun f(f (int)->()->int)
# f( x -> x -> x * 2 )
#
# Call:
#   Receiver: f
#   Args:
#     Lambda:
#       Args: ( x )
#       Body:
#         Call:
#           Receiver:
#             Lambda:
#               Args: ( x )
#               Body:
#                 Op * ( x 2 )
#
################################################################################################
#
# Challenge: Ambiguous syntax which requires N-token look-ahead to parse.
#
# First, let's consider the easier one to solve, requiring only a 1-token look-ahead.
# The parser already operates with one-token look-ahead, so this is trivial to implement:
#
#   f(x -> x * 2)  # 1. x is an argument
#   f(x * 2)       # 2. x is part of an expression
#
#   f(x
#     ^
#     Argument identifier or expression?
#
#   Must parse one more token and:
#   if token+1 == "->" then:
#     Treat x as the single argument of a lambda
#   else:
#     Treat x as start of the lambda body expression
#
# Second, let's consider the harder pattern to parse which requires theoretically infinite
# look-ahead of tokens:
#
#   f((x, y) -> (x, y))  # 1. arguments, tuple expression as body
#   f((x, y))            # 2. no arguments, tuple expression as body
#
#   f((
#     ^
#     Argument group start or expression? ("(..." can be group or tuple)
#
#   Here we can't simply look ahead for a "->" since it might be nested, for example:
#     f((x, y -> z))
#
#   Instead, when we expect a lambda, we must parse ahead on two "tracks":
#
#
#    f((x,y)->...)
#                         1      3      4      5
#                         x      ,y     )      ->
#    ------------------------------------------------------------------
#     Parse --> Fork    Track A: "the arguments"       |
#                                                      |  Pick track A
#     f((                 (x   (x,y  (x,y)  (x,y) ->   |
#    ------------ \ ---------------------------------------------------
#                  \    Track B: "the body expression"
#                   \
#                    \    (x   (x,y  (x,y)  ERROR: expected ")" or ";"
#                     \--------------------------------
#
#
#    f((x,y){...})
#                         1      3      4      5
#                         x      ,y     )      {
#    ------------------------------------------------------------------
#     Parse --> Fork    Track A: "the arguments"       |
#                                                      |  Pick track A
#     f((                 (x   (x,y  (x,y)  (x,y) {    |
#    ------------ \ ---------------------------------------------------
#                  \    Track B: "the body expression"
#                   \
#                    \    (x   (x,y  (x,y)  ERROR: expected ")" or ";"
#                     \--------------------------------
#
#
#    f((x,y))
#                         1      3      4      5
#                         x      ,y     )      )
#    --------------------------------------------------
#     Parse --> Fork    Track A: "the arguments"
#
#     f((                 (x   (x,y  (x,y)  ERROR: expected "->" or "{"
#    ------------ \ ---------------------------------------------------
#                  \    Track B: "the body expression" |
#                   \                                  |  Pick track B
#                    \    (x   (x,y  (x,y)  (x,y))     |
#                     \------------------------------------------------
#
#
# Note: Another tricky one is "StructType{foo:123}" for struct initialization.
#
#   f(Foo{bar:1})  # 1. zero-argument lambda with body "Foo{bar:1}"
#   f(Foo{bar})    # 2. single-argument lambda with body "bar"
#