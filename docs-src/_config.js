module.exports = ({
  site,     // mutable object describing the site
  hljs,     // HighlightJS module (NPM: highlight.js)
  markdown, // Markdown module (NPM: markdown-wasm)
  glob,     // glob function (NPM: miniglob)
}) => {
  // called when program starts
  site.outdir = "../docs"
  // console.log(site)

  // configure highlight.js
  hljs.registerLanguage("co",     require("./_hl/co"))
  hljs.registerLanguage("asmarm", require("./_hl/armasm"))
  hljs.registerLanguage("asmx86", require("./_hl/x86asm"))
  hljs.registerLanguage("wast",   require("./_hl/wasm"))
  hljs.registerLanguage("llvm",   require("./_hl/llvm"))

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
