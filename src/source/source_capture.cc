#include "source_capture.h"
#include "gpupixel_context.h"
#include "util.h"


#if defined(GPUPIXEL_WIN)
#include <windows.h>
#elif defined(GPUPIXEL_MAC)
#include <CoreGraphics/CoreGraphics.hs
#endif
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <d3d11.h>
#include <dxgi.h>
#include <vector>
#include <wrl.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Foundation.h >
using namespace Microsoft::WRL;
using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

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

    // 验证HWND有效性
    if (!IsWindow(targetHwnd_)) {
      throw std::runtime_error("Invalid window handle");
    }

    // 获取图形捕获接口
    auto activation_factory = winrt::get_activation_factory<
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
    auto captureInterop = activation_factory.as<IGraphicsCaptureItemInterop>();

    // 创建捕获项
    GraphicsCaptureItem captureItem = {nullptr};
    if (FAILED(captureInterop->CreateForWindow(
            targetHwnd_,
            winrt::guid_of<
                ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
            winrt::put_abi(captureItem)))) {
      throw std::runtime_error("Failed to create capture item");
    }

    // 创建D3D11设备
    ComPtr<ID3D11Device> d3dDevice;
    D3D_FEATURE_LEVEL featureLevel;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                 nullptr, 0, D3D11_SDK_VERSION, &d3dDevice,
                                 &featureLevel, nullptr))) {
      throw std::runtime_error("Failed to create D3D11 device");
    }

    // 转换为WinRT图形设备
    ComPtr<IDXGIDevice> dxgiDevice;
    d3dDevice.As(&dxgiDevice);
    IInspectable* inspectable = nullptr;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),
                                                    &inspectable))) {
      throw std::runtime_error("Failed to create WinRT graphics device");
    }

    IDirect3DDevice winrtDevice = nullptr;
    inspectable->QueryInterface(
        winrt::guid_of<IDirect3DDevice>(),
        reinterpret_cast<void**>(winrt::put_abi(winrtDevice)));
    inspectable->Release();

    // 创建帧池
    Direct3D11CaptureFramePool framePool = Direct3D11CaptureFramePool::Create(
        winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
        captureItem.Size());

    // 创建捕获会话
    GraphicsCaptureSession session =
        framePool.CreateCaptureSession(captureItem);
    auto start_time = std::chrono::steady_clock::now();
    session.StartCapture();
    // 同步事件处理
    ComPtr<ID3D11Texture2D> capturedTexture;
    int captureWidth = 0;
    int captureHeight = 0;

    auto frameToken = framePool.FrameArrived([&](auto&&, auto&&) {
      if (Direct3D11CaptureFrame frame = framePool.TryGetNextFrame()) {
        // 获取帧尺寸
        SizeInt32 size = frame.ContentSize();
        captureWidth = size.Width;
        captureHeight = size.Height;

        // 获取DXGI接口
        auto surface = frame.Surface();
        auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::
                                     IDirect3DDxgiInterfaceAccess>();

        // 获取纹理
        ComPtr<ID3D11Texture2D> texture;
        if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&texture)))) {
          capturedTexture = texture;
        }
        session.Close();
        framePool.Close();
      }
    });

    // 等待捕获完成
    MSG msg;
    while (true) {
      while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      if (capturedTexture) {
        break;
      }
    }


    // 验证捕获结果
    if (!capturedTexture || captureWidth <= 0 || captureHeight <= 0) {
      throw std::runtime_error("Frame capture failed");
    }

    ComPtr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(&d3dContext);

    D3D11_TEXTURE2D_DESC desc = {0};
    capturedTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;
    if (FAILED(d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture))) {
      throw std::runtime_error("Failed to create staging texture");
    }

    // 将捕获的纹理复制到暂存纹理
    d3dContext->CopyResource(stagingTexture.Get(), capturedTexture.Get());

    // 映射纹理数据
    D3D11_MAPPED_SUBRESOURCE mapped = {0};
    if (SUCCEEDED(d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                                  &mapped))) {
      // 初始化/更新OpenGL纹理
      if (!_textureInitialized) {
        glGenTextures(1, &_texture);
        glBindTexture(GL_TEXTURE_2D, _texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        _textureInitialized = true;
      }
      // 更新framebuffer（如果需要）
      if (!_framebuffer || _framebuffer->getWidth() != captureWidth ||
          _framebuffer->getHeight() != captureHeight) {
        _framebuffer = GPUPixelContext::getInstance()
                           ->getFramebufferFactory()
                           ->fetchFramebuffer(captureWidth, captureHeight);
        setFramebuffer(_framebuffer);

      }

      // 上传纹理数据
      glBindTexture(GL_TEXTURE_2D, _texture);
      glTexImage2D(GL_TEXTURE_2D, 0,
                   GL_RGBA,  // OpenGL内部格式
                   captureWidth, captureHeight, 0,
                   GL_BGRA,  // 匹配DXGI_FORMAT_B8G8R8A8_UNORM
                   GL_UNSIGNED_BYTE, mapped.pData);

      // 解除映射
      d3dContext->Unmap(stagingTexture.Get(), 0);
    }
    // 触发渲染
    std::vector<uint8_t> pixels(captureWidth * captureHeight * 4);
    const uint8_t* src = static_cast<uint8_t*>(mapped.pData);
    uint8_t* dst = pixels.data();
    const size_t srcRowPitch = mapped.RowPitch;
    const size_t dstRowPitch = captureWidth * 4;  // 假设目标不需要对齐
    auto end_time = std::chrono::steady_clock::now();
    for (int y = 0; y < captureHeight; ++y) {
      memcpy(dst + y * dstRowPitch, src + y * srcRowPitch, dstRowPitch);
    }



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

  // 验证HWND有效性
  if (!IsWindow(targetHwnd_)) {
    throw std::runtime_error("Invalid window handle");
  }

  // 获取图形捕获接口
  auto activation_factory = winrt::get_activation_factory<
      winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
  auto captureInterop = activation_factory.as<IGraphicsCaptureItemInterop>();
    
  // 创建捕获项
  GraphicsCaptureItem captureItem = {nullptr};
  if (FAILED(captureInterop->CreateForWindow(
          targetHwnd_,
          winrt::guid_of<
              ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
          winrt::put_abi(captureItem)))) {
    throw std::runtime_error("Failed to create capture item");
  }

  // 创建D3D11设备
  ComPtr<ID3D11Device> d3dDevice;
  D3D_FEATURE_LEVEL featureLevel;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION, &d3dDevice,
                               &featureLevel, nullptr))) {
    throw std::runtime_error("Failed to create D3D11 device");
  }

  // 转换为WinRT图形设备
  ComPtr<IDXGIDevice> dxgiDevice;
  d3dDevice.As(&dxgiDevice);
  IInspectable* inspectable = nullptr;
  if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),
                                                  &inspectable))) {
    throw std::runtime_error("Failed to create WinRT graphics device");
  }

  IDirect3DDevice winrtDevice = nullptr;
  inspectable->QueryInterface(
      winrt::guid_of<IDirect3DDevice>(),
      reinterpret_cast<void**>(winrt::put_abi(winrtDevice)));
  inspectable->Release();

  // 创建帧池
  Direct3D11CaptureFramePool framePool = Direct3D11CaptureFramePool::Create(
      winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
      captureItem.Size());

  // 创建捕获会话
  GraphicsCaptureSession session = framePool.CreateCaptureSession(captureItem);
  session.StartCapture();
  // 同步事件处理
  ComPtr<ID3D11Texture2D> capturedTexture;
  int captureWidth = 0;
  int captureHeight = 0;

  auto frameToken = framePool.FrameArrived([&](auto&&, auto&&) {
    if (Direct3D11CaptureFrame frame = framePool.TryGetNextFrame()) {
      // 获取帧尺寸
      SizeInt32 size = frame.ContentSize();
      captureWidth = size.Width;
      captureHeight = size.Height;

      // 获取DXGI接口
      auto surface = frame.Surface();
      auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

      // 获取纹理
      ComPtr<ID3D11Texture2D> texture;
      if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&texture)))) {
        capturedTexture = texture;
      }
      session.Close();
      framePool.Close();
    }
  });

  // 等待捕获完成
  MSG msg;
  while (true){
    
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (capturedTexture) {
      break;
    }
  }



  
  // 验证捕获结果
  if (!capturedTexture || captureWidth <= 0 || captureHeight <= 0) {
    throw std::runtime_error("Frame capture failed");
  }

  ComPtr<ID3D11DeviceContext> d3dContext;
  d3dDevice->GetImmediateContext(&d3dContext);
  
  D3D11_TEXTURE2D_DESC desc = {0};
  capturedTexture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;
  desc.MiscFlags = 0;

  ComPtr<ID3D11Texture2D> stagingTexture;
  if (FAILED(d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture))) {
    throw std::runtime_error("Failed to create staging texture");
  }

  // 将捕获的纹理复制到暂存纹理
  d3dContext->CopyResource(stagingTexture.Get(), capturedTexture.Get());

  // 映射纹理数据
  D3D11_MAPPED_SUBRESOURCE mapped = {0};
  if (SUCCEEDED(d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                                &mapped))) {
    // 初始化/更新OpenGL纹理
    if (!_textureInitialized) {
      glGenTextures(1, &_texture);
      glBindTexture(GL_TEXTURE_2D, _texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      _textureInitialized = true;
    }

    // 更新framebuffer（如果需要）
    if (!_framebuffer || _framebuffer->getWidth() != captureWidth ||
        _framebuffer->getHeight() != captureHeight) {
      _framebuffer = GPUPixelContext::getInstance()
                         ->getFramebufferFactory()
                         ->fetchFramebuffer(captureWidth, captureHeight);
      setFramebuffer(_framebuffer);
    }

    // 上传纹理数据
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_RGBA,  // OpenGL内部格式
                 captureWidth, captureHeight, 0,
                 GL_BGRA,  // 匹配DXGI_FORMAT_B8G8R8A8_UNORM
                 GL_UNSIGNED_BYTE, mapped.pData);

    // 解除映射
    d3dContext->Unmap(stagingTexture.Get(), 0);
  }

  // 触发渲染
  renderToFramebuffer();
#endif
}

}  // namespace gpupixel
