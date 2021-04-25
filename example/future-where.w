# <expr> where <bindings>
#
# Similar to Haskell's "where"
# Similar to Rust's "where"
#
# Note: This may be a bad idea.
#

fun fmtSyntaxErrors(errors [Error]) {
  errors.map(e ->
    logger.warn("$severity in $file:$line:$col: $error$snippet") where {
      severity = if e.severity == nil "error" else e.severity
      line, col, snippet = switch e.loc {
        nil -> (0,0,"")
        Location(source, line, col) -> {
          line, col, switch source.IndexOfNth('\n', line - 1) {
            nil -> ""
            i   -> "\n" + source[i:i+1]
          }
        }
      }
    }
  )
}

fun fmtSyntaxErrors(errors [Error]) {
  errors.map(e -> {
    severity = if e.severity == nil "error" else e.severity
    line, col, snippet = switch e.loc {
      nil -> (0,0,"")
      Location(source, line, col) -> {
        line, col, switch source.IndexOfNth('\n', line - 1) {
          nil -> ""
          i   -> "\n" + source[i:i+1]
        }
      }
    }
    logger.warn("$severity in $file:$line:$col: $error$snippet")
  })
}

# Python-esque

fun fmtSyntaxErrors(errors [Error]):
  errors.map(e ->
    logger.warn("$severity in $file:$line:$col: $error$snippet") where:
      severity = if e.severity == nil "error" else e.severity
      line, col, snippet = switch e.loc:
        nil -> (0,0,"")
        Location(source, line, col) ->
          line, col, switch source.IndexOfNth('\n', line - 1):
            nil -> ""
            i   -> "\n" + source[i:i+1] )


fun fmtSyntaxErrors(errors [Error]):
  errors.map(e -> {
    severity = if e.severity == nil "error" else e.severity
    line, col, snippet = switch e.loc:
      nil -> (0,0,"")
      Location(source, line, col) ->
        line, col, switch source.IndexOfNth('\n', line - 1):
          nil -> ""
          i   -> "\n" + source[i:i+1]
    logger.warn("$severity in $file:$line:$col: $error$snippet")
  })
