module.exports = ({
  site,     // mutable object describing the site
  hljs,     // HighlightJS module (NPM: highlight.js)
  markdown, // Markdown module (NPM: markdown-wasm)
  glob,     // glob function (NPM: miniglob)
}) => {
  // called when program starts
  // console.log(site)
  site.outdir = "../../docs"

  // configure highlight.js
  hljs.registerLanguage("co", require("../../misc/highlight.js_co.js"))
  // hljs.registerLanguage("asmarm",  require("highlight.js/lib/languages/armasm"))
  // hljs.registerLanguage("asmavr",  require("highlight.js/lib/languages/avrasm"))
  // hljs.registerLanguage("asmmips", require("highlight.js/lib/languages/mipsasm"))
  // hljs.registerLanguage("asmx86",  require("highlight.js/lib/languages/x86asm"))

  // these optional callbacks can return a Promise to cause build process to wait
  //
  // site.onBeforeBuild = (files) => {
  //   // called when .pages has been populated
  //   // console.log("onBeforeBuild pages:", site.pages)
  //   // console.log("onBeforeBuild files:", files)
  // }

  // site.onAfterBuild = (files) => {
  //   // called after site has been generated
  //   // console.log("onAfterBuild")
  // }
}
