// Co syntax highlighting for highlight.js
module.exports = function(hljs) {
  const LITERALS = [
    "true",
    "false",
    "nil"
  ];
  const BUILT_INS = [
    "copy",
    "panic",
    "print",
  ];
  const KWS = [
    "auto",
    "break",
    "case",
    "const",
    "continue",
    "default",
    "else",
    "fallthrough",
    "for",
    "fun",
    "goto",
    "if",
    "import",
    "interface",
    "mut",
    "return",
    "select",
    "struct",
    "switch",
    "type",
  ];
  const TYPES = [
    "bool",
    "i8",  "i16", "i32", "i64", "int",
    "u8",  "u16", "u32", "u64", "uint",
    "f32", "f64",
    "byte",
    "str",
  ];
  const KEYWORDS = {
    keyword:  KWS,
    literal:  LITERALS,
    built_in: BUILT_INS,
    type:     TYPES,
  };
  const COMMENTS = [
    //hljs.COMMENT('//', '$'),
    hljs.COMMENT('/\\*', '\\*/'),
  ]
  return {
    name: 'Co',
    aliases: [],
    keywords: KEYWORDS,
    illegal: '</',
    contains: [
      hljs.SHEBANG({ binary: "co" }),

      { className: 'comment',
        begin: '//',
        end: '$',
        contains: [
          {
            className: 'errormsg',
            begin: /error:/,
            end: /$/,
          },
        ]
      },

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

      { className: 'typedef',
        beginKeywords: 'type',
        end: '\\s*$',
        excludeEnd: true,
        contains: [
          hljs.TITLE_MODE,
          ...COMMENTS,
        ]
      },

    ]
  };
}
