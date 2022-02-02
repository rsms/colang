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
  if depth == 1:
    out.append(ind+'%-*s = %2d,' % (namelen, 'NodeKind_MAX', i - 1))
  return i


def output_compact(output, input, maxcol, indent, lineend=''):
  buf = ""
  for w in input:
    if len(buf) + len(w) > maxcol and len(buf) > 0:
      output.append(buf[:len(buf)-1] + lineend)
      buf = indent
    buf += w + ' '
  if len(buf) > 0:
    output.append(buf[:len(buf)-1])


def output_compact_macro(output, input, indent=1):
  line1idx = len(output)
  output_compact(output, input, 88, '  ' * indent, ' \\')
  output[line1idx] = ('  ' * (indent - 1)) + output[line1idx].strip()


def structname(nodename, subtypes):
  if len(subtypes) == 0 or nodename in ('Node', 'Stmt', 'Expr', 'Type'):
    return nodename
  return 'struct '+nodename


# ---------------------------------------------------------------------------------------

outh = [] # ast.h
outc = [] # ast.c

# enum NodeKind
outh.append('enum NodeKind {')
nodekind_max = gen_NodeKind_enum(outh, 1, 0, Node) - 1
outh.append('} END_TYPED_ENUM(NodeKind)')
outh.append('')

# NodeKindName
outh.append('// NodeKindName returns a printable name. E.g. %s => "%s"' % (
  'N'+strip_node_suffix(leafnames[0]), strip_node_suffix(leafnames[0]) ))
outh.append('const char* NodeKindName(NodeKind);')
outh.append('')
outc.append('const char* NodeKindName(NodeKind k) {')
outc.append('  // kNodeNameTable[NodeKind] => const char* name')
outc.append('  static const char* const kNodeNameTable[NodeKind_MAX+2] = {')
outtmp = ['"%s",' % strip_node_suffix(name) for name in leafnames]
outtmp.append('"?"')
output_compact(outc, outtmp, 80, '    ')
outc.append('  };')
outc.append('  return kNodeNameTable[MIN(NodeKind_MAX+2,k)];')
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
outh.append('#ifdef DEBUG')
outh.append('#define _assert_is1(NAME,n) ({ \\')
outh.append('  NodeKind nk__ = assertnotnull(n)->kind; \\')
outh.append('  assertf(NodeKindIs##NAME(nk__), "expected N%s; got N%s #%d", \\')
outh.append('          #NAME, NodeKindName(nk__), nk__); \\')
outh.append('})')
outh.append('#else')
outh.append('#define _assert_is1(NAME,n) ((void)0)')
outh.append('#endif')
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
# inspect "Node*" at runtime
tmp += [
  'const Node*: ( is_Type(n) ? (const Type*)kType_type :',
  ' is_Expr(n) ? (const Type*)((Expr*)(n))->type : NULL ),',
  'Node*:( is_Type(n) ? kType_type : is_Expr(n) ? ((Expr*)(n))->type : NULL))',
]
output_compact_macro(outh, tmp)
outh.append('')


# union NodeUnion
outh.append('union NodeUnion {')
outtmp = ['%s _%d;' % (leafnames[i], i) for i in range(0, len(leafnames))]
output_compact(outh, outtmp, 80, '  ')
outh.append('};')
outh.append('')

# ASTVisitorFuns
ftable_size = len(leafnames) + 2
outh.append("""
typedef struct ASTVisitor     ASTVisitor;
typedef struct ASTVisitorFuns ASTVisitorFuns;
typedef int(*ASTVisitorFun)(ASTVisitor*, const Node*);
struct ASTVisitor {
  ASTVisitorFun ftable[%d];
};
void ASTVisitorInit(ASTVisitor*, const ASTVisitorFuns*);
""".strip() % ftable_size)

outh.append('// error ASTVisit(ASTVisitor* v, const NODE_TYPE* n)')
tmp = []
tmp.append('#define ASTVisit(v, n) _Generic((n),')
for name in leafnames:
  shortname = strip_node_suffix(name)
  tmp.append('const %s*: (v)->ftable[N%s]((v),(const Node*)(n)),' % (name,shortname))
  tmp.append('%s*: (v)->ftable[N%s]((v),(const Node*)(n)),' % (name,shortname))
for name, subtypes in typemap.items():
  if len(subtypes) > 0:
    stname = structname(name, subtypes)
    tmp.append('const %s*: (v)->ftable[(n)->kind]((v),(const Node*)(n)),' % stname)
    tmp.append('%s*: (v)->ftable[(n)->kind]((v),(const Node*)(n)),' % stname)
# tmp.append('default: v->ftable[MIN(%d,(n)->kind)]((v),(const Node*)(n)),' % (
#   ftable_size - 1))
tmp[-1] = tmp[-1][:-1] + ')' # replace last ',' with ')'
output_compact_macro(outh, tmp)
outh.append('')

outh.append('struct ASTVisitorFuns {')
for name in leafnames:
  shortname = strip_node_suffix(name)
  outh.append('  error(*nullable %s)(ASTVisitor*, const %s*);' % (shortname, name))

outh.append('')
outh.append("  // class-level visitors called for nodes without specific visitors")
for name, subtypes in typemap.items():
  if len(subtypes) == 0: continue
  shortname = strip_node_suffix(name)
  if shortname == '': continue
  stname = structname(name, subtypes)
  outh.append('  error(*nullable %s)(ASTVisitor*, const %s*);' % (shortname, stname))
outh.append('')
outh.append("  // catch-all fallback visitor")
outh.append('  error(*nullable Node)(ASTVisitor*, const Node*);')
outh.append('};')
outh.append('')

outc.append('static error ASTVisitorNoop(ASTVisitor* v, const Node* n) { return 0; }')
outc.append('')
outc.append('void ASTVisitorInit(ASTVisitor* v, const ASTVisitorFuns* f) {')
outc.append('  ASTVisitorFun dft = (f->Node ? f->Node : &ASTVisitorNoop), dft1 = dft;')
outc.append('  // populate v->ftable')

def visit(out, name, subtypes, islast=False):
  shortname = strip_node_suffix(name)
  if shortname != '':
    if len(subtypes) > 0:
      out.append('  // begin %s' % shortname)
      out.append('  if (f->%s) { dft1 = dft; dft = ((ASTVisitorFun)f->%s); }' % (
        shortname, shortname))
    else:
      outc.append('  v->ftable[N%s] = f->%s ? ((ASTVisitorFun)f->%s) : dft;' % (
        shortname, shortname, shortname))

  i = 0
  lasti = len(subtypes) - 1
  for name2, subtypes2 in subtypes.items():
    visit(out, name2, subtypes2, islast=(shortname=='' and i == lasti))
    i += 1

  if len(subtypes) > 0 and shortname != '':
    if islast:
      out.append('  // end %s' % shortname)
    else:
      out.append('  dft = dft1; // end %s' % shortname)

visit(outc, 'Node', Node)
outc.append('}')
outc.append('')

# static error ASTConstVisitorInit(ASTConstVisitor* v, const ASTConstVisitorFuns* f) {
#   v->ftable[NBad]        = f->Bad        ? ((ASTConstVisitorFun)f->Bad)        : noop;
#   v->ftable[NField]      = f->Field      ? ((ASTConstVisitorFun)f->Field)      : noop;
#   v->ftable[NPkg]        = f->Pkg        ? ((ASTConstVisitorFun)f->Pkg)        : noop;
#   v->ftable[NFile]       = f->File       ? ((ASTConstVisitorFun)f->File)       : noop;
#   v->ftable[NComment]    = f->Comment    ? ((ASTConstVisitorFun)f->Comment)    : noop;
#   v->ftable[NNil]        = f->Nil        ? ((ASTConstVisitorFun)f->Nil)        : noop;
#   v->ftable[NBoolLit]    = f->BoolLit    ? ((ASTConstVisitorFun)f->BoolLit)    : noop;
#   v->ftable[NIntLit]     = f->IntLit     ? ((ASTConstVisitorFun)f->IntLit)     : noop;
#   v->ftable[NFloatLit]   = f->FloatLit   ? ((ASTConstVisitorFun)f->FloatLit)   : noop;
#   v->ftable[NStrLit]     = f->StrLit     ? ((ASTConstVisitorFun)f->StrLit)     : noop;
#   v->ftable[NId]         = f->Id         ? ((ASTConstVisitorFun)f->Id)         : noop;
#   v->ftable[NBinOp]      = f->BinOp      ? ((ASTConstVisitorFun)f->BinOp)      : noop;
#   v->ftable[NPrefixOp]   = f->PrefixOp   ? ((ASTConstVisitorFun)f->PrefixOp)   : noop;
#   v->ftable[NPostfixOp]  = f->PostfixOp  ? ((ASTConstVisitorFun)f->PostfixOp)  : noop;
#   v->ftable[NReturn]     = f->Return     ? ((ASTConstVisitorFun)f->Return)     : noop;
#   v->ftable[NAssign]     = f->Assign     ? ((ASTConstVisitorFun)f->Assign)     : noop;
#   v->ftable[NTuple]      = f->Tuple      ? ((ASTConstVisitorFun)f->Tuple)      : noop;
#   v->ftable[NArray]      = f->Array      ? ((ASTConstVisitorFun)f->Array)      : noop;
#   v->ftable[NBlock]      = f->Block      ? ((ASTConstVisitorFun)f->Block)      : noop;
#   v->ftable[NFun]        = f->Fun        ? ((ASTConstVisitorFun)f->Fun)        : noop;
#   v->ftable[NMacro]      = f->Macro      ? ((ASTConstVisitorFun)f->Macro)      : noop;
#   v->ftable[NCall]       = f->Call       ? ((ASTConstVisitorFun)f->Call)       : noop;
#   v->ftable[NTypeCast]   = f->TypeCast   ? ((ASTConstVisitorFun)f->TypeCast)   : noop;
#   v->ftable[NVar]        = f->Var        ? ((ASTConstVisitorFun)f->Var)        : noop;
#   v->ftable[NRef]        = f->Ref        ? ((ASTConstVisitorFun)f->Ref)        : noop;
#   v->ftable[NNamedArg]   = f->NamedArg   ? ((ASTConstVisitorFun)f->NamedArg)   : noop;
#   v->ftable[NSelector]   = f->Selector   ? ((ASTConstVisitorFun)f->Selector)   : noop;
#   v->ftable[NIndex]      = f->Index      ? ((ASTConstVisitorFun)f->Index)      : noop;
#   v->ftable[NSlice]      = f->Slice      ? ((ASTConstVisitorFun)f->Slice)      : noop;
#   v->ftable[NIf]         = f->If         ? ((ASTConstVisitorFun)f->If)         : noop;
#   v->ftable[NTypeType]   = f->TypeType   ? ((ASTConstVisitorFun)f->TypeType)   : noop;
#   v->ftable[NNamedType]  = f->NamedType  ? ((ASTConstVisitorFun)f->NamedType)  : noop;
#   v->ftable[NAliasType]  = f->AliasType  ? ((ASTConstVisitorFun)f->AliasType)  : noop;
#   v->ftable[NRefType]    = f->RefType    ? ((ASTConstVisitorFun)f->RefType)    : noop;
#   v->ftable[NBasicType]  = f->BasicType  ? ((ASTConstVisitorFun)f->BasicType)  : noop;
#   v->ftable[NArrayType]  = f->ArrayType  ? ((ASTConstVisitorFun)f->ArrayType)  : noop;
#   v->ftable[NTupleType]  = f->TupleType  ? ((ASTConstVisitorFun)f->TupleType)  : noop;
#   v->ftable[NStructType] = f->StructType ? ((ASTConstVisitorFun)f->StructType) : noop;
#   v->ftable[NFunType]    = f->FunType    ? ((ASTConstVisitorFun)f->FunType)    : noop;
#   v->ftable[NodeKind_MAX+1] = noop;
#   return 0;
# }


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
  else:
    print("skip", filename)


patch_source_file(hfilename, hsource, "\n".join(outh).strip())
patch_source_file(cfilename, csource, "\n".join(outc).strip())

# write "marker" file for ninja/make
with open(markfilename, "w") as f:
  f.write("")
