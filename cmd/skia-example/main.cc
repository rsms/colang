/*
* Copyright 2017 Google Inc.
*
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/
#include <stdio.h>

#include "tools/sk_app/Application.h"
#include "tools/sk_app/Window.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkTime.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkGradientShader.h"
#include "include/effects/SkPerlinNoiseShader.h"
#include "include/effects/SkRuntimeEffect.h"

#ifdef DEBUG
  #define dlog(format, ...) \
    fprintf(stderr, "D " format " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__)
  #define errlog(format, ...) \
    fprintf(stderr, "E " format " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__)
#else
  #define dlog(...) do{}while(0)
  #define errlog(format, ...) fprintf(stderr, "E " format "\n", ##__VA_ARGS__)
#endif

using namespace sk_app;
using float4 = std::array<float, 4>;

class App : public Application, Window::Layer {
public:
    App(int argc, char** argv, void* platformData);
    ~App() override;

    void onIdle() override;

    void onBackendCreated() override;
    void onAttach(Window* window) override;
    void onPaint(SkSurface*) override;
    bool onChar(SkUnichar c, skui::ModifierKey modifiers) override;

    void buildTestShader();

private:
    void updateTitle();

    Window* fWindow;
    Window::BackendType fBackendType;
    double fTimeBase = 0;

    SkScalar fRotationAngle;

    SkRuntimeShaderBuilder* fShaderBuilder = nullptr;
    sk_sp<SkShader> fTestShader;
    //sk_sp<SkTypeface> fInterTypeface;

    double fDebugMessageTime = 0;
    uint32_t fDebugFrameCount = 0;
    char fDebugMessageBuf[64];
    int fDebugMessageLen = 0;
    SkScalar fDebugMessageAdvanceWidth;

    SkFont fFontInterMedium24;
};

Application* Application::Create(int argc, char** argv, void* platformData) {
  return new App(argc, argv, platformData);
}

App::App(int argc, char** argv, void* platformData)
    : fBackendType(Window::kMetal_BackendType)
    // : fBackendType(Window::kNativeGL_BackendType)
    , fRotationAngle(0) {
  SkGraphics::Init();

  // build shader early before there's a backend to make sure compiling SkSL
  // is not backend-dependent.
  buildTestShader();

  fWindow = Window::CreateNativeWindow(platformData);
  fWindow->setRequestedDisplayParams(DisplayParams());

  // register callbacks
  fWindow->pushLayer(this);

  fWindow->attach(fBackendType);

  auto interMedium = SkTypeface::MakeFromFile("misc/Inter-Medium.otf");
  fFontInterMedium24.setSubpixel(true); // sub-pixel positioning, not SPAA
  fFontInterMedium24.setHinting(SkFontHinting::kNone);
  if (interMedium) {
    fFontInterMedium24.setTypeface(interMedium);
  }
  fFontInterMedium24.setSize(24);
}

App::~App() {
  fWindow->detach();
  delete fWindow;
}

void App::updateTitle() {
  if (!fWindow /*|| fWindow->sampleCount() <= 1*/ ) {
    return;
  }
  SkString title("Skia");
  fWindow->setTitle(title.c_str());
}

void App::onAttach(Window* window) {
  dlog("App::onAttach");
}

void App::buildTestShader() {
  // compile an SkSL shader
  // const char* sksl_src = R"_EOF_(
  //   half4 main(float2 p) {
  //     return half4(half2(p - 0.5), 0, 1);
  //   }
  // )_EOF_";
  // const char* sksl_src = R"_EOF_(
  //   uniform float4 gColor;
  //   half4 main() {
  //     return half4(gColor);
  //   }
  // )_EOF_";
  const char* sksl_src = R"(
uniform float uTime;

vec3 mod289(vec3 x) {
  return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec2 mod289(vec2 x) {
  return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec3 permute(vec3 x) {
  return mod289(((x*34.0)+1.0)*x);
}

// simplex noise
float snoise(vec2 v) {
  const vec4 C = vec4(0.211324865405187,  // (3.0-sqrt(3.0))/6.0
                      0.366025403784439,  // 0.5*(sqrt(3.0)-1.0)
                     -0.577350269189626,  // -1.0 + 2.0 * C.x
                      0.024390243902439); // 1.0 / 41.0
  // First corner
  vec2 i  = floor(v + dot(v, C.yy) );
  vec2 x0 = v -   i + dot(i, C.xx);

  // Other corners
  vec2 i1;
  //i1.x = step( x0.y, x0.x ); // x0.x > x0.y ? 1.0 : 0.0
  //i1.y = 1.0 - i1.x;
  i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
  // x0 = x0 - 0.0 + 0.0 * C.xx ;
  // x1 = x0 - i1 + 1.0 * C.xx ;
  // x2 = x0 - 1.0 + 2.0 * C.xx ;
  vec4 x12 = x0.xyxy + C.xxzz;
  x12.xy -= i1;

  // Permutations
  i = mod289(i); // Avoid truncation effects in permutation
  vec3 p = permute( permute( i.y + vec3(0.0, i1.y, 1.0 ))
    + i.x + vec3(0.0, i1.x, 1.0 ));

  vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
  m = m*m ;
  m = m*m ;

  // Gradients: 41 points uniformly over a line, mapped onto a diamond.
  // The ring size 17*17 = 289 is close to a multiple of 41 (41*7 = 287)

  vec3 x = 2.0 * fract(p * C.www) - 1.0;
  vec3 h = abs(x) - 0.5;
  vec3 ox = floor(x + 0.5);
  vec3 a0 = x - ox;

  // Normalise gradients implicitly by scaling m
  // Approximation of: m *= inversesqrt( a0*a0 + h*h );
  m *= 1.79284291400159 - 0.85373472095314 * ( a0*a0 + h*h );

  // Compute final noise value at P
  vec3 g;
  g.x  = a0.x  * x0.x  + h.x  * x0.y;
  g.yz = a0.yz * x12.xz + h.yz * x12.yw;
  return 130.0 * dot(m, g);
}

//float random(float2 st) {
//  return fract(sin(dot(st.xy , float2(12.9898,78.233))) * 43758.5453);
//}

float4 main(float2 fragCoord) {
  float scale = 0.002;
  // return half4(scale * (half2(fragCoord.xy) - 0.5), abs(sin(uTime * 0.5)), 1);

  // float3 color = float3(random( ipos ));

  float2 st = fragCoord * 0.0013;
  //float2 ipos = floor(st); // get the integer coords
  //float2 fpos = fract(st); // get the fractional coords
  //float3 noise = float3(random( ipos ));

  return float4(
    float3(
      scale * ((fragCoord.x) - 0.5) * abs(sin(uTime * 0.1)),
      scale * ((fragCoord.y) - 0.5) * abs(cos(uTime * 0.4)),
      abs(sin(uTime * 0.5))
    ) * (0.8 + snoise(st + abs(uTime * 0.2))*0.2),
    1);
}
  )";
  auto [effect, errorText] = SkRuntimeEffect::MakeForShader(SkString(sksl_src));
  if (!effect) {
    errlog("sksl didn't compile: %s", errorText.c_str());
    return;
  }
  dlog("sksl compiled OK");

  if (fShaderBuilder) {
    delete fShaderBuilder;
  }
  fShaderBuilder = new SkRuntimeShaderBuilder(std::move(effect));

  // SkRuntimeShaderBuilder::BuilderUniform uniform(const char* name) {
  //   return fBuilder->uniform(name);
  // }

  // dlog("setting SkSL uniform 'gColor'");
  // fShaderBuilder->uniform("gColor") = float4{ 1.0, 0.25, 0.75, 1.0 };

  // // sk_sp<SkShader> makeShader(const SkMatrix* localMatrix, bool isOpaque)
  // auto shader = fShaderBuilder->makeShader(nullptr, false);
  // if (!shader) {
  //   errlog("fShaderBuilder->makeShader failed");
  //   return;
  // }
  // fTestShader = shader;
  // dlog("fShaderBuilder->makeShader OK");
}

void App::onBackendCreated() {
  dlog("App::onBackendCreated");
  this->updateTitle();
  fWindow->show();
  fWindow->inval();
  fTimeBase = SkTime::GetSecs();
}

void App::onPaint(SkSurface* surface) {
  auto time = (float)(SkTime::GetSecs() - fTimeBase);
  auto canvas = surface->getCanvas();

  // Clear background
  //canvas->clear(SK_ColorWHITE);

  if (fShaderBuilder) {
    // fShaderBuilder->uniform("gColor") = float4{ (float)(time*0.01), 0.25, 0.75, 1.0 };
    if (!fShaderBuilder->uniform("uTime").set(&time, 1)) {
      errlog("failed to set uniform uTime");
    }
    auto shader = fShaderBuilder->makeShader(nullptr, false);
    if (!shader) {
      errlog("fShaderBuilder->makeShader failed");
    } else {
      // dlog("shader getTypeName() => %s", shader->getTypeName()); // SkRTShader
      SkPaint shader1;
      shader1.setShader(std::move(shader));
      shader1.setBlendMode(SkBlendMode::kSrc);
      //canvas->save();

      // paint on entire canvas
      canvas->drawPaint(shader1);

      // paint in shapes
      // shader1.setAntiAlias(true);
      // canvas->drawCircle(500, 400, 256, shader1);
      // canvas->drawCircle(700, 500, 256, shader1);
      // canvas->drawCircle(400, 600, 256, shader1);

      //SkPaint noiseShader;
      //noiseShader.setShader(SkPerlinNoiseShader::MakeFractalNoise(0.5, 0.5, 4, time*120, nullptr));
      //noiseShader.setAlphaf(0.5);
      //canvas->drawPaint(noiseShader);

      //canvas->rotate(45.0f);
      //canvas->restore();
    }
  }

  // debug message
  SkPaint whitePaint;
  whitePaint.setColor(SK_ColorWHITE);
  SkPaint textShadowPaint;
  textShadowPaint.setColor(SK_ColorBLACK);
  textShadowPaint.setAlphaf(0.5);
  fDebugFrameCount++;
  const double sampleRateSec = 1.0; // how often to sample & compare, in seconds
  if (time - fDebugMessageTime >= sampleRateSec || fDebugMessageLen == 0) {
    // update
    double fAvgMsPerFrame = (
      fDebugMessageLen == 0 ? 0 : (sampleRateSec * 1000) / double(fDebugFrameCount) );
    fDebugMessageTime = time;
    fDebugMessageLen = snprintf(
      fDebugMessageBuf, sizeof(fDebugMessageBuf), "%.0f ms", floor(fAvgMsPerFrame));
    fDebugMessageAdvanceWidth = fFontInterMedium24.measureText(
      fDebugMessageBuf, (size_t)fDebugMessageLen, SkTextEncoding::kUTF8, nullptr, &whitePaint);
    fDebugFrameCount = 0;
  }
  SkISize canvasSize = canvas->getBaseLayerSize();
  canvas->drawSimpleText(
    fDebugMessageBuf, (size_t)fDebugMessageLen, SkTextEncoding::kUTF8,
    canvasSize.width() - (fDebugMessageAdvanceWidth + 7), fFontInterMedium24.getSize() + 10,
    fFontInterMedium24, textShadowPaint);
  canvas->drawSimpleText(
    fDebugMessageBuf, (size_t)fDebugMessageLen, SkTextEncoding::kUTF8,
    canvasSize.width() - (fDebugMessageAdvanceWidth + 8), fFontInterMedium24.getSize() + 8,
    fFontInterMedium24, whitePaint);

  /*SkPaint paint;
  paint.setColor(SK_ColorRED);

  // Draw a rectangle with red paint
  SkRect rect = SkRect::MakeXYWH(10, 10, 128, 128);
  canvas->drawRect(rect, paint);

  // Set up a linear gradient and draw a circle
  {
    SkPoint linearPoints[] = { { 0, 0 }, { 300, 300 } };
    SkColor linearColors[] = { SK_ColorGREEN, SK_ColorBLACK };
    paint.setShader(SkGradientShader::MakeLinear(linearPoints, linearColors, nullptr, 2,
                           SkTileMode::kMirror));
    paint.setAntiAlias(true);

    canvas->drawCircle(200, 200, 64, paint);

    // Detach shader
    paint.setShader(nullptr);
  }

  // Draw a message with a nice black paint
  SkFont font;
  font.setSubpixel(true);
  font.setSize(40);
  paint.setColor(SK_ColorBLACK);

  canvas->save();
  static const char message[] = "Hello World";

  // Translate and rotate
  canvas->translate(300, 300);
  fRotationAngle += 0.2f;
  if (fRotationAngle > 360) {
    fRotationAngle -= 360;
  }
  canvas->rotate(fRotationAngle);

  // Draw the text
  canvas->drawSimpleText(message, strlen(message), SkTextEncoding::kUTF8, 0, 0, font, paint);

  canvas->restore();*/
}

void App::onIdle() {
  // Just re-paint continously
  fWindow->inval();
}

bool App::onChar(SkUnichar c, skui::ModifierKey modifiers) {
  // if (' ' == c) {
  //   fBackendType = (
  //     fBackendType == Window::kRaster_BackendType ? Window::kNativeGL_BackendType :
  //     fBackendType == Window::kNativeGL_BackendType ? Window::kMetal_BackendType :
  //     Window::kRaster_BackendType
  //   );
  //   fWindow->detach();
  //   fWindow->attach(fBackendType);
  // }
  return true;
}
