#pragma once

#include "gpupixel_program.h"
#include "source.h"
#include <windows.h>
#include <wingdi.h>
#include <mutex>


static std::mutex mtx;
namespace gpupixel {

class GPUPIXEL_API SourceCapture : public Source {
 public:
  ~SourceCapture();
  static std::shared_ptr<SourceCapture> create();
  void Render();
  void captureFrame();
  bool isTextureInitialized() const { return _textureInitialized; }

#if defined(GPUPIXEL_WIN)
  void setTargetWindow(HWND hwnd);
#elif defined(GPUPIXEL_MAC)
  void setTargetWindow(uint32_t windowID);
#endif

 private:
  SourceCapture();
  bool init();
  void processWindowCapture();
  void renderToFramebuffer();
  GPUPixelGLProgram* _filterProgram = nullptr;
  GLuint _texture = 0;
  bool _textureInitialized = false;
#if defined(GPUPIXEL_WIN)
  HWND targetHwnd_ = nullptr;
#elif defined(GPUPIXEL_MAC)
  uint32_t macWindowID_ = 0;
#endif
};

}  // namespace gpupixel
