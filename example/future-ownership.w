# Exploring a variant of the Rust model
#
# - Each value in Rust has a variable that’s called its owner.
# - There can only be one owner at a time.
# - When the owner goes out of scope, the value will be dropped.
#
# https://doc.rust-lang.org/book/ch04-01-what-is-ownership.html

type User {
  id     int
  name   str
  emails [str]
}

fun print(u &User)  # borrows u
fun addEmail(u User, email &str) User  # takes ownership of u, borrows email, returns u
fun store(u User)  # takes ownership of u

fun example1 {
  u = User(id=0, name="sam")  # heap-alloc + assign pointer
  u = addEmail(u, "sam@hawtmail.com")
  print(u)  # print borrows u
  store(u)  # u moves to store(); local u is invalid
  print(u)  # error! u has moved to store
}

fun example2 {
  u = User(id=0, name="sam")
  {
    t = timer(fun {
      # u in here is an immutable borrowed reference
      print(u)  # ok; print just reads
      store(u)  # error! can't move reference u to store
    })
    # u is immutable here as a reference has been borrowed
    print(u)  # ok; print just reads
    store(u)  # error! can't move borrowed u to store
  }
  # t is gone thus nothing borrows u anymore; u is mutable and can be moved
  store(u)  # ok; u moved to store
  print(u)  # error! u has moved to store
}
