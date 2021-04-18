#pragma once
ASSUME_NONNULL_BEGIN

// #define HAMT_MAX_TREE_DEPTH 7
// typedef bool (*HamtEq)(void* a, void* b);

typedef struct Hamt Hamt;

// // Hamt is an immutable collection
// Hamt* HamtNew();

// HamtWith returns an evolution of h with entry
// const Hamt* HamtWith(const Hamt*, void* entry);

/* Return a new collection based on "o", but without "key". */
// PyHamtObject * _PyHamt_Without(PyHamtObject *o, PyObject *key);

/* Find "key" in the "o" collection.

   Return:
   - -1: An error occurred.
   - 0: "key" wasn't found in "o".
   - 1: "key" is in "o"; "*val" is set to its value (a borrowed ref).
*/
// int _PyHamt_Find(PyHamtObject *o, PyObject *key, PyObject **val);


ASSUME_NONNULL_END
