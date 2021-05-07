# scalar        int, float (address can be modeled on int)
# compound      array, slice, enum, struct
# function      identity = name + arg types)
# flow control  if...else, for, goto, switch

var names [4]str  # array of strings; cap=4, len=0
# C equiv: struct { str p[4]; uint len, refs; } names = {{},0,1};

var days = [byte]('M','T','W','T','F','S','S')  # slice of bytes; cap=7, len=7
# C equiv:
# struct { byte p[7]; uint len, refs; } days_a = {{'M','T','W','T','F','S','S'}, 7, 1};
# struct byteslice { byte* ptr; uint cap, len; } days = {days_a, 7, 7};

var name = "Sam"
# C equiv: struct { const char* p; uint len, refs; } name = {"Sam", 3, 1};

var nameBytes = []byte("Sam")
# C equiv:
# struct { byte p[7]; uint len, refs; } nameBytes_a = {{'S','a','m'}, 3, 1};
# struct byteslice { byte* ptr; uint cap, len; } nameBytes = {nameBytes_a, 3, 3};

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


# ocaml-like union enum type
type Account enum {
  DummyAccount
  UserAccount    User
  TwitterAccount str
  POSIXAccount   uid, gid int  # parameter names are merely comments
}
# C equivalent:
# struct Account {
#   int type; // 0 | 1 | 2 | 3
#   union {
#     User* user;    // when type == 1
#     char* str;     // when type == 2
#     int   pair[2]; // when type == 3
#   } u;
# };

fun name(a Account) str {
  switch a {
    DummyAccount           "Dummy"
    TwitterAccount(handle) handle
    POSIXAccount(uid, gid) "${uid}:${gid}"
  }
}

fun greet(name str, a Account) {
  var i = 3
  for i-- > 0 {
    print("Hello $name. Your account is ${a.name()}")
  }
}

greet("Sam", TwitterAccount("sam"))

# ----------------

type BinaryTree<T> enum {
  Leaf T
  Tree BinaryTree<T>, BinaryTree<T>
}
# C equivalent:
# struct BinaryTree {
#   int type; // 0 | 1
#   union {
#     T                  leaf;  // when type == 0
#     struct BinaryTree* LR[2]; // when type == 1
#   } u;
# };

