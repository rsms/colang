// Co syntax highlighting for highlight.js
module.exports = function(hljs) {
  const LITERALS = [
    "true",
    "false",
    "nil"
  ];
  const BUILT_INS = [
    "append",
    "cap",
    "copy",
    "len",
    "make",
    "panic",
    "print",
  ];
  const KWS = [
    "auto",
    "break",
    "default",
    "fun",
    "interface",
    "select",
    "case",
    "struct",
    "else",
    "goto",
    "switch",
    "const",
    "fallthrough",
    "if",
    "type",
    "continue",
    "for",
    "import",
    "return",
    "var",
    "bool",
    "i8",  "i16", "i32", "i64", "isize", "int",
    "u8",  "u16", "u32", "u64", "usize", "uint",
    "f32", "f64",
    "byte",
    "rune",
    "str",
  ];
  const KEYWORDS = {
    keyword: KWS,
    literal: LITERALS,
    built_in: BUILT_INS
  };
  const COMMENTS = [
    hljs.COMMENT('#', '$'),
    hljs.COMMENT('#\\*', '\\*#'),
  ]
  return {
    name: 'Co',
    aliases: [],
    keywords: KEYWORDS,
    illegal: '</',
    contains: [
      hljs.SHEBANG({ binary: "co" }),

      ...COMMENTS,

      { className: 'string',
        variants: [
          hljs.QUOTE_STRING_MODE, // "str"
          hljs.APOS_STRING_MODE,  // 'c'
        ]
      },

      { className: 'number',
        variants: [
          { begin: hljs.C_NUMBER_RE + '[i]', relevance: 1 },
          hljs.C_NUMBER_MODE
        ]
      },

      { className: 'function',
        beginKeywords: 'fun',
        end: '\\s*(\\{|$)',
        excludeEnd: true,
        contains: [
          hljs.TITLE_MODE,
          {
            className: 'params',
            begin: /\(/,
            end: /\)/,
            keywords: KEYWORDS,
            illegal: /["']/,
            contains: [
              ...COMMENTS,
            ],
          },
          ...COMMENTS,
        ]
      },

    ]
  };
}
