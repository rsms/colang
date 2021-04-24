//
// Left-leaning red-black tree implementation.
// Based on the paper "Left-leaning Red-Black Trees" by Robert Sedgewick.
//
// This file is intended not to be compiled directly, but to be included by
// another C file. All functions are "static" and thus has hidden visibility.
//
// The following lists marcos and fucntions which are expected to be defined and
// implemented by the file that includes this file:

#ifndef RBKEY
  #error "please define RBKEY to your key type (e.g. const char*)"
#endif
// Note: define RBVALUE to have a "value" field added to nodes and functions
// supporting values (i.e. map/dictionary.) Activates RBGet in addition to RBHas.
#ifdef RBVALUE
  #ifndef RBVALUE_NOT_FOUND
    // Value returned when key lookup fails
    #define RBVALUE_NOT_FOUND ((RBVALUE)NULL)
  #endif
#endif

#ifndef RBKEY_NULL
  #define RBKEY_NULL NULL
#endif

// If RBUSERDATA is defined, then a last parameter is added to most functions
// for threading through a value of this type.
#ifdef RBUSERDATA
  #define RB_COMMA ,
  #define RBUSERDATA_NAME        userdata
  #define RBUSERDATA_NAME_COMMA  RB_COMMA userdata
  #define RBUSERDATA_            RBUSERDATA RBUSERDATA_NAME
  #define RBUSERDATA_COMMA       RB_COMMA RBUSERDATA RBUSERDATA_NAME
#else
  #define RBUSERDATA_NAME
  #define RBUSERDATA_NAME_COMMA
  #define RBUSERDATA_
  #define RBUSERDATA_COMMA
#endif

// The node type
typedef struct RBNode {
  RBKEY          key;
#ifdef RBVALUE
  RBVALUE        value;
#endif
  bool           isred;
  struct RBNode* left;
  struct RBNode* right;
} RBNode;

// Implement the following functions:

// RBAllocNode allocates memory for a node. It's called by RBSet.
static RBNode* RBAllocNode(RBUSERDATA_);

// RBFreeNode frees up a no-longer used node. Called by RBDelete and RBFree.
//   Invariant: RBNode->left == NULL
//   Invariant: RBNode->right == NULL
static void RBFreeNode(RBNode* node RBUSERDATA_COMMA);

#ifdef RBVALUE
// RBFreeNode frees up a no-longer used value. Called by RBSet when replacing a value.
static void RBFreeValue(RBVALUE RBUSERDATA_COMMA);
#endif

// RBCmp compares two keys. Return a negative value to indicate that the left
// key is smaller than the right, a positive value for the inverse and zero
// to indicate the keys are identical.
static int RBCmp(RBKEY a, RBKEY b RBUSERDATA_COMMA);

// Example implementation:
// inline static RBNode* RBAllocNode() {
//   return (RBNode*)malloc(sizeof(RBNode));
// }
// inline static void RBFreeValue(RBVALUE v) {
//   BarFree((Bar*)v);
// }
// inline static void RBFreeNode(RBNode* node) {
//   FooFree((Foo*)node->key);
//   RBFreeValue(node->value);
//   free(node);
// }
// inline static int RBCmp(RBKEY a, RBKEY b) {
//   return a < b ? -1 : b < a ? 1 : 0;
// }

// -------------------------------------------------------------------------------------
// API implemented by the remainder of this file

// RBHas performs a lookup of k. Returns true if found.
static bool RBHas(const RBNode* n, RBKEY k RBUSERDATA_COMMA);

#ifdef RBVALUE
  // RBGet performs a lookup of k. Returns value or RBVALUE_NOT_FOUND.
  static RBVALUE RBGet(const RBNode* n, RBKEY k RBUSERDATA_COMMA);

  // RBSet adds or replaces value for k. Returns new n.
  static RBNode* RBSet(RBNode* n, RBKEY k, RBVALUE v RBUSERDATA_COMMA);

  // RBSet adds value for k if it does not exist. Returns new n.
  // "added" is set to true when a new value was added, false otherwise.
  static RBNode* RBAdd(RBNode* n, RBKEY k, RBVALUE v, bool* added RBUSERDATA_COMMA);
#else
  // RBInsert adds k. May modify tree even if k exists.
  // *added is set to true if there was no existing entry with key k.
  // Returns new n (n maybe be unchanged even after insertion.)
  static RBNode* RBInsert(RBNode* n, RBKEY k, bool* added RBUSERDATA_COMMA);
#endif

// RBDelete removes k if found. Returns new n.
static RBNode* RBDelete(RBNode* n, RBKEY k RBUSERDATA_COMMA);

// RBClear removes all entries. n is invalid after this operation.
static void RBClear(RBNode* n RBUSERDATA_COMMA);

// Iteration. Return true from callback to keep going.
typedef bool(RBIterator)(const RBNode* n, void* userdata);
static bool RBIter(const RBNode* n, RBIterator* f, void* userdata);

// RBCount returns the number of entries starting at n. O(n) time complexity.
static size_t RBCount(const RBNode* n);

// RBRepr formats n as printable lisp text, useful for inspecting a tree.
// keystr should produce a string representation of a given key.
static Str RBRepr(const RBNode* n, Str s, int depth, Str(keyfmt)(Str,RBKEY));

// -------------------------------------------------------------------------------------


static RBNode* rbNewNode(
  RBKEY key
#ifdef RBVALUE
, RBVALUE value
#endif
  RBUSERDATA_COMMA
) {
  RBNode* node = RBAllocNode(RBUSERDATA_NAME);
  if (node) {
    node->key   = key;
    #ifdef RBVALUE
    node->value = value;
    #endif
    node->isred = true;
    node->left  = NULL;
    node->right = NULL;
  } // else: out of memory
  return node;
}

static void flipColor(RBNode* node) {
  node->isred        = !node->isred;
  node->left->isred  = !node->left->isred;
  node->right->isred = !node->right->isred;
}

static RBNode* rotateLeft(RBNode* l) {
  auto r = l->right;
  l->right = r->left;
  r->left  = l;
  r->isred = l->isred;
  l->isred = true;
  return r;
}

static RBNode* rotateRight(RBNode* r) {
  auto l = r->left;
  r->left  = l->right;
  l->right = r;
  l->isred = r->isred;
  r->isred = true;
  return l;
}

// -------------------------------------------------------------------------------------

inline static void RBClear(RBNode* node RBUSERDATA_COMMA) {
  assert(node);
  if (node->left) {
    RBClear(node->left RBUSERDATA_NAME_COMMA);
  }
  if (node->right) {
    RBClear(node->right RBUSERDATA_NAME_COMMA);
  }
  node->left = (RBNode*)0;
  node->right = (RBNode*)0;
  RBFreeNode(node RBUSERDATA_NAME_COMMA);
}

// -------------------------------------------------------------------------------------


inline static bool RBHas(const RBNode* node, RBKEY key RBUSERDATA_COMMA) {
  do {
    int cmp = RBCmp(key, (RBKEY)node->key RBUSERDATA_NAME_COMMA);
    if (cmp == 0) {
      return true;
    }
    node = cmp < 0 ? node->left : node->right;
  } while (node);
  return false;
}


// inline static const RBNode* RBGetNode(const RBNode* node, RBKEY key RBUSERDATA_COMMA) {
//   do {
//     int cmp = RBCmp(key, (RBKEY)node->key RBUSERDATA_NAME_COMMA);
//     if (cmp == 0) {
//       return node;
//     }
//     node = cmp < 0 ? node->left : node->right;
//   } while (node);
//   return NULL;
// }


#ifdef RBVALUE
inline static RBVALUE RBGet(const RBNode* node, RBKEY key RBUSERDATA_COMMA) {
  do {
    int cmp = RBCmp(key, (RBKEY)node->key RBUSERDATA_NAME_COMMA);
    if (cmp == 0) {
      return (RBVALUE)node->value;
    }
    node = cmp < 0 ? node->left : node->right;
  } while (node);
  return RBVALUE_NOT_FOUND;
}
#endif


inline static size_t RBCount(const RBNode* n) {
  size_t count = 1;
  if (n->left) {
    count += RBCount(n->left);
  }
  return n->right ? count + RBCount(n->right) : count;
}

// -------------------------------------------------------------------------------------
// insert

// RB_TREE_VARIANT changes the tree type. See https://en.wikipedia.org/wiki/2–3–4_tree
// - #define RB_TREE_VARIANT 3:  2-3 tree
// - #define RB_TREE_VARIANT 4:  2-3-4 tree
#define RB_TREE_VARIANT 4


inline static bool isred(RBNode* node) {
  return node && node->isred;
}


static RBNode* rbInsert(
  RBNode* node,
  RBKEY key
#ifdef RBVALUE
, RBVALUE value
#else
, bool* added
#endif
  RBUSERDATA_COMMA
) {
  if (!node) {
    return rbNewNode(key
      #ifdef RBVALUE
      , value
      #endif
      RBUSERDATA_NAME_COMMA
    );
  }

  #if RB_TREE_VARIANT != 3
  // build a 2-3-4 tree
  if (isred(node->left) && isred(node->right)) { flipColor(node); }
  #endif

  int cmp = RBCmp(key, node->key RBUSERDATA_NAME_COMMA);

  #ifdef RBVALUE
  if (cmp < 0) {
    node->left = rbInsert(node->left, key, value RBUSERDATA_NAME_COMMA);
  } else if (cmp > 0) {
    node->right = rbInsert(node->right, key, value RBUSERDATA_NAME_COMMA);
  } else {
    // exists
    auto oldval = node->value;
    node->value = value;
    RBFreeValue(oldval RBUSERDATA_NAME_COMMA);
  }
  #else
  if (cmp < 0) {
    node->left = rbInsert(node->left, key, added RBUSERDATA_NAME_COMMA);
  } else if (cmp > 0) {
    node->right = rbInsert(node->right, key, added RBUSERDATA_NAME_COMMA);
  } else {
    // key exists
    *added = false;
  }
  #endif

  if (isred(node->right) && !isred(node->left))     { node = rotateLeft(node); }
  if (isred(node->left) && isred(node->left->left)) { node = rotateRight(node); }

  #if RB_TREE_VARIANT == 3
  // build a 2-3 tree
  if (isred(node->left) && isred(node->right)) { flipColor(node); }
  #endif

  return node;
}



#ifdef RBVALUE

inline static RBNode* RBSet(RBNode* root, RBKEY key, RBVALUE value RBUSERDATA_COMMA) {
  root = rbInsert(root, key, value RBUSERDATA_NAME_COMMA);
  if (root) {
    // Note: rbInsert returns NULL when out of memory (malloc failure)
    root->isred = false;
  }
  return root;
}

inline static RBNode* RBAdd(RBNode* root, RBKEY key, RBVALUE value, bool* added RBUSERDATA_COMMA) {
  if (!RBHas(root, key RBUSERDATA_NAME_COMMA)) {
    *added = true;
    root = rbInsert(root, key, value RBUSERDATA_NAME_COMMA);
    if (root) {
      root->isred = false;
    }
  } else {
    *added = false;
  }
  return root;
}

#else

inline static RBNode* RBInsert(RBNode* root, RBKEY key, bool* added RBUSERDATA_COMMA) {
  *added = true;
  root = rbInsert(root, key, added RBUSERDATA_NAME_COMMA);
  if (root) {
    // Note: rbInsert returns NULL when out of memory (malloc failure)
    root->isred = false;
  }
  return root;
}

#endif


// -------------------------------------------------------------------------------------
// delete


static RBNode* fixUp(RBNode* node) {
  // re-balance
  if (isred(node->right)) {
    node = rotateLeft(node);
  }
  if (isred(node->left) && isred(node->left->left)) {
    node = rotateRight(node);
  }
  if (isred(node->left) && isred(node->right)) {
    flipColor(node);
  }
  return node;
}

static RBNode* moveRedLeft(RBNode* node) {
  flipColor(node);
  if (isred(node->right->left)) {
    node->right = rotateRight(node->right);
    node = rotateLeft(node);
    flipColor(node);
  }
  return node;
}

static RBNode* moveRedRight(RBNode* node) {
  flipColor(node);
  if (isred(node->left->left)) {
    node = rotateRight(node);
    flipColor(node);
  }
  return node;
}

static RBNode* minNode(RBNode* node) {
  while (node->left) {
    node = node->left;
  }
  return node;
}

static RBNode* rbDeleteMin(RBNode* node RBUSERDATA_COMMA) {
  if (!node->left) {
    // found; delete
    assert(node->right == NULL);
    RBFreeNode(node RBUSERDATA_NAME_COMMA);
    return NULL;
  }
  if (!isred(node->left) && !isred(node->left->left)) {
    node = moveRedLeft(node);
  }
  node->left = rbDeleteMin(node->left RBUSERDATA_NAME_COMMA);
  return fixUp(node);
}

static RBNode* rbDelete(RBNode* node, RBKEY key RBUSERDATA_COMMA) {
  assert(node);
  int cmp = RBCmp(key, node->key RBUSERDATA_NAME_COMMA);
  if (cmp < 0) {
    assert(node->left != NULL);
    if (!isred(node->left) && !isred(node->left->left)) {
      node = moveRedLeft(node);
    }
    node->left = rbDelete(node->left, key RBUSERDATA_NAME_COMMA);
  } else {
    if (isred(node->left)) {
      node = rotateRight(node);
      cmp = RBCmp(key, node->key RBUSERDATA_NAME_COMMA);
    }
    if (cmp == 0 && node->right == NULL) {
      // found; delete
      assert(node->left == NULL);
      RBFreeNode(node RBUSERDATA_NAME_COMMA);
      return NULL;
    }
    assert(node->right != NULL);
    if (!isred(node->right) && !isred(node->right->left)) {
      node = moveRedRight(node);
      cmp = RBCmp(key, node->key RBUSERDATA_NAME_COMMA);
    }
    if (cmp == 0) {
      assert(node->right);
      auto m = minNode(node->right);
      node->key = m->key;
      m->key = (RBKEY)(RBKEY_NULL); // key moved; signal to RBFreeNode
      #ifdef RBVALUE
      node->value = m->value;
      #endif
      node->right = rbDeleteMin(node->right RBUSERDATA_NAME_COMMA);
    } else {
      node->right = rbDelete(node->right, key RBUSERDATA_NAME_COMMA);
    }
  }
  return fixUp(node);
}


inline static RBNode* RBDelete(RBNode* node, RBKEY key RBUSERDATA_COMMA) {
  if (node) {
    node = rbDelete(node, key RBUSERDATA_NAME_COMMA);
    if (node)
      node->isred = false;
  }
  return node;
}


// ---------------------------------------------------------------------------------------



inline static bool RBIter(const RBNode* n, RBIterator* f, void* userdata) {
  if (!f(n, userdata))
    return false;
  if (n->left && !RBIter(n->left, f, userdata))
    return false;
  if (n->right && !RBIter(n->right, f, userdata))
    return false;
  return true;
}


inline static Str RBRepr(const RBNode* n, Str s, int depth, Str(keyfmt)(Str,RBKEY)) {
  if (depth > 0) {
    s = str_appendc(s, '\n');
    s = str_appendfill(s, depth*2, ' ');
  }
  s = str_appendn(s, n->isred ? "(R " : "(B ", 3);
  s = keyfmt(s, n->key);
  if (n->left) {
    s = RBRepr(n->left, s, depth + 2, keyfmt);
  }
  if (n->right) {
    s = RBRepr(n->right, s, depth + 2, keyfmt);
  }
  s = str_appendc(s, ')');
  return s;
}


