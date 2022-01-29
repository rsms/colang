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


def output_compact(output, input, maxcol, indent, lineend=''):
  buf = indent
  for w in input:
    if len(buf) + len(w) > maxcol and len(buf) > 0:
      output.append(buf[:len(buf)-1] + lineend)
      buf = indent
    buf += w + ' '
  if len(buf) > 0:
    output.append(buf[:len(buf)-1])



def structname(nodename, subtypes):
  if len(subtypes) == 0 or nodename in ('Node', 'Stmt', 'Expr', 'Type'):
    return nodename
  return 'struct '+nodename


# ---------------------------------------------------------------------------------------

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
output_compact(outc, outtmp, 80, '    ')
outc.append('  };')
outc.append('  return k < %d ? kNodeNameTable[k] : "?";' % len(leafnames))
outc.append('}')
outc.append('')

# node typedefs
outh += ['typedef struct %s %s;' % (name, name) for name in leafnames]
outh.append('')

# NodeKindIs*
outh.append('// bool NodeKindIs<kind>(NodeKind)')
for name, subtypes in typemap.items():
  if len(subtypes) == 0:
    continue # skip leafs
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  outh.append('#define NodeKindIs%s(k) (N%s_BEG <= (k) && (k) <= N%s_END)' % (
    shortname, shortname, shortname))
outh.append('')

# is_*
outh.append('// bool is_<kind>(const Node*)')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    outh.append('#define is_%s(n) ((n)->kind==N%s)' % (name, shortname))
  else:
    outh.append('#define is_%s(n) NodeKindIs%s((n)->kind)' % (name, shortname))
outh.append('')

# assert_is_*
outh.append('// void assert_is_<kind>(const Node*)')
outh.append('#define _assert_is1(NAME,n) ({ \\')
outh.append('  NodeKind nk__ = assertnotnull(n)->kind; \\')
outh.append('  assertf(NodeKindIs##NAME(nk__), "expected N%s; got N%s #%d", \\')
outh.append('          #NAME, NodeKindName(nk__), nk__); \\')
outh.append('})')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    outh.append(
      '#define assert_is_%s(n) asserteq(assertnotnull(n)->kind,N%s)' % (
      name, shortname))
  else:
    outh.append(
      '#define assert_is_%s(n) _assert_is1(%s,(n))' % (
      name, shortname))
outh.append('')

def output_compact_macro(output, input):
  line1idx = len(output)
  output_compact(output, input, 88, '  ', ' \\')
  output[line1idx] = output[line1idx].strip()

# as_*
outh.append('// <type>* as_<type>(Node* n)')
outh.append('// const <type>* as_<type>(const Node* n)')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    # has typedef so no need for "struct"
    outh.append('#define as_%s(n) ({ assert_is_%s(n); (%s*)(n); })' % (
      name, name, name))

def gen_as_TYPE(out, name, subtypes):
  stname = structname(name, subtypes)

  def visit(out, name, subtypes):
    for name2, subtypes2 in subtypes.items():
      visit(out, name2, subtypes2)
    out.append('const %s*:(const %s*)(n),' % (structname(name, subtypes), stname))
    out.append('%s*:(%s*)(n),' % (structname(name, subtypes), stname))

  tmp = []
  tmp.append('#define as_%s(n) _Generic((n),' % (name))
  visit(tmp, name, subtypes)

  if strip_node_suffix(name) != '':
    # tmp.append('default: ({ assert_is_%s(n); (%s*)(n); }))' % (name, stname))
    tmp.append('const Node*: ({ assert_is_%s(n); (const %s*)(n); }),' % (name, stname))
    tmp.append('Node*: ({ assert_is_%s(n); (%s*)(n); }))' % (name, stname))

  tmp[-1] = tmp[-1][:-1] + ')' # replace last ',' with ')'
  output_compact_macro(out, tmp)
  out.append('')

def gen_as_TYPE_all(out, subtypes):
  for name, subtypes2 in subtypes.items():
    if subtypes2:
      gen_as_TYPE(out, name, subtypes2)
      gen_as_TYPE_all(out, subtypes2)

# as_Node
gen_as_TYPE(outh, 'Node', Node)
gen_as_TYPE_all(outh, Node)


# maybe_*
outh.append('// <type>* nullable maybe_<type>(Node* n)')
outh.append('// const <type>* nullable maybe_<type>(const Node* n)')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    outh.append('#define maybe_%s(n) (is_%s(n)?(%s*)(n):NULL)' % (name, name, name))
  else:
    outh.append('#define maybe_%s(n) (is_%s(n)?as_%s(n):NULL)' % (name, name, name))
outh.append('')


# TypeOfNode
visit_typeof_seen = set()
def visit_typeof(out, name, subtypes, action, constaction):
  for name2, subtypes2 in subtypes.items():
    visit_typeof(out, name2, subtypes2, action, constaction)
  out.append('const %s*:%s,' % (structname(name, subtypes), constaction))
  out.append('%s*:%s,' % (structname(name, subtypes), action))
  visit_typeof_seen.add(name)

outh.append('// Type* nullable TypeOfNode(Node* n)')
outh.append('// Type* TypeOfNode(Type* n)')
tmp = []
tmp.append('#define TypeOfNode(n) _Generic((n),')
visit_typeof(tmp, 'Type', typemap['Type'], 'kType_type', '(const Type*)kType_type')
visit_typeof(tmp, 'Expr', typemap['Expr'],
  '((Expr*)(n))->type', '(const Type*)((Expr*)(n))->type')

# remaining node kinds have no type
for name, subtypes in typemap.items():
  if name != 'Node' and name not in visit_typeof_seen:
    tmp.append('%s*:NULL,' % structname(name, subtypes))

# Node: inspect at runtime
tmp += [
  'const Node*: ( is_Type(n) ? (const Type*)kType_type :',
  ' is_Expr(n) ? (const Type*)((Expr*)(n))->type : NULL ),',
  'Node*:( is_Type(n) ? kType_type : is_Expr(n) ? ((Expr*)(n))->type : NULL))',
]

# tmp.append('Node*:(is_Type(n) ? kType_type : is_Expr(n) ? ((Expr*)(n))->type : NULL))')

output_compact_macro(outh, tmp)
outh.append('')


# union NodeUnion
outh.append('union NodeUnion {')
outtmp = ['%s _%d;' % (leafnames[i], i) for i in range(0, len(leafnames))]
output_compact(outh, outtmp, 80, '  ')
outh.append('};')
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
