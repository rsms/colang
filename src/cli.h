// command line interface functions
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef u8 CliOptionType;
enum CliOptionType {
  CLI_T_BOOL = 1,
  CLI_T_STR,
  CLI_T_INT,
  CLI_T_END_OF_OPTIONS = 0,
} END_ENUM(CliOptionType)

typedef enum CliParseStatus {
  CLI_PS_HELP   =  1,
  CLI_PS_OK     =  0,
  CLI_PS_BADOPT = -1, // unknown option, or missing or invalid value
  CLI_PS_NOMEM  = -2, // ran out of memory appending to rest array
} CliParseStatus;

typedef struct CliOption {
  const char*          longname;  // e.g. "output"
  char                 shortname; // e.g. 'o'
  const char*          valuename; // e.g. "<file>"
  CliOptionType        type;
  const char* nullable help;      // e.g. "Write output to <file>"
  void* nullable       valuep;    // optional pointer to external value storage
  union {
    bool                 boolval;
    const char* nullable strval;
    i64                  intval;
  };
  int _order; // internal
} CliOption;


// cliopt_parse parses arguments, populating options[N].*val and rest.
// Non-option arguments are added to rest.
// If rest is NULL then any arguments encountered (except for options) cause an error.
// i.e. it is assumed that the program does not accept any arguments.
CliParseStatus cliopt_parse(
  CliOption*           options,
  int                  argc,
  const char**         argv,
  CStrArray* nullable  rest,
  const char* nullable usage,
  const char* nullable extra_help);

// cliopt_help prints help to stdout
// If usage is a non-empty string, it is used instead of the default "usage: prog" string.
// If extra_help is provided it is printed after usage options list.
void cliopt_help(
  CliOption*           options,
  const char*          progname,
  bool                 accepts_args,
  const char* nullable usage,
  const char* nullable extra_help);

// cliopt_bool returns true if flag is set in options
static bool cliopt_bool(CliOption* opts, const char* name);
bool cliopt_booln(CliOption* opts, const char* name, usize namelen);

// cliopt_str returns the string value for an option (defaultval if not found or set.)
static const char* nullable cliopt_str(
  CliOption*, const char* name, const char* nullable defaultval);
const char* nullable cliopt_strn(
  CliOption*, const char* name, usize namelen, const char* nullable defaultval);

// cliopt_find locates an option in the opts array. Returns NULL if not found.
// If consider_short is true, then name[0] is considered for shortname if no
// option with longname was found.
CliOption* nullable cliopt_find(
  CliOption* opts, const char* name, usize namelen, bool consider_short);

//———————————————————————————————————————————————————————————————————————————————————————
// internal

inline static bool cliopt_bool(CliOption* opts, const char* name) {
  return cliopt_booln(opts, name, strlen(name));
}

inline static const char* cliopt_str(
  CliOption* opts, const char* name, const char* nullable defaultval)
{
  return cliopt_strn(opts, name, strlen(name), defaultval);
}

END_INTERFACE
