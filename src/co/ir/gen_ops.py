#!/usr/bin/env python
# encoding: utf8
#
# This script reads from:
# - ir/arch_*.lisp
# - parse/parse.h
# - types.h
# and patches the following files:
# - ir/op.h
# - ir/op.c
# - ir/ir-ast.c
#
import re, sys, os, os.path
from functools import reduce
import pprint

SRCFILE_IR_OP_C   = "op.c"
SRCFILE_IR_OP_H   = "op.h"
SRCFILE_AST_IR_C  = "ir-ast.c"
SRCFILE_ARCH_BASE = "arch_base.lisp"
SRCFILE_PARSE_H   = "../parse/parse.h"
SRCFILE_TYPES_H   = "../types.h"

pp = pprint.PrettyPrinter(indent=2)
def rep(any): return pp.pformat(any)

# change directory to project root, i.e. "(dirname $0)/.."
scriptfile = os.path.abspath(__file__)
os.chdir(os.path.dirname(scriptfile))
scriptname = os.path.relpath(scriptfile)  # e.g. "misc/gen_ops.py"

DRY_RUN = False  # don't actually write files
DEBUG   = False  # log stuff to stdout

# S-expression types
Symbol = str              # A Scheme Symbol is implemented as a Python str
Number = (int, float)     # A Scheme Number is implemented as a Python int or float
Atom   = (Symbol, Number) # A Scheme Atom is a Symbol or Number
List   = list             # A Scheme List is implemented as a Python list
Exp    = (Atom, List)     # A Scheme expression is an Atom or List

# TypeCode => [irtype ...]
# Note: loadTypeCodes verifies that this list exactly matches the list in types.h
typeCodeToIRType :{str:[str]} = {
  # multiple irtypes should be listed from most speecific to most generic
  "bool":    ["bool"],
  "int8":    ["s8",  "i8"],
  "uint8":   ["u8",  "i8"],
  "int16":   ["s16", "i16"],
  "uint16":  ["u16", "i16"],
  "int32":   ["s32", "i32"],
  "uint32":  ["u32", "i32"],
  "int64":   ["s64", "i64"],
  "uint64":  ["u64", "i64"],
  "float32": ["f32"],
  "float64": ["f64"],
}

typeCodeAliases = {
  "int":  "int32",
  "uint": "uint32",
}

auxTypes = [
  "IRAuxBool",
  "IRAuxI8",
  "IRAuxI16",
  "IRAuxI32",
  "IRAuxI64",
  "IRAuxF32",
  "IRAuxF64",
  "IRAuxMem",
  "IRAuxSym",
]
irtypeToAuxType = {
  "bool" : "IRAuxBool",
  "i8"   : "IRAuxI8",
  "i16"  : "IRAuxI16",
  "i32"  : "IRAuxI32",
  "i64"  : "IRAuxI64",
  "f32"  : "IRAuxF32",
  "f64"  : "IRAuxF64",
  "mem"  : "IRAuxMem",
  "sym"  : "IRAuxSym",
}

# irtype => [ TypeCode ... ]
irTypeToTypeCode = {"mem":"mem"}
for typeCodeName, irtypes in typeCodeToIRType.items():
  for irtype in irtypes:
    v = irTypeToTypeCode.get(irtype, [])
    v.append(typeCodeName)
    if len(v) == 1:
      irTypeToTypeCode[irtype] = v
longestTypeCode = reduce(lambda a, v: max(a, len(v)), typeCodeToIRType.keys(), 0)

# Maps AST operators to IR operator prefix
# Note: loadASTOpTokens verifies that this list exactly matches the list in token.h
astOpToIROpPrefix = {
  # AST token    IROp prefix
  #              (1-input, 2-input)
  "TStar":       (None,    "Mul"),     # *
  "TSlash":      (None,    "Div"),     # /
  "TShl":        (None,    "ShL"),     # <<
  "TShr":        (None,    "ShR"),     # >>
  "TAnd":        (None,    "And"),     # &
  "TPercent":    (None,    "Rem"),     # %

  "TPlus":       ("Pos",   "Add"),     # +
  "TMinus":      ("Neg",   "Sub"),     # -
  "TPipe":       (None,    "Or"),      # |
  "THat":        ("Compl", "XOr"),     # ^  complement
  "TTilde":      ("BNot",  None),      # ~

  "TEq":         (None,    "Eq"),      # ==
  "TNEq":        (None,    "NEq"),     # !=
  "TLt":         (None,    "Less"),    # <
  "TLEq":        (None,    "LEq"),     # <=
  "TGt":         (None,    "Greater"), # >
  "TGEq":        (None,    "GEq"),     # >=

  "TPlusPlus":   ("Incr",  None),        # ++
  "TMinusMinus": ("Decr",  None),        # --
  "TExcalm":     ("Not",   None),        # !
}

# Operator flags. Encoded as bitflags and thus have a count limit of 31.
OpFlags = [
  ("ZeroWidth"         , 'dummy op; no actual I/O.'),
  ("Constant"          , 'true if the value is a constant. Value in aux'),
  ("Commutative"       , 'commutative on its first 2 arguments (e.g. addition; x+y==y+x)'),
  ("ResultInArg0"      , 'output of v and v.args[0] must be allocated to the same register.'),
  ("ResultNotInArgs"   , 'outputs must not be allocated to the same registers as inputs'),
  ("Rematerializeable" , 'register allocator can recompute value instead of spilling/restoring.'),
  ("ClobberFlags"      , 'this op clobbers flags register'),
  ("Call"              , 'is a function call'),
  ("NilCheck"          , 'this op is a nil check on arg0'),
  ("FaultOnNilArg0"    , 'this op will fault if arg0 is nil (and aux encodes a small offset)'),
  ("FaultOnNilArg1"    , 'this op will fault if arg1 is nil (and aux encodes a small offset)'),
  ("UsesScratch"       , 'this op requires scratch memory space'),
  ("HasSideEffects"    , 'for "reasons", not to be eliminated. E.g., atomic store.'),
  ("Generic"           , 'generic op'),
  ("Lossy"             , 'operation may be lossy. E.g. converting i32 to i16.'),
]
if len(OpFlags) > 31:
  raise Exception("too many OpFlags to work as bitflags")
OpFlagsKeySet = set([k for k, _ in OpFlags])

# operator attributes which has a value. E.g. "(aux i32)"
OpKeyAttributes = set([
  "aux",
])

# the IROpDescr struct's fields. (type, name, comment)
IROpDescr = [
  ("IROpFlag", "flags",      ""),
  ("TypeCode", "outputType", "invariant: < TypeCode_NUM_END"),
  ("IRAux",    "aux",        "type of data in IRValue.aux"),
]

if DEBUG:
  print("DEBUG=True DRY_RUN=%r cwd=%r" % (DRY_RUN, os.getcwd()))

class Op:
  def __init__(self, name :str, input :Exp, output :Exp, attributes :List, commentsPre :[str]):
    self.name = name
    self.input = input
    self.output = output
    self.commentsPre = commentsPre
    self.commentsPost = []
    self.attributes = {}
    self.flags = set()

    # inputSig is a string of the input, useful as a key of dicts
    if isinstance(self.input, Atom):
      self.inputSig = str(self.input)
      self.inputCount = 1
    else:
      self.inputSig = " ".join([str(v) for v in self.input])
      self.inputCount = len(self.input)

    # outputSig is a string of the output, useful as a key of dicts
    if isinstance(self.output, Atom):
      self.outputSig = str(self.output)
      self.outputCount = 1
    else:
      self.outputSig = " ".join([str(v) for v in self.output])
      self.outputCount = len(self.output)

    for attr in attributes:
      if isinstance(attr, Atom):
        if attr not in OpFlagsKeySet:
          raise Exception(
            "unexpected attribute %r in op %s.\nExpected one of:\n  %s" %
            (attr, self.name, "\n  ".join(sorted(OpFlagsKeySet))))
        self.flags.add(attr)
      else:
        if len(attr) < 2:
          raise Exception("invalid attribute %r in op %s" % (attr, self.name))
        key = attr[0]
        if key not in OpKeyAttributes:
          raise Exception(
            "unexpected attribute %r in op %s.\nExpected one of:\n  %s" %
            (key, self.name, "\n  ".join(sorted(OpKeyAttributes))))
        self.attributes[key] = attr[1:]

class Arch:
  isGeneric = False
  name = "_"
  addrSize = 0
  regSize = 0
  intSize = 0
  ops :[Op] = []
  def __init__(self, sourcefile :str):
    self.sourcefile = sourcefile


# ------------------------------------------------------------------------------------------------
# main


def main():
  typeCodes = loadTypeCodes(SRCFILE_TYPES_H)
  astOps = loadASTOpTokens(SRCFILE_PARSE_H)
  baseArch = parse_arch_file(SRCFILE_ARCH_BASE)
  baseArch.isGeneric = True
  if DEBUG:
    print("baseArch:", {
      "addrSize": baseArch.addrSize,
      "regSize":  baseArch.regSize,
      "intSize":  baseArch.intSize,
      "ops":      len(baseArch.ops)
    })
  archs = [ baseArch ]
  gen_IR_OPS(archs)
  gen_IROpNames(archs)
  gen_IROpConstMap(baseArch, typeCodes)
  gen_IROpSwitches(baseArch, typeCodes, astOps) # => SRCFILE_AST_IR_C
  gen_IROpConvTable(baseArch, typeCodes)
  gen_IROpFlag()
  gen_IRAux()
  gen_IROpDescr()
  gen_IROpInfo(archs, typeCodes)


# ------------------------------------------------------------------------------------------------
# generate output code


def gen_IROpInfo(archs :[Arch], typeCodes :[str]):
  startline = 'const IROpDescr _IROpInfoMap[Op_MAX] = {'
  endline   = '};'
  lines = [ startline, '  // Do not edit. Generated by %s' % scriptname ]
  for a in archs:
    for op in a.ops:
      fields :[str] = []
      for _, name, _ in IROpDescr:
        value = "0"
        comment = ""

        if name == "outputType":
          if isinstance(op.output, Atom):
            typeCodes = irTypeToTypeCode[op.output]
            if len(typeCodes) > 1:
              # multiple output types means that the result depends on the input types
              # print("TODO: multiple possible TypeCodes %r for ir type %r" % (typeCodes, op.output))
              if isinstance(op.input, Atom):
                value = "TypeCode_param1"
              elif len(op.input) == 0:
                # special case of ops that "generate" values, like constants.
                value = "TypeCode_param1"
              else:
                value = ""
                i = 1
                for intype in op.input:
                  if intype == op.output:
                    value = "TypeCode_param%d" % i
                    break
                  i += 1
                if value == "" or i > 2:
                  raise Exception(
                    "variable output type %r of Op%s without matching input %r" % (
                    op.output, op.name, op.input))
              value = "TypeCode_param1"
              comment = op.output
            else:
              value = "TypeCode_" + typeCodes[0]
          elif len(op.output) == 0:
            value = "TypeCode_nil"
          else:
            raise Exception("TODO: implement handling of multi-output ops (%s -> %r) in %s" % (
              op.name, op.output, scriptname))

        elif name == "flags":
          if len(op.flags) == 0:
            value = "IROpFlagNone"
          else:
            value = "|".join([ "IROpFlag" + flag for flag in sorted(op.flags) ])

        elif name == "aux":
          aux = op.attributes.get("aux")
          value = "IRAuxNone"
          if aux:
            if len(aux) != 1:
              raise Exception(
                "invalid aux arguments in op %s (expected exactly 1 irtype; got %r)" %
                (op.name, aux))
            irtype = aux[0]
            if irtype not in irtypeToAuxType:
              raise Exception(
                "invalid aux type %s in op %s (expected one of: %s)" %
                (irtype, op.name, ", ".join(irtypeToAuxType.keys())))
            value = irtypeToAuxType[irtype]

        else:
          raise Exception("unexpected field %r in IROpDescr" % name)

        if len(comment) > 0:
          fields.append("%s/*%s*/" % (value, comment))
        else:
          fields.append(value)
        # lines.append("    /* %s = */ %s,%s" % (name, value, comment))

      lines.append("  { /* Op%s */ %s }," % (op.name, ", ".join(fields)))

    if a.isGeneric:
      # ZERO entry for Op_GENERIC_END
      lines.append("  {0,0,0}, // Op_GENERIC_END")
  lines.append(endline)
  if DEBUG:
    print("\n".join(lines))
  replaceInSourceFile(SRCFILE_IR_OP_C, startline, "\n"+endline, "\n".join(lines))


def gen_IROpDescr():
  startline = 'typedef struct IROpDescr {'
  endline   = '} IROpDescr;'
  lines = [ startline, '  // Do not edit. Generated by %s' % scriptname ]
  typeWidth = reduce(lambda a, v: max(a, len(v[0])), IROpDescr, 0)
  nameWidth = reduce(lambda a, v: max(a, len(v[1])), IROpDescr, 0)
  for type, name, comment in IROpDescr:
    if len(comment) > 0:
      comment = " // " + comment
    lines.append(("  {0:<{typeWidth}} {1:<{nameWidth}}{2}".format(
      type,
      name + ";",
      comment,
      typeWidth=typeWidth,
      nameWidth=nameWidth+1)).rstrip())
  lines.append(endline)
  if DEBUG:
    print("\n".join(lines))
  replaceInSourceFile(SRCFILE_IR_OP_H, startline, "\n"+endline, "\n".join(lines))


def gen_IRAux():
  startline = 'typedef enum IRAux {'
  endline   = '} IRAux;'
  lines = [
    startline,
    '  // Do not edit. Generated by %s' % scriptname,
    '  IRAuxNone = 0,',
  ]
  for s in auxTypes:
    lines.append("  {0},".format(s))
  lines.append(endline)
  if DEBUG:
    print("\n".join(lines))
  replaceInSourceFile(SRCFILE_IR_OP_H, startline, "\n"+endline, "\n".join(lines))


def gen_IROpFlag():
  startline = 'typedef enum IROpFlag {'
  endline   = '} IROpFlag;'
  lines = [
    startline,
    '  // Do not edit. Generated by %s' % scriptname,
    '  IROpFlagNone = 0,',
  ]
  nameWidth = reduce(lambda a, v: max(a, len(v[0])), OpFlags, 0)
  value = 0
  for name, comment in OpFlags:
    lines.append("  IROpFlag{0:<{nameWidth}} = 1 << {1:<2},// {2}".format(
      name,
      value,
      comment,
      nameWidth=nameWidth))
    value += 1
  lines.append(endline)
  if DEBUG:
    print("\n".join(lines))
  replaceInSourceFile(SRCFILE_IR_OP_H, startline, "\n"+endline, "\n".join(lines))


def gen_IROpConvTable(baseArch :Arch, typeCodes :[str]):
  # generates matrix table of size TypeCode_NUM_END*2,
  # mapping fromType*toType to a Conv* operation.
  # const IROp _IROpConvMap[TypeCode_NUM_END][TypeCode_NUM_END];

  # typeCodeToIRType TypeCode => [irtype ...]
  # irTypeToTypeCode irtype => [ TypeCode ... ]

  # print("irTypeToTypeCode", rep(irTypeToTypeCode))
  # print("typeCodeToIRType", rep(typeCodeToIRType))

  table = {"bool":{}} # input type => { output type => op }
  outTypeCodes = set(["bool"]) # tracks output types, used for error checking

  for op in baseArch.ops:
    if not op.name.startswith("Conv"):
      continue
    if not isinstance(op.input, Atom):
      raise Exception("conversion op %s is expected to have exactly 1 input" % op.name)
    if not isinstance(op.output, Atom):
      raise Exception("conversion op %s is expected to have exactly 1 output" % op.name)

    for inTypeCode in irTypeToTypeCode[op.input]:
      for outTypeCode in irTypeToTypeCode[op.output]:
        # print("%s\t->  %s\t%s" % (inTypeCode, outTypeCode, op.name))
        outTypeCodes.add(outTypeCode)
        m = table.get(inTypeCode)
        if m is None:
          m = {}
          table[inTypeCode] = m
        if outTypeCode in m:
          raise Exception("duplicate conflicting conversion ops for %s->%s: %s and %s" % (
            inTypeCode, outTypeCode, m[outTypeCode].name, op.name))
        m[outTypeCode] = op

  # fail if not all known TypeCodes are covered
  if len(table) < len(typeCodes) - len(typeCodeAliases):
    diff = set(typeCodes).difference(table)
    raise Exception("not all source types covered; missing: %s -> *" % " -> *, ".join(diff))
  if len(outTypeCodes) < len(typeCodes) - len(typeCodeAliases):
    diff = set(typeCodes).difference(outTypeCodes)
    raise Exception("not all destination types covered; missing: * -> %s" % ", * -> ".join(diff))

  # generate 2-D table
  startline = 'const IROp _IROpConvMap[TypeCode_NUM_END][TypeCode_NUM_END] = {'
  endline   = '};'
  lines = [
    startline,
    '  // Do not edit. Generated by %s' % scriptname,
  ]

  # TypeCodes must be listed in order of the TypeCode enum.
  # typeCodes is parsed from src/types.h
  missing = []
  for inTypeCode in typeCodes:
    lines.append("  { // %s -> ..." % inTypeCode)
    inTypeCode = typeCodeAliases.get(inTypeCode,inTypeCode)
    m = table.get(inTypeCode)
    for outTypeCode in typeCodes:
      outTypeCodeC = typeCodeAliases.get(outTypeCode,outTypeCode)
      opname = "Nil"
      if m is not None:
        op = m.get(outTypeCodeC)
        if op is not None:
          opname = op.name
      if opname == "Nil" and inTypeCode != outTypeCodeC:
        if (outTypeCodeC[0] == "u" and outTypeCodeC[1:] == inTypeCode) or \
           (inTypeCode[0] == "u" and inTypeCode[1:] == outTypeCodeC):
          # ignore signed differences. e.g. uint32 -> int32
          pass
        else:
          missing.append("%s -> %s" % (inTypeCode, outTypeCode))
      lines.append("    /* -> %s */ Op%s," % (outTypeCode, opname))
    lines.append("  },")

  # TODO: fix this so that we don't print extensions with sign diffs, e.g. i16 -> u32.
  # # if any pairs are missing, print but don't fail
  # if len(missing) > 0:
  #   print("conversion: Not all types covered by conversion. Missing:\n  %s" % (
  #     "\n  ".join(missing)
  #   ), file=sys.stderr)

  # finalize table
  lines.append(endline)
  if DEBUG:
    print("\n".join(lines))
  replaceInSourceFile(SRCFILE_IR_OP_C, startline, "\n"+endline, "\n".join(lines))


def gen_IROpSwitches(baseArch :Arch, typeCodes :[str], astOps):
  # # preprocess astOps
  # astOpMap = {}
  # longestTok = 0
  # longestAstComment = 0
  # for tok, comment in astOps:
  #   astOpMap[tok] = comment
  #   longestTok = max(longestTok, len(tok))
  #   longestAstComment = max(longestAstComment, len(comment))

  # map ops
  iropsByInput = {}  # inputSig => [ (astPrefix, Op) ... ]
  for astTok, opPrefixes in astOpToIROpPrefix.items():
    opPrefix1Input, opPrefix2Input = opPrefixes
    for op in baseArch.ops:
      if (opPrefix1Input and op.name.startswith(opPrefix1Input)) or \
         (opPrefix2Input and op.name.startswith(opPrefix2Input)):
        v = iropsByInput.get(op.inputSig, [])
        v.append((astTok, op))
        iropsByInput[op.inputSig] = v

  revTypeCodeAliases = {}
  for alias, target in typeCodeAliases.items():
    v = revTypeCodeAliases.get(target,[])
    v.append(alias)
    revTypeCodeAliases[target] = v

  def genAstOpToIrOpSwitch(lines, typeCode, ops):
    for alias in revTypeCodeAliases.get(typeCode,[]):
      lines.append('      case TypeCode_%s:' % alias)
    lines.append('      case TypeCode_%s: switch (tok) {' % typeCode)
    longestAstOp = 0
    longestIrOp = 0
    for astOp, op in ops:
      longestAstOp = max(longestAstOp, len(astOp))
      longestIrOp  = max(longestIrOp, len(op.name))
    for astOp, op in ops:
      lines.append('        case %-*s : return Op%-*s ;// %s -> %s' %
        (longestAstOp, astOp, longestIrOp, op.name, op.inputSig, op.outputSig))
    lines.append('        default: return OpNil;')
    lines.append('      }')

  startline = '  //!BEGIN_AST_TO_IR_OP_SWITCHES'
  endline   = '  //!END_AST_TO_IR_OP_SWITCHES'
  lines = [
    startline,
    '// Do not edit. Generated by %s' % scriptname,
  ]

  lines.append('switch (type1) {')
  i = 0
  for type1 in typeCodes:
    if type1 in typeCodeAliases:
      continue

    for alias in revTypeCodeAliases.get(type1,[]):
      lines.append('  case TypeCode_%s:' % alias)
    lines.append('  case TypeCode_%s:' % type1)
    lines.append('    switch (type2) {')

    # 1-input
    ops1 = []  # [ (astPrefix, Op) ... ]
    for irtype in typeCodeToIRType[type1]:
      ops1 += iropsByInput.get(irtype, [])
    # print(rep(ops1))
    if len(ops1) > 0:
      genAstOpToIrOpSwitch(lines, 'nil', ops1)

    # 2-input
    for type2 in typeCodes:
      if type2 in typeCodeAliases:
        continue

      ops2 = []  # [ (astPrefix, Op) ... ]
      for irtype1 in typeCodeToIRType[type1]:
        for irtype2 in typeCodeToIRType[type2]:
          ops2 += iropsByInput.get(irtype1 + " " + irtype2, [])

      if len(ops2) > 0:
        genAstOpToIrOpSwitch(lines, type2, ops2)

    lines.append('      default: return OpNil;')
    lines.append('    } // switch (type2)')
  lines.append('  default: return OpNil;')
  lines.append('} // switch (type1)')

  lines.append(endline)
  if DEBUG:
    print("\n  ".join(lines))

  replaceInSourceFile(SRCFILE_AST_IR_C, startline, endline, "\n  ".join(lines))



def mustFind(s, substr, start=None):
  i = s.find(substr, start)
  if i == -1:
    raise Exception("substring %r not found" % substr)
  return i


def gen_IROpConstMap(baseArch :Arch, typeCodes :[str]):
  constOps = createConstantOpsMap([baseArch])  # { irtype: [Op] }
  # print("constOps", constOps)
  startline = 'const IROp _IROpConstMap[TypeCode_NUM_END] = {'
  endline   = '};'
  lines = [
    startline,
    '  // Do not edit. Generated by %s' % scriptname,
  ]
  for typeCode in typeCodes:
    # find best matching constop
    op = None
    for irtype in typeCodeToIRType[typeCodeAliases.get(typeCode, typeCode)]:
      op = constOps.get(irtype)
      if op:
        break
    if not op:
      raise Exception("no constant op for TypeCode %s" % typeCode)
    lines.append("  /* TypeCode_%-*s = */ Op%s," % (longestTypeCode, typeCode, op.name))

  lines.append(endline)
  if DEBUG:
    print("\n".join(lines))
  replaceInSourceFile(SRCFILE_IR_OP_C, startline, "\n"+endline, "\n".join(lines))


def gen_IROpNames(archs :[Arch]):
  startline = 'const char* const IROpNames[Op_MAX] = {'
  endline   = '};'
  lines = [
    startline,
    '  // Do not edit. Generated by %s' % scriptname,
  ]
  longestName = 0
  for a in archs:
    for op in a.ops:
      lines.append('  "%s",' % op.name)
      longestName = max(longestName, len(op.name))
    if a.isGeneric:
      lines.append('  "?", // Op_GENERIC_END')
  lines.append(endline)
  if DEBUG:
    print("\n".join(lines))
  replaceInSourceFile(SRCFILE_IR_OP_C, startline, endline, "\n".join(lines))
  # IROpNamesMaxLen:
  lines = [
    '// IROpNamesMaxLen = longest name in IROpNames',
    '//!Generated by %s -- do not edit.' % scriptname,
    '#define IROpNamesMaxLen %d' % longestName,
    '//!EndGenerated',
  ]
  replaceInSourceFile(SRCFILE_IR_OP_H, lines[0], lines[len(lines)-1], "\n".join(lines))


def gen_IR_OPS(archs :[Arch]):
  startline = 'typedef enum IROp {'
  endline   = '} IROp;'
  lines = [ startline ]
  for a in archs:
    lines.append("  // generated by %s from %s" % (scriptname, a.sourcefile))
    for op in a.ops:

      for comment in op.commentsPre:
        lines.append("  //%s" % comment)

      postcomment = " ".join(op.commentsPost)
      if len(postcomment) > 0:
        postcomment = "\t//%s" % postcomment

      lines.append("  Op%s,%s" % (op.name, postcomment))
    if a.isGeneric:
      lines.append('')
      lines.append('  Op_GENERIC_END, // ---------------------------------------------')
  lines.append('')
  lines.append('  Op_MAX')
  lines.append(endline)
  if DEBUG:
    print("\n".join(lines))
  replaceInSourceFile(SRCFILE_IR_OP_H, startline, endline, "\n".join(lines))


def replaceInSourceFile(filename, findstart, findend, body):
  source = ""
  with open(filename, "r") as f:
    source = f.read()

  start = source.find(findstart)
  end   = source.find(findend, start)

  if start == -1:
    raise Exception("can't find %r in %s" % (findstart, filename))
  if end == -1:
    raise Exception("can't find %r in %s" % (findend, filename))

  # safeguard to make sure that the new content contains findstart and findend so that
  # subsequent calls don't fail
  bodystart = body.find(findstart)
  if bodystart == -1:
    raise Exception(
      ("Can't find %r in replacement body. Writing would break replacement." +
       " To rename the start/end line, first rename in source file %s then in %s") %
      (findstart, filename, scriptname))
  if body.find(findend, bodystart) == -1:
    raise Exception(
      ("Can't find %r in replacement body. Writing would break replacement." +
       " To rename the start/end line, first rename in source file %s then in %s") %
      (findend, filename, scriptname))

  source2 = source[:start] + body + source[end + len(findend):]

  # write changes only if we modified the source
  if source2 == source:
    if not DRY_RUN:
      # touch mtime to satisfy build systems like make
      with os.fdopen(os.open(filename, flags=os.O_APPEND)) as f:
        os.utime(f.fileno() if os.utime in os.supports_fd else filename)
    return False
  if DRY_RUN:
    print(scriptname + ": patch", filename, " (dry run)")
  else:
    print(scriptname + ": patch", filename)
    with open(filename, "w") as f:
      f.write(source2)
      return True



# ------------------------------------------------------------------------------------------------
# types


def createConstantOpsMap(archs :[Arch]) -> {str:[Op]}:
  # build map of constant ops.
  constOps :{str:Op} = {}  # irtype => ops. e.g. i32 => [op1, op2]
  for a in archs:
    for op in a.ops:
      if "Constant" in op.flags:
        if not isinstance(op.output, str):
          raise Exception("constant op %r produces multiple outputs; should produce one" % op.name)
        if op.output in constOps:
          raise Exception("duplicate constant op %r for type %r" % (op.name, op.output))
        constOps[op.output] = op
  return constOps


def loadASTOpTokens(filename :str) -> [(str,str)]:
  tokens = []
  started = 0
  ended = False

  startSubstring = b"#define TOKENS("
  startName = "T_PRIM_OPS_START"
  endName   = "T_PRIM_OPS_END"
  pat = re.compile(r'\s*_\(\s*(\w+)\s*,\s*"([^"]*)"')  # _( name, "repr" )
  verifiedNames = set()

  with open(filename, "rb") as fp:
    for line in fp:
      if started == 0:
        if line.find(startSubstring) != -1:
          started = True
      else:
        s = line.decode("utf8")
        m = pat.match(s)
        if m:
          name = m.group(1)
          if started == 1:
            if name == startName:
              started = 2
          else:
            if name == endName:
              ended = True
              break
            if name in astOpToIROpPrefix:
              verifiedNames.add(name)
              tokens.append((name, m.group(2).replace('\\"', '"')))
            else:
              raise Exception("AST operator token %r missing in astOpToIROpPrefix map" % name)
  if started == 0:
    raise Exception("unable to find start substring %r" % startSubstring)
  if started == 1:
    raise Exception("unable to find start value %r" % startName)
  if not ended:
    raise Exception("unable to find ending value %r" % endName)

  if len(astOpToIROpPrefix) != len(verifiedNames):
    diff = set(astOpToIROpPrefix.keys()).difference(verifiedNames)
    raise Exception(
      "%d name(s) in astOpToIROpPrefix missing in %s: %s" %
      (len(diff), filename, ", ".join(diff)))

  return tokens


def loadTypeCodes(filename :str) -> [str]:
  typeCodes = []
  started = False
  ended = False
  startSubstring = b"#define TYPE_CODES"
  endName = "NUM_END"
  verifiedTypeCodes = set()
  pat = re.compile(r'\s*_\(\s*(\w+)')  # _( name, ... )

  with open(filename, "rb") as fp:
    for line in fp:
      if not started:
        if line.find(startSubstring) != -1:
          started = True
      else:
        s = line.decode("utf8")
        m = pat.match(s)
        if m:
          name = m.group(1)
          if name == endName:
            ended = True
            break
          else:
            typeCodes.append(name)
            if name in typeCodeToIRType or name in typeCodeAliases:
              verifiedTypeCodes.add(name)
            else:
              raise Exception("TypeCode %r missing in typeCodeToIRType map" % name)
            # print(s, m.group(1))
  if not started:
    raise Exception("unable to find start substring %r" % startSubstring)
  if not ended:
    raise Exception("unable to find ending typecode %r" % endName)
  if len(typeCodeToIRType) + len(typeCodeAliases) != len(verifiedTypeCodes):
    diff = set(typeCodeToIRType.keys()).difference(verifiedTypeCodes)
    raise Exception(
      "%d TypeCode(s) in typeCodeToIRType missing in %s: %s" %
      (len(diff), filename, ", ".join(diff)))
  return typeCodes

# ------------------------------------------------------------------------------------------------
# parse input lisp

def parse_op(xs, commentsPre) -> Op:
  # Name INPUT -> OUTPUT ATTRIBUTES
  # 0    1     2  3      4...
  #
  # Examples:
  #   (AddI16   (i16 i16) -> i16  Commutative  ResultInArg0)
  #   (ConstI32 () -> u32         Constant  (aux u32))
  #
  if len(xs) < 4:
    raise Exception("not enough elements in op: %r" % xs)
  name = xs[0]
  if not isinstance(name, Symbol):
    err("operation should start with a name. %r" % xs)
  if xs[2] != "->":
    err("missing '->' in %r" % xs)
  input = xs[1]
  output = xs[3]
  attributes = xs[4:]
  # print(name, input, output, attributes)
  return Op(name, input, output, attributes, commentsPre)


def parse_arch_file(filename: str) -> Exp:
  a = Arch(filename)

  doc = None
  with open(filename, "rb") as fp:
    doc = parse_lisp( "(\n" + fp.read().decode("utf8") + "\n)" )
    # print(doc)

  for e in doc:
    if isinstance(e, list) and (e[0] == ";" or e[0] == ";;"):
      continue # skip comment

    # accumulators
    commentsPre = []
    commentsPost = []

    # print(e)
    key = e[0] #.lower()
    if key == "ops":
      lastop = None
      for x in e[1:]:
        if not isinstance(x, list):
          err("unexpected %r in ops spec" % x)

        if len(x) > 0:
          if x[0] == ";":
            comment = decodeComment(x[1]) if len(x) > 1 else ""
            # if len(comment) > 0 or len(commentsPre) > 0:
            commentsPre.append(comment)
            continue
          if x[0] == ";;":
            comment = decodeComment(x[1]) if len(x) > 1 else ""
            # if len(comment) > 0 or len(commentsPost) > 0:
            commentsPost.append(comment)
            continue

        if lastop and len(commentsPost):
          lastop.commentsPost = commentsPost
          commentsPost = []

        lastop = parse_op(x, commentsPre)
        a.ops.append(lastop)
        commentsPre = []

      # trailing/last op
      if lastop and len(commentsPost):
        lastop.commentsPost = commentsPost
        commentsPost = []

    elif hasattr(a, key):
      if len(e) != 2:
        err("unexpected attribute value %r" % e[1:])
      setattr(a, key, e[1])
    else:
      err("unexpected arch attribute %r" % key)

  return a


# S-expression / LISP parser from https://norvig.com/lispy.html

def parse_lisp(program: str) -> Exp:
  "Read a Scheme expression from a string."
  return read_from_tokens(tokenize(program))


comment_re = re.compile(r'^([^;\n]*);+([^\n]*)\n', re.M)
tokenize_re = re.compile(r'([\(\)])')

comment_enc_table = str.maketrans(" ()", "\x01\x02\x03")
comment_dec_table = str.maketrans("\x01\x02\x03", " ()")

def encodeComment(s):
  return s.translate(comment_enc_table)

def decodeComment(s):
  return s.translate(comment_dec_table)


def tokenize(text: str) -> list:
  "Convert a string of characters into a list of tokens."
  includeComments = True

  def replaceComment(m):
    commentType = ';'
    # print(repr(m.group(1).strip()))
    if len(m.group(1).strip()) > 0:
      commentType = ';;'  # trailing
    return "%s(%s %s)\n" % (m.group(1), commentType, encodeComment(m.group(2)))

  def stripComment(m):
    return m.group(1) + "\n"

  if includeComments:
    text = comment_re.sub(replaceComment, text)
  else:
    text = comment_re.sub(stripComment, text)
  # text = text.replace(',', ' ')
  text = tokenize_re.sub(" \\1 ", text)  # "a(b)c" => "a ( b ) c"
  # print(text)
  return text.split()


def read_from_tokens(tokens: list) -> Exp:
  "Read an expression from a sequence of tokens."
  if len(tokens) == 0:
    raise SyntaxError('unexpected EOF')
  token = tokens.pop(0)
  if token == '(':
    L = []
    while tokens[0] != ')':
      L.append(read_from_tokens(tokens))
    tokens.pop(0) # pop off ')'
    # if len(L) == 1 and isinstance(L[0], list) and L[0][0] == ';':
    #   # unwrap comment
    #   return L[0]
    return L
  elif token == ')':
    raise SyntaxError('unexpected )')
  else:
    return atom(token)


def atom(token: str) -> Atom:
  "Numbers become numbers; every other token is a symbol."
  try: return int(token)
  except ValueError:
    try: return float(token)
    except ValueError:
      return Symbol(token)


def err(msg):
  print(msg)
  sys.exit(1)


main()
