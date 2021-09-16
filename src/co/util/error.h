#pragma once

typedef struct ErrorValue ErrorValue;
typedef const ErrorValue* Error;

#define ErrorNone ((Error)NULL)

Error err_make(int code, const char* message);
Error err_makef(int code, const char* messagefmt, ...) ATTR_FORMAT(printf, 2, 3);
Error err_makefv(int code, const char* messagefmt, va_list);

static int         err_code(Error nullable e);
static const char* err_msg(Error nullable e);

// —————————————————————————————————————————————————————————————————————————————————
// implementation

struct ErrorValue {
  int         code;
  const char* message;
};

inline static int         err_code(Error nullable e) { return e ? e->code : 0; }
inline static const char* err_msg(Error nullable e)  { return e ? e->message : ""; }
