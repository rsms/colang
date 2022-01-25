#include "coimpl.h"
#include "coparse.h"

// Lookup table TypeCode => string encoding char
const char _TypeCodeEncodingMap[TC_END] = {
  #define _(name, encoding, _flags) encoding,
  // IMPORTANT: order of macro invocations must match enum TypeCode
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  DEF_TYPE_CODES_PUB(_)
  DEF_TYPE_CODES_ETC(_)
  #undef _
};
