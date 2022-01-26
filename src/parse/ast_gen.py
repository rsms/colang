#!/usr/bin/env python3
#
# This script parses the struct heirarchy in ast.h and generates code in ast.h & ast.c
#
import re, sys, os, os.path, pprint
from collections import OrderedDict

def prettyrepr(obj):
  return pprint.pformat(obj, indent=2, sort_dicts=False)

def err(msg):
  print(msg)
  sys.exit(1)

# "struct name { struct? parent;"
node_struct_re = re.compile(r'\n\s*struct\s+([^\s\n]+)\s*\{[\n\s]*(?:struct\s+|)([^\s]+)\s*;')

# parse CLI
if len(sys.argv) < 4:
  err("usage: %s <hfile> <cfile> <markfile>" % sys.argv[0])
hfilename = sys.argv[1]
cfilename = sys.argv[2]
markfilename = sys.argv[3]

# load input files
hsource = ''
with open(hfilename, "r") as f:
  hsource = f.read()

csource = ''
with open(cfilename, "r") as f:
  csource = f.read()

# find all node struct definitions
typemap = OrderedDict()
for m in node_struct_re.finditer(hsource):
  name = m.group(1)
  parent_name = m.group(2)

  m = typemap.setdefault(name, OrderedDict())
  parent_m = typemap.setdefault(parent_name, OrderedDict())
  parent_m[name] = m

# print("typemap:", prettyrepr(typemap))
Node = typemap["Node"]
# print(prettyrepr(Node))


def strip_node_suffix(name):
  if name.endswith('Node'):
    name = name[:len(name) - len('Node')]
  return name


def collect_leaf_names(leafnames_out, subtypes):
  for name, subtypes2 in subtypes.items():
    if len(subtypes2) == 0:
      leafnames_out.append(name)
    else:
      collect_leaf_names(leafnames_out, subtypes2)
leafnames = []  # matches enum order
collect_leaf_names(leafnames, Node)


def find_maxdepth(depth, subtypes):
  d = depth
  for subtypes2 in subtypes.values():
    d = find_maxdepth(depth + 1, subtypes2)
  return d
maxdepth = find_maxdepth(0, Node)


# find longest enum value name
def find_maxnamelen(a, b, name, subtypes):
  shortname = strip_node_suffix(name)
  if len(subtypes) > 0:
    name += '_END'
    shortname += '_END'
  a = max(a, len(name))
  b = max(b, len(shortname))
  for name, subtypes2 in subtypes.items():
    a2, b2 = find_maxnamelen(a, b, name, subtypes2)
    a = max(a, a2)
    b = max(b, b2)
  return a, b
maxnamelen, maxshortnamelen = find_maxnamelen(len('Node'), 0, 'Node', Node)


IND = '  '
def gen_NodeKind_enum(out, depth, i, subtypes):
  ind = IND * depth
  namelen = (maxdepth * len(IND) + maxshortnamelen +1) - len(ind) # +1 for 'N' prefix
  for name, subtypes2 in subtypes.items():
    shortname = strip_node_suffix(name)
    is_leaf = len(subtypes2) == 0
    if is_leaf:
      out.append(ind+'%-*s = %2d, // struct %s' % (namelen, 'N'+shortname, i, name))
      if leafnames[i] != name:
        err("unexpected name %r (leafnames out of order? found %r at leafnames[%d])" % (
          name, leafnames[i], i))
      i += 1
    else:
      out.append(ind+'%-*s = %2d,' % (namelen, 'N'+shortname+'_BEG', i))
    i = gen_NodeKind_enum(out, depth + 1, i, subtypes2)
    if not is_leaf:
      out.append(ind+'%-*s = %2d,' % (namelen, 'N'+shortname+'_END', i - 1))
  return i


def output_compact(output, input, maxcol, indent):
  buf = indent
  for w in input:
    if len(buf) + len(w) > maxcol and len(buf) > 0:
      output.append(buf[:len(buf)-1])
      buf = indent
    buf += w + ' '
  if len(buf) > 0:
    output.append(buf[:len(buf)-1])

outh = [] # ast.h
outc = [] # ast.c

# enum NodeKind
outh.append('enum NodeKind {')
gen_NodeKind_enum(outh, 1, 0, Node)
# outh.append('  %-*s = %d,' %
#   (((maxdepth-1) * len(IND) + maxnamelen +1), 'NodeKind_MAX', i - 1))
outh.append('} END_TYPED_ENUM(NodeKind)')
outh.append('')

# NodeKindName
outh.append('// NodeKindName returns a printable name. E.g. %s => "%s"' % (
  'N'+strip_node_suffix(leafnames[0]), strip_node_suffix(leafnames[0]) ))
outh.append('const char* NodeKindName(NodeKind);')
outh.append('')
outc.append('const char* NodeKindName(NodeKind k) {')
outc.append('  // kNodeNameTable[NodeKind] => const char* name')
outc.append('  static const char* const kNodeNameTable[%d] = {' % len(leafnames))
outtmp = ['"%s",' % strip_node_suffix(name) for name in leafnames]
outtmp[len(outtmp)-1] = outtmp[len(outtmp)-1][:len(outtmp[len(outtmp)-1])-1] # strip last ','
output_compact(outc, outtmp, 80, '    ')
outc.append('  };')
outc.append('  return k < %d ? kNodeNameTable[k] : "?";' % len(leafnames))
outc.append('}')
outc.append('')

# NodeKindIs*
outh.append('// bool NodeKindIs<kind>(NodeKind)')
for name, subtypes in typemap.items():
  if len(subtypes) == 0:
    continue # skip leafs
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  outh.append('#define NodeKindIs%s(nkind) ((int)(nkind)-N%s_BEG <= (int)N%s_END-N%s_BEG)' % (
    shortname, shortname, shortname, shortname))
outh.append('')

# NodeIs*
outh.append('// bool NodeIs<kind>(const Node*)')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    outh.append('#define NodeIs%s(n) ((n)->kind==N%s)' % (shortname, shortname))
  else:
    outh.append('#define NodeIs%s(n) NodeKindIs%s((n)->kind)' % (shortname, shortname))
outh.append('')

# NodeAssert*
outh.append('// void NodeAssert<kind>(const Node*)')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    outh.append(
      '#define NodeAssert%s(n) assertf((n)->kind==N%s,"%%d",(n)->kind)' % (
      shortname, shortname))
  else:
    outh.append(
      '#define NodeAssert%s(n) assertf(NodeKindIs%s((n)->kind),"%%d",(n)->kind)' % (
      shortname, shortname))
outh.append('')

# as_*
outh.append('// <type>* as_<type>(Node* n)')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  is_leaf = len(subtypes) == 0
  outh.append('#define as_%s(n) ({ NodeAssert%s(n); (%s*)(n); })' % (name, shortname, name))
  # outh.append('')
outh.append('')


# print("——— ast.h ———")
# print("\n".join(outh))
# print("——— ast.c ———")
# print("\n".join(outc))

srcdir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
relscriptfile = os.path.relpath(os.path.abspath(__file__), srcdir)


def patch_source_file(filename, source, generated_code):
  startstr = '//BEGIN GENERATED CODE'
  endstr   = '\n//END GENERATED CODE'
  start = source.find(startstr)
  end   = source.find(endstr, start)
  if start == -1:
    err("can not find %r in %s" % (startstr, filename))
  startend = source.find('\n', start)
  if end == -1:
    err("can not find %r in %s" % (endstr, filename))
  if startend == -1 or startend >= end:
    err("missing line terminator after %r in %s" % (startstr, filename))
  startstr += ' by %s\n' % (
    os.path.relpath(os.path.abspath(__file__), os.path.dirname(filename)))
  generated_code = '\n' + generated_code + "\n"
  source2 = source[:start] + startstr + generated_code + source[end:]
  # print(generated_code)
  # print(source2)
  # write changes if we modified the source
  if source2 != source:
    print("write", filename)
    with open(filename, "w") as f:
      f.write(source2)


patch_source_file(hfilename, hsource, "\n".join(outh).strip())
patch_source_file(cfilename, csource, "\n".join(outc).strip())

# write "marker" file for ninja/make
with open(markfilename, "w") as f:
  f.write("")
