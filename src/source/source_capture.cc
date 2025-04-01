#include "source_capture.h"
#include "gpupixel_context.h"
#include "util.h"


#if defined(GPUPIXEL_WIN)
#include <windows.h>
#elif defined(GPUPIXEL_MAC)
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace gpupixel {

const std::string kCaptureVertexShader = R"(
    attribute vec4 position;
    attribute vec4 inputTextureCoordinate;
    varying vec2 textureCoordinate;
    void main() {
        gl_Position = position;
        textureCoordinate = inputTextureCoordinate.xy;
    })";

const std::string kCaptureFragmentShader = R"(
    varying vec2 textureCoordinate;
    uniform sampler2D inputImageTexture;
    void main() {
        gl_FragColor = texture2D(inputImageTexture, textureCoordinate);
    })";

std::shared_ptr<SourceCapture> SourceCapture::create() {
  auto source = std::shared_ptr<SourceCapture>(new SourceCapture());
  if (source->init()) {
    return source;
  }
  return nullptr;
}

SourceCapture::SourceCapture() {}

SourceCapture::~SourceCapture() {
  GPUPixelContext::getInstance()->runSync([=] {
    if (_texture) {
      glDeleteTextures(1, &_texture);
    }
  });
}

bool SourceCapture::init() {
  _filterProgram = GPUPixelGLProgram::createByShaderString(
      kCaptureVertexShader, kCaptureFragmentShader);
  return _filterProgram != nullptr;
}

#if defined(GPUPIXEL_WIN)
void SourceCapture::setTargetWindow(HWND hwnd) {
  GPUPixelContext::getInstance()->runSync([=] {
    targetHwnd_ = hwnd;
    if (targetHwnd_ && IsWindow(targetHwnd_)) {
      processWindowCapture();
    }
  });
}
#elif defined(GPUPIXEL_MAC)
void SourceCapture::setTargetWindow(uint32_t windowID) {
  GPUPixelContext::getInstance()->runSync([=] {
    macWindowID_ = windowID;
    if (macWindowID_ != 0) {
      processWindowCapture();
    }
  });
}
#endif

void SourceCapture::captureFrame() {
  GPUPixelContext::getInstance()->runSync([=] { processWindowCapture(); });
}

void SourceCapture::Render() {
  if (!_framebuffer || !_textureInitialized) {
    return;
  }

  GPUPixelContext::getInstance()->runSync([=] {
    // 绑定FBO并清除内容
    _framebuffer->active();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 设置视口匹配FBO尺寸
    glViewport(0, 0, _framebuffer->getWidth(), _framebuffer->getHeight());

    // 执行实际渲染

    if (!targetHwnd_ || !IsWindow(targetHwnd_)) {
      return;
    }

    HDC hdcWindow = GetDC(targetHwnd_);
    RECT rc;
    GetClientRect(targetHwnd_, &rc);

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, width, height);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi = {sizeof(bi), width, -height, 1, 32, BI_RGB};
    std::vector<uint8_t> pixels(width * height * 4);
    GetDIBits(hdcMem, hBitmap, 0, height, pixels.data(), (BITMAPINFO*)&bi,
              DIB_RGB_COLORS);

    // 初始化纹理
    if (!_textureInitialized) {
      glGenTextures(1, &_texture);
      glBindTexture(GL_TEXTURE_2D, _texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      _textureInitialized = true;
    }

    // 更新framebuffer
    if (!_framebuffer || _framebuffer->getWidth() != width ||
        _framebuffer->getHeight() != height) {
      _framebuffer = GPUPixelContext::getInstance()
                         ->getFramebufferFactory()
                         ->fetchFramebuffer(width, height);
      setFramebuffer(_framebuffer);
    }

    // 上传纹理
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA,
                 GL_UNSIGNED_BYTE, pixels.data());

    // 清理资源
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(targetHwnd_, hdcWindow);

    // 触发渲染

    renderToFramebuffer();

    if(_face_detector) {
    _face_detector->Detect(pixels.data(),
                           _framebuffer->getWidth(),
                           _framebuffer->getHeight(),
                           GPUPIXEL_MODE_FMT_PICTURE,
                           GPUPIXEL_FRAME_TYPE_RGBA8888);
  }
    
    
// 触发后续滤镜链
    Source::doRender(true);
    
  });
}

void SourceCapture::renderToFramebuffer() {


  GPUPixelContext::getInstance()->setActiveShaderProgram(_filterProgram);
  _framebuffer->active();

  static const GLfloat vertices[] = {-1.0f, 1.0f, 1.0f, 1.0f,
                                     -1.0f, -1.0f,  1.0f, -1.0f};

  GLuint positionAttr = _filterProgram->getAttribLocation("position");
  glEnableVertexAttribArray(positionAttr);
  glVertexAttribPointer(positionAttr, 2, GL_FLOAT, GL_FALSE, 0, vertices);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, _texture);
  _filterProgram->setUniformValue("inputImageTexture", 0);

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(positionAttr);

  _framebuffer->inactive();
}

void SourceCapture::processWindowCapture() {
#if defined(GPUPIXEL_WIN)
  if (!targetHwnd_ || !IsWindow(targetHwnd_)) {
    return;
  }

  HDC hdcWindow = GetDC(targetHwnd_);
  RECT rc;
  GetClientRect(targetHwnd_, &rc);

  int width = rc.right - rc.left;
  int height = rc.bottom - rc.top;

  HDC hdcMem = CreateCompatibleDC(hdcWindow);
  HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, width, height);
  SelectObject(hdcMem, hBitmap);
  BitBlt(hdcMem, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

  BITMAPINFOHEADER bi = {sizeof(bi), width, -height, 1, 32, BI_RGB};
  std::vector<uint8_t> pixels(width * height * 4);
  GetDIBits(hdcMem, hBitmap, 0, height, pixels.data(), (BITMAPINFO*)&bi,
            DIB_RGB_COLORS);

  // 初始化纹理
  if (!_textureInitialized) {
    glGenTextures(1, &_texture);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    _textureInitialized = true;
  }

  // 更新framebuffer
  if (!_framebuffer || _framebuffer->getWidth() != width ||
      _framebuffer->getHeight() != height) {
    _framebuffer = GPUPixelContext::getInstance()
                       ->getFramebufferFactory()
                       ->fetchFramebuffer(width, height);
    setFramebuffer(_framebuffer);
  }

  // 上传纹理
  glBindTexture(GL_TEXTURE_2D, _texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA,
               GL_UNSIGNED_BYTE, pixels.data());

  // 清理资源
  DeleteObject(hBitmap);
  DeleteDC(hdcMem);
  ReleaseDC(targetHwnd_, hdcWindow);

  // 触发渲染
  renderToFramebuffer();
#endif
}

}  // namespace gpupixel
