// Co syntax highlighting for highlight.js
module.exports = function(hljs) {
  const LITERALS = [
    "true",
    "false",
    "nil"
  ]
  const BUILT_INS = [
    "copy",
    "panic",
    "print",
  ]
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
    "var",
  ]
  const TYPES = [
    "bool",
    "i8",  "i16", "i32", "i64", "int",
    "u8",  "u16", "u32", "u64", "uint",
    "f32", "f64",
    "byte",
    "str",
  ]

  const KEYWORDS = {
    keyword:  KWS,
    literal:  LITERALS,
    built_in: BUILT_INS,
    type:     TYPES,
  }

  const COMMENTS = [
    //hljs.COMMENT('//', '$'),
    hljs.COMMENT('/\\*', '\\*/'),
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
  ]

  const FUN_TYPE = {
    variants: [
      {
        className: 'funtype',
        begin: 'fun',
        returnBegin: true,
        contains: [] // defined later
      },
      {
        begin: /\(/,
        end: /\)/,
        contains: [] // defined later
      }
    ]
  }
  const FUN_TYPE2 = FUN_TYPE
  FUN_TYPE2.variants[1].contains = [ FUN_TYPE ]
  FUN_TYPE.variants[1].contains = [ FUN_TYPE2 ]
  const PARAMS = {
    className: 'params',
    begin: /\(/,
    end: /\)/,
    endsParent: true,
    keywords: KEYWORDS,
    relevance: 0,
    contains: [
      FUN_TYPE,
      ...COMMENTS
    ]
  }
  FUN_TYPE.variants[0].contains = [
    { className: 'type',
      begin: 'fun',
    },
    PARAMS,
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

      {
        className: 'function',
        beginKeywords: 'fun',
        end: '[(]|$',
        returnBegin: true,
        excludeEnd: true,
        keywords: KEYWORDS,
        relevance: 5,
        contains: [
          {
            begin: hljs.UNDERSCORE_IDENT_RE + '\\s*\\(',
            returnBegin: true,
            relevance: 0,
            contains: [ hljs.UNDERSCORE_TITLE_MODE ]
          },
          {
            className: 'type',
            begin: /</,
            end: />/,
            keywords: 'reified',
            relevance: 0
          },
          PARAMS,
          ...COMMENTS
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
