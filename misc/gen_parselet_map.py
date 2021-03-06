#!/usr/bin/env python3
#
# This script reads and updates the parselet map in src/co/parse/parse.c
#
import re, sys, os, os.path

def err(msg):
  print(msg)
  sys.exit(1)

# os.chdir(srcdir)

if len(sys.argv) < 3:
  err("usage: %s <cfile> <markfile>" % sys.argv[0])
sourcefilename = sys.argv[1]
markfilename = sys.argv[2]

with open(sourcefilename, "r") as f:
  source = f.read()

# //!Parselet (TPlusPlus UNARY_POSTFIX) (TMinusMinus UNARY_POSTFIX)
# //!PrefixParselet TPlus TMinus TStar TSlash
parseletp = re.compile(
  r'\n//\s*\!Parselet\s+(?P<m>(?:\([^\)]+\)[\s\r\n\/\/]*)+)\n\s*(?:static|)\s*Node\*\s*(?:nullable\s*|)(?P<fun>\w+)')
prefixparseletp = re.compile(
  r'\n//\s*\!PrefixParselet\s+([^\n]+)\n\s*(?:static|)\s*Node\*\s*(?:nullable\s*|)(\w+)')
splitspecs = re.compile(r'\)[\s\r\n\/\/]*\(')
splitsep = re.compile(r'[\s,]+')
parselets = dict()  # keyed by token, e.g. "TPlus"

for m in prefixparseletp.finditer(source):
  fun = m.group(2)
  for tok in splitsep.split(m.group(1)):
    struct_init = parselets.get(tok)
    if struct_init:
      err("duplicate parselet %s for token %s" % (fun, tok))
    parselets[tok] = [fun, "NULL", "MEMBER"]

for m in parseletp.finditer(source):
  md = m.groupdict()
  for s in splitspecs.split(md["m"]):
    tok, prec = splitsep.split(s.strip("()"), 1)
    fun = md["fun"]
    # print({ "tok": tok, "prec": prec, "fun": md["fun"] })
    struct_init = parselets.get(tok)
    if not struct_init:
      parselets[tok] = ["NULL", fun, prec]
    else:
      if struct_init[1] != "NULL":
        err("duplicate parselet %s for token %s" % (fun, tok))
      struct_init[1] = fun
      struct_init[2] = prec

# const Parselet parselets[TMax] = {
#   [TComment] = { PLComment, NULL, PREC_LOWEST },
# };
srcdir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
relscriptfile = os.path.relpath(os.path.abspath(__file__), srcdir)
output = [
  '// automatically generated by %s; do not edit' % relscriptfile,
]
output.append("static const Parselet parselets[TMax] = {")
for tok, struct_init in parselets.items():
  output.append("  [%s] = {%s, %s, PREC_%s}," % (tok, *struct_init))
output.append("};")
output = "\n".join(output)

startstr = '//PARSELET_MAP_BEGIN\n'
endstr   = '\n//PARSELET_MAP_END'
start = source.find(startstr)
end   = source.find(endstr, start)
if start == -1:
  err("can not find %r in %s" % (startstr, sourcefilename))
if end == -1:
  err("can not find %r in %s" % (endstr, sourcefilename))

source2 = source[:start + len(startstr)] + output + source[end:]

# write changes only if we modified the source
if source2 != source:
  print("write", sourcefilename)
  with open(sourcefilename, "w") as f:
    f.write(source2)
# write "marker" file for ninja/make
with open(markfilename, "w") as f:
  f.write("")
