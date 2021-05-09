
type User struct {
  name str
  id   int
}
# C equiv: struct User { str name; int id; };

var user User      # C equiv: User user = {};
const user User    # C equiv: const User user = {}; // immovable; can only be borrowed
var user User?     # C equiv: User* user = 0;
var user MutableHandle(User)
# C equiv: User u = {}; struct { User* p; } user = {&u};

# Rust's Ownership Rules:
# - Each value in Rust has a variable that’s called its owner.
# - There can only be one owner at a time.
# - When the owner goes out of scope, the value will be dropped.
fun borrowUser(u &User) {
  print(u.name)  # reading is ok
  u.id = 0       # ERROR: u is borrowed; it's immutable
}
fun editUser(u User) User {
  u.id = 2  # OK: we own u
  return u  # move ownership to caller
}
fun takeUser(u User) {
  u.id = 0  # OK: we own u
  # u is deallocated here
}
fun makeUser() User {
  return User(name = "Sam", id = 0)  # move ownership to caller
}
fun example() {
  var u = makeUser()  # OK: u owns the User value
  u.name = "Robin"    # OK
  borrowUser(u)       # OK: borrow is always valid (except for dead vars)
  u.name = "Sam"      # OK
  {
    var u2 = &u       # OK: u2 borrows u
    print(u.name)     # OK: can read u but not write...
    u.name = "Robin"  # ERROR: u is immutable since it's currently borrowed (by u2)
    takeUser(u)       # ERROR: u can't move; it's currently borrowed
  } # u2 falls out of scope
  u.name = "Robin"    # OK: no borrowed refs of u; it's mutable again
  u = editUser(u)     # OK: move ownership of u and then take back ownership of u
  takeUser(u)         # OK: ownership of u moves to takeUser
  print(u.name)       # ERROR: u has moved to takeUser
}
