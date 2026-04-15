#include "gpupixel_context.h"
#include "util.h"

#include "source_capture.h"
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
#include <chrono>
#include <thread>
#include <vector>
#include <wrl.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Foundation.h >
#include <mutex>
using namespace Microsoft::WRL;
using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
static Direct3D11CaptureFramePool g_framePool{nullptr};
static GraphicsCaptureSession g_session{nullptr};
static ComPtr<ID3D11Device> g_d3dDevice;
static ComPtr<ID3D11Texture2D> capturedTexture;
static GraphicsCaptureItem captureItem = {nullptr};
static IDirect3DDevice g_winrtDevice = nullptr;
static winrt::event_token g_frameToken;
static int captureHeight = 0;
static int captureWidth = 0;

namespace {
void stopCaptureSession() {
  if (g_framePool && g_frameToken) {
    g_framePool.FrameArrived(g_frameToken);
    g_frameToken = {};
  }
  if (g_session) {
    g_session.Close();
    g_session = nullptr;
  }
  if (g_framePool) {
    g_framePool.Close();
    g_framePool = nullptr;
  }
  g_d3dDevice.Reset();
  capturedTexture.Reset();
  captureItem = nullptr;
  g_winrtDevice = nullptr;
  captureHeight = 0;
  captureWidth = 0;
}
}  // namespace

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
#if defined(GPUPIXEL_WIN)
    stopCaptureSession();
#endif
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
    } else {
      stopCapture();
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

void SourceCapture::Render() {
  if (!_framebuffer || !_textureInitialized) {
    return;
  }
  GPUPixelContext::getInstance()->runSync([=] {

    // ��FBO���������
    _framebuffer->active();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    
    // �����ӿ�ƥ��FBO�ߴ�
    glViewport(0, 0, captureWidth, captureHeight);
    // ִ��ʵ����Ⱦ

    if (!targetHwnd_ || !IsWindow(targetHwnd_)) {

      return;
    }
    // ��֤HWND��Ч��
    if (!IsWindow(targetHwnd_)) {
      stopCapture();
      return;
    }
    
    // ��֤������
    if (!capturedTexture || captureWidth <= 0 || captureHeight <= 0) {
      return;
    }
    ComPtr<ID3D11DeviceContext> d3dContext;
    g_d3dDevice->GetImmediateContext(&d3dContext);
    D3D11_TEXTURE2D_DESC desc = {0};
    capturedTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;
    if (FAILED(g_d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture))) {
      return;
    }

    // ��������������Ƶ��ݴ�����
    d3dContext->CopyResource(stagingTexture.Get(), capturedTexture.Get());

    // ӳ����������
    D3D11_MAPPED_SUBRESOURCE mapped;
    d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                                  &mapped);

    // ������Ⱦ
    std::vector<uint8_t> pixels(captureWidth * captureHeight * 4);
    const uint8_t* src = static_cast<uint8_t*>(mapped.pData);
    uint8_t* dst = pixels.data();
    const size_t srcRowPitch = mapped.RowPitch;
    const size_t dstRowPitch = captureWidth * 4;  

    for (int y = 0; y < captureHeight; ++y) {
      memcpy(dst + y * dstRowPitch, src + y * srcRowPitch, dstRowPitch);
    }


    // ��ʼ��/����OpenGL����
    if (!_textureInitialized) {
    glGenTextures(1, &_texture);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    _textureInitialized = true;
    }
    // ����framebuffer
    if (!_framebuffer || _framebuffer->getWidth() != captureWidth ||
        _framebuffer->getHeight() != captureHeight) {
    _framebuffer = GPUPixelContext::getInstance()
                        ->getFramebufferFactory()
                        ->fetchFramebuffer(captureWidth, captureHeight);
    setFramebuffer(_framebuffer);
    }

    // �ϴ���������
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexImage2D(GL_TEXTURE_2D, 0,
                GL_RGBA,  // OpenGL�ڲ���ʽ
                captureWidth, captureHeight, 0,
                GL_BGRA,  // ƥ��DXGI_FORMAT_B8G8R8A8_UNORM
                 GL_UNSIGNED_BYTE, pixels.data());

    // ���ӳ��
    d3dContext->Unmap(stagingTexture.Get(), 0);
    
    

    renderToFramebuffer();

    if (_face_detector) {
      _face_detector->Detect(
          pixels.data(), _framebuffer->getWidth(), _framebuffer->getHeight(),
          GPUPIXEL_MODE_FMT_PICTURE, GPUPIXEL_FRAME_TYPE_RGBA8888);
    }

    // ���������˾���
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

  // ��֤HWND��Ч��
  if (!IsWindow(targetHwnd_)) {
    throw std::runtime_error("Invalid window handle");
  }
  stopCaptureSession();
  // ��ȡͼ�β���ӿ�
  auto activation_factory = winrt::get_activation_factory<
      winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
  auto captureInterop = activation_factory.as<IGraphicsCaptureItemInterop>();
    

  // ����������
  if (FAILED(captureInterop->CreateForWindow(
          targetHwnd_,
          winrt::guid_of<
              ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
          winrt::put_abi(captureItem)))) {
    throw std::runtime_error("Failed to create capture item");
  }

  // ����D3D11�豸
  D3D_FEATURE_LEVEL featureLevel;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION, &g_d3dDevice,
                               &featureLevel, nullptr))) {
    throw std::runtime_error("Failed to create D3D11 device");
  }

  // ת��ΪWinRTͼ���豸
  ComPtr<IDXGIDevice> dxgiDevice;
  g_d3dDevice.As(&dxgiDevice);
  IInspectable* inspectable = nullptr;
  if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),
                                                  &inspectable))) {
    throw std::runtime_error("Failed to create WinRT graphics device");
  }

 
  inspectable->QueryInterface(
      winrt::guid_of<IDirect3DDevice>(),
      reinterpret_cast<void**>(winrt::put_abi(g_winrtDevice)));
  inspectable->Release();


  // ����֡��
  g_framePool = Direct3D11CaptureFramePool::Create(
      g_winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
      captureItem.Size());

  // ��������Ự
  g_session =
      g_framePool.CreateCaptureSession(captureItem);

  g_session.StartCapture();

    g_frameToken = g_framePool.FrameArrived([&](auto&&,
                                                                auto&&) {

    if (Direct3D11CaptureFrame frame = g_framePool.TryGetNextFrame()) {
      // ��ȡ֡�ߴ�
      SizeInt32 size = frame.ContentSize();
      captureHeight = size.Height;
      captureWidth = size.Width;
      if (captureItem.Size().Width != captureWidth ||
          captureItem.Size().Height != captureHeight) {

        g_framePool.Recreate(g_winrtDevice,
                             DirectXPixelFormat::B8G8R8A8UIntNormalized, 1,
                             captureItem.Size());
        printf("%d %d\n", captureItem.Size().Width, captureItem.Size().Height);
      }
      // ��ȡDXGI�ӿ�
      auto surface = frame.Surface();
      auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

      // ��ȡ����
      ComPtr<ID3D11Texture2D> texture;
      if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&texture)))) {
        capturedTexture = texture;
      }
      frame.Close();
    }

  });
  // �ȴ��������
  MSG msg;
  const auto waitStart = std::chrono::steady_clock::now();
  constexpr auto kFirstFrameTimeout = std::chrono::milliseconds(400);
  while (true){
    
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (capturedTexture) {
      break;
    }
    if (std::chrono::steady_clock::now() - waitStart >= kFirstFrameTimeout) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }



  
  // ��֤������
  if (!capturedTexture || captureWidth <= 0 || captureHeight <= 0) {
    return;
  }

  ComPtr<ID3D11DeviceContext> d3dContext;
  g_d3dDevice->GetImmediateContext(&d3dContext);
  
  D3D11_TEXTURE2D_DESC desc = {0};
  capturedTexture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;
  desc.MiscFlags = 0;

  ComPtr<ID3D11Texture2D> stagingTexture;
  if (FAILED(g_d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture))) {
    throw std::runtime_error("Failed to create staging texture");
  }

  // ��������������Ƶ��ݴ�����
  d3dContext->CopyResource(stagingTexture.Get(), capturedTexture.Get());

  // ӳ����������
  D3D11_MAPPED_SUBRESOURCE mapped = {0};
  if (SUCCEEDED(d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                                &mapped))) {
    // ��ʼ��/����OpenGL����
    if (!_textureInitialized) {
      glGenTextures(1, &_texture);
      glBindTexture(GL_TEXTURE_2D, _texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      _textureInitialized = true;
    }

    // ����framebuffer�������Ҫ��
    if (!_framebuffer || _framebuffer->getWidth() != captureWidth ||
        _framebuffer->getHeight() != captureHeight) {
      _framebuffer = GPUPixelContext::getInstance()
                         ->getFramebufferFactory()
                         ->fetchFramebuffer(captureWidth, captureHeight);
      setFramebuffer(_framebuffer);
    }

    // �ϴ���������
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_RGBA,  // OpenGL�ڲ���ʽ
                 captureWidth, captureHeight, 0,
                 GL_BGRA,  // ƥ��DXGI_FORMAT_B8G8R8A8_UNORM
                 GL_UNSIGNED_BYTE, mapped.pData);

    // ���ӳ��
    d3dContext->Unmap(stagingTexture.Get(), 0);
  }

  // ������Ⱦ
  renderToFramebuffer();
#endif
}

void SourceCapture::stopCapture() {
#if defined(GPUPIXEL_WIN)
  stopCaptureSession();
  targetHwnd_ = nullptr;
#elif defined(GPUPIXEL_MAC)
  macWindowID_ = 0;
#endif
}

}  // namespace gpupixel
