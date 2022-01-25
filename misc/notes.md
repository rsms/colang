## Misc Skia stuff

- skcms is a color space management lib, logically part of skia but separately developed.
  Find the header in `skia/include/third_party/skcms/skcms.h`
- [sksl][deps/skia/src/sksl/README] is Skia's shader IR format

## Lua

- [WASM project](https://github.com/vvanders/wasm_lua) (build is failing)
- [WASM build guide](https://medium.com/@imwithye/port-lua-to-web-environment-using-webassembly-3144a8ac000e)
- [Lua bytecode tools](https://github.com/rochus-keller/LjTools)
- Lua implementations in JavaScript:
  - [fengari (vm)](https://github.com/fengari-lua/fengari)
  - [moonshine (vm)](https://github.com/gamesys/moonshine)
  - [brozula (vm)](https://github.com/creationix/brozula)
  - [starlight (transpiler)](https://github.com/paulcuth/starlight)

luajit can [dump and load bytecode](https://luajit.org/running.html):

```sh
$ (cd deps/luajit/src && ./luajit -ble "print('hello world')")
0001    GGET     0   0      ; "print"
0002    KSTR     2   1      ; "hello world"
0003    CALL     0   1   2
0004    RET0     0   1
```

Lua WASM compile with emscripten is essentially:

```sh
$ cd lua
$ make generic CC='emcc -s WASM=1' AR='emar rcu' RANLIB='emranlib'
$ ls -l src/*.wasm
-rwxr-xr-x  1 rsms  staff  248339 Jan 22 17:45 src/lua.wasm
-rwxr-xr-x  1 rsms  staff  157332 Jan 22 17:45 src/luac.wasm
$ cat << EOF > index.html
<script>
var Module = {
  print: (text) => console.log("stdout: " + text),
  printErr: (text) => console.error("stderr: " + text),
  onRuntimeInitialized: () => {
    const lua_main = Module.cwrap('lua_main', 'number', ['string']);
    lua_main("print('hello world'");
  }
};
</script>
<script src="./lua.js">
EOF
```

## Skia key C++ classes

- [`SkAutoCanvasRestore`][SkAutoCanvasRestore] - Canvas save stack manager
- [`SkBitmap`][SkBitmap] - two-dimensional raster pixel array
- [`SkBlendMode`][SkBlendMode] - pixel color arithmetic
- [`SkCanvas`][SkCanvas] - drawing context
- [`SkColor`][SkColor] - color encoding using integer numbers
- [`SkFont`][SkFont] - text style and typeface
- [`SkImage`][SkImage] - two dimensional array of pixels to draw
- [`SkImageInfo`][SkImageInfo] - pixel dimensions and characteristics
- [`SkIPoint`][SkIPoint] - two integer coordinates
- [`SkIRect`][SkIRect] - integer rectangle
- [`SkMatrix`][SkMatrix] - 3x3 transformation matrix
- [`SkPaint`][SkPaint] - color, stroke, font, effects
- [`SkPath`][SkPath] - sequence of connected lines and curves
- [`SkPicture`][SkPicture] - sequence of drawing commands
- [`SkPixmap`][SkPixmap] - pixel map: image info and pixel address
- [`SkPoint`][SkPoint] - two floating point coordinates
- [`SkRRect`][SkRRect] - floating point rounded rectangle
- [`SkRect`][SkRect] - floating point rectangle
- [`SkRegion`][SkRegion] - compressed clipping mask
- [`SkSurface`][SkSurface] - drawing destination
- [`SkTextBlob`][SkTextBlob] - runs of glyphs
- [`SkTextBlobBuilder`][SkTextBlobBuilder] - constructor for runs of glyphs

<!-- URLs: -->
[SkAutoCanvasRestore][https://api.skia.org/classSkAutoCanvasRestore.html#details]
[SkBitmap][https://api.skia.org/classSkBitmap.html#details]
[SkBlendMode][https://api.skia.org/SkBlendMode_8h.html]
[SkCanvas][https://api.skia.org/classSkCanvas.html#details]
[SkColor][https://api.skia.org/SkColor_8h.html]
[SkFont][https://api.skia.org/classSkFont.html#details]
[SkImage][https://api.skia.org/classSkImage.html#details]
[SkImageInfo][https://api.skia.org/structSkImageInfo.html#details]
[SkIPoint][https://api.skia.org/structSkIPoint.html#details]
[SkIRect][https://api.skia.org/structSkIRect.html#details]
[SkMatrix][https://api.skia.org/classSkMatrix.html#details]
[SkPaint][https://api.skia.org/classSkPaint.html#details]
[SkPath][https://api.skia.org/classSkPath.html#details]
[SkPicture][https://api.skia.org/classSkPicture.html#details]
[SkPixmap][https://api.skia.org/classSkPixmap.html#details]
[SkPoint][https://api.skia.org/structSkPoint.html#details]
[SkRRect][https://api.skia.org/classSkRRect.html#details]
[SkRect][https://api.skia.org/structSkRect.html#details]
[SkRegion][https://api.skia.org/classSkRegion.html#details]
[SkSurface][https://api.skia.org/classSkSurface.html#details]
[SkTextBlob][https://api.skia.org/classSkTextBlob.html#details]
[SkTextBlobBuilder][https://api.skia.org/classSkTextBlobBuilder.html#details]

