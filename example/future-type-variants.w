# OCaml / ReasonML style types
type Account = Twitter(str)
             | Google(str, int)
             | Local
             | Test

fun signIn(a Account) {
  switch a {
    Twitter(handle)   -> print "Sign in to twitter as @$handle"
    Google(email, id) -> print "Sign in to Google with #$id $email"
    Local | Test      -> print "Use local computer user"
  }
}

# really just compiles to tuples. The above becomes:
fun signIn(a (int,str)|(int,str,int)|(int)) {
  switch a[0] {
    case 0:
      handle = a[1]
      print("Sign in to twitter as @$handle")
    case 1:
      email, id = a[1:]
      print("Sign in to Google with #$id $email")
    case 2: case 3:
      print("Use local computer user")
  }
}

# ReasonML syntax:
#
# type account = Twitter(string)
#              | Google(string, int)
#              | Local
#              | Test
#
# let a = Twitter("bobby99")
# let b = Google("bob@gmail.com", 123556)
#
# let signIn = switch (a) {
#   | Twitter(handle)   => "Sign in to twitter as @$handle"
#   | Google(email, id) => "Sign in to Google with #$id $email"
#   | Local | Test      => "Use local computer user"
# };
#
