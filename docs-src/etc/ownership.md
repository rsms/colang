---
title: Resource ownership
---
# {{title}}

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


See also

- ["Ownership You Can Count On: A Hybrid Approach to Safe Explicit Memory Management"](https://researcher.watson.ibm.com/researcher/files/us-bacon/Dingle07Ownership.pdf)
- ["Friendship ended with the garbage collector" (inko)](https://yorickpeterse.com/articles/friendship-ended-with-the-garbage-collector/)
- [Vale's Hybrid-Generational Memory](https://vale.dev/blog/hybrid-generational-memory)
