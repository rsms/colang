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
#include "modules/skshaper/include/SkShaper.h"

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

private:
    Window* fWindow;
    Window::BackendType fBackendType;
    double fTimeBase = 0;

    SkScalar fRotationAngle;

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
    , fRotationAngle(0)
{
  SkGraphics::Init();

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

void App::onAttach(Window* window) {
  dlog("App::onAttach");
}

void App::onBackendCreated() {
  fWindow->setTitle("Shapes & Colors");
  fWindow->show();
  fWindow->inval();
  fTimeBase = SkTime::GetSecs();
}

void App::onPaint(SkSurface* surface) {
  auto time = (float)(SkTime::GetSecs() - fTimeBase);
  auto canvas = surface->getCanvas();

  // Clear background
  static constexpr SkColor kBgColor = SkColorSetARGB(0xFF, 0xEE, 0xEE, 0xEE);
  canvas->clear(kBgColor);

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
