#define GLFW_EXPOSE_NATIVE_WIN32
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>  // For Windows API
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "gpupixel.h"
#include "imgui.h"
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")

using namespace gpupixel;

// Filters
std::shared_ptr<BeautyFaceFilter> beautyFilter;
std::shared_ptr<FaceReshapeFilter> reshapeFilter;
std::shared_ptr<gpupixel::LipstickFilter> lipstickFilter;
std::shared_ptr<gpupixel::BlusherFilter> blusherFilter;
std::shared_ptr<BeautyFaceFilter> beautyFilter1;
std::shared_ptr<FaceReshapeFilter> reshapeFilter1;
std::shared_ptr<gpupixel::LipstickFilter> lipstickFilter1;
std::shared_ptr<gpupixel::BlusherFilter> blusherFilter1;
std::shared_ptr<SourceImage> sourceImage;
std::shared_ptr<SourceCapture> sourceCapture;
std::shared_ptr<SinkRender> renderSink;
std::shared_ptr<SinkRender> renderSink1;
// Filter parameters
float beautyStrength = 0.0f;
float whiteningStrength = 0.0f;
float faceSlimStrength = 0.0f;
float eyeEnlargeStrength = 0.0f;
float lipstickStrength = 0.0f;
float blusherStrength = 0.0f;
bool isWindowTopMost = false;
// ��app.cc�������ȫ���������
static GLuint g_capturedTexture = 0;
static ImVec2 g_capturedSize(0, 0);

// GLFW window handle
GLFWwindow* mainWindow = nullptr;

// List of open windows
std::vector<HWND> openWindows;
std::vector<std::string> windowTitles;
bool showWindowSelector = false;
// Callback for window close event
// GLFW framebuffer resize callback
void onFramebufferResize(GLFWwindow* window, int width, int height) {
  glViewport(0, 0, width, height);
  renderSink->onSizeChanged(width, height);
  renderSink1->onSizeChanged(width, height);
}

// Initialize GLFW and create window
#include "imgui_internal.h"

// Add a new variable to track fullscreen state
bool isFullscreen = false;
GLFWwindow* fullscreenWindow = nullptr;

// Function to toggle fullscreen mode
void toggleFullscreen() {
  if (isFullscreen) {
    // Restore windowed mode
    glfwSetWindowMonitor(mainWindow, nullptr, 100, 100, 1280, 720, 0);
    glfwSetWindowAttrib(mainWindow, GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwSetWindowAttrib(mainWindow, GLFW_FLOATING, GLFW_TRUE);
    isFullscreen = false;
  } else {
    // Enter fullscreen mode  
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();  
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);  
    glfwSetWindowMonitor(mainWindow, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    glfwSetWindowAttrib(mainWindow, GLFW_AUTO_ICONIFY,
                        GLFW_FALSE);  // �����Զ���С��
    glfwSetInputMode(mainWindow, GLFW_CURSOR,
                     GLFW_CURSOR_NORMAL);  // �������ɼ�  
    isFullscreen = true;
  }
}

// Key callback to handle F key press
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (key == GLFW_KEY_F && action == GLFW_PRESS) {
    toggleFullscreen();
  }
}

bool setupGlfwWindow() {
  // Initialize GLFW
  if (!glfwInit()) {
    std::cout << "Failed to initialize GLFW" << std::endl;
    return false;
  }
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
  glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);  // ���ִ���װ��
  // Get OpenGL context window from GPUPixel
  mainWindow = GPUPixelContext::getInstance()->GetGLContext();
  if (mainWindow == NULL) {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  HWND hwnd = glfwGetWin32Window(mainWindow);
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  style = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
          WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX |
          WS_MAXIMIZEBOX;

  SetWindowLongPtr(hwnd, GWL_STYLE, style);


  gladLoadGL();
  glfwMakeContextCurrent(mainWindow);
  // Enable VSync to reduce tearing under compositor scheduling pressure.
  glfwSwapInterval(1);
  glfwShowWindow(mainWindow);
  glfwSetFramebufferSizeCallback(mainWindow, onFramebufferResize);

  glfwSetWindowAttrib(mainWindow, GLFW_HOVERED, GLFW_TRUE);

  // Set key callback
  glfwSetKeyCallback(mainWindow, keyCallback);

  return true;
}


// Initialize ImGui interface
void setupImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc",  // ΢���ź�����·��
                               18.0f, nullptr,
                               io.Fonts->GetGlyphRangesChineseFull());

  // ������������
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  // ��ʼ�����
  ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
  ImGui_ImplOpenGL3_Init("#version 130");
}

// Initialize GPUPixel filters and pipeline
void setupFilterPipeline() {
  // Create filters
  lipstickFilter1 = LipstickFilter::create();
  blusherFilter1 = BlusherFilter::create();
  reshapeFilter1 = FaceReshapeFilter::create();
  beautyFilter1 = BeautyFaceFilter::create();


  // Create source image and render sink
  sourceImage = SourceImage::create("./demo.png");
  sourceCapture = SourceCapture::create();
  renderSink = std::make_shared<SinkRender>();
  renderSink1 = std::make_shared<SinkRender>();





  // Setup face landmarks callback

  sourceCapture->RegLandmarkCallback([=](std::vector<float> landmarks) {
    lipstickFilter1->SetFaceLandmarks(landmarks);
    blusherFilter1->SetFaceLandmarks(landmarks);
    reshapeFilter1->SetFaceLandmarks(landmarks);
  });

  // Build filter pipeline
  sourceImage
      ->addSink(renderSink);

  sourceCapture->addSink(lipstickFilter1)
      ->addSink(blusherFilter1)
      ->addSink(reshapeFilter1)
      ->addSink(beautyFilter1)
      ->addSink(renderSink1);
  renderSink->onSizeChanged(1280, 720);
  renderSink1->onSizeChanged(1280, 720);

}

// Update filter parameters from UI controls
void updateFilterParameters() {
  // Beauty filter controls
  if (ImGui::SliderFloat("Smoothing", &beautyStrength, 0.0f, 10.0f)) {
    beautyFilter1->setBlurAlpha(beautyStrength / 10.0f);
  }

  if (ImGui::SliderFloat("Whitening", &whiteningStrength, 0.0f, 10.0f)) {
    beautyFilter1->setWhite(whiteningStrength / 20.0f);
  }

  if (ImGui::SliderFloat("Face Slimming", &faceSlimStrength, 0.0f, 10.0f)) {
    reshapeFilter1->setFaceSlimLevel(faceSlimStrength / 200.0f);
  }

  if (ImGui::SliderFloat("Eye Enlarging", &eyeEnlargeStrength, 0.0f, 10.0f)) {
    reshapeFilter1->setEyeZoomLevel(eyeEnlargeStrength / 100.0f);
  }

  if (ImGui::SliderFloat("Lipstick", &lipstickStrength, 0.0f, 10.0f)) {
    lipstickFilter1->setBlendLevel(lipstickStrength / 10.0f);
  }

  if (ImGui::SliderFloat("Blusher", &blusherStrength, 0.0f, 10.0f)) {
    blusherFilter1->setBlendLevel(blusherStrength / 10.0f);
  }
}



// EnumWindows callback function to list open windows
// �޸� enumWindowsProc �ص�
BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
  wchar_t titleW[256];
  char titleUTF8[256 * 4];

  GetWindowTextW(hwnd, titleW, sizeof(titleW) / sizeof(wchar_t));
  WideCharToMultiByte(CP_UTF8, 0, titleW, -1, titleUTF8, sizeof(titleUTF8),
                      NULL, NULL);
  if (IsWindowVisible(hwnd) && titleW[0] != L'\0') {
    openWindows.push_back(hwnd);
    // ��Ӵ��ھ����Ϣ������
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s [0x%p]",
             strlen(titleUTF8) > 0 ? titleUTF8 : "(Untitled)", hwnd);
    windowTitles.push_back(buffer);
  }
  return TRUE;
}

// Render a single frame
void renderFrame() {
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(mainWindow, &framebufferWidth, &framebufferHeight);
  glViewport(0, 0, framebufferWidth, framebufferHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Start ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Create control panel
  ImGui::Begin("Beauty Control Panel", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize);
  updateFilterParameters();

  // Add button to capture window
  if (ImGui::Button("Capture Window")) {
    // List open windows
    openWindows.clear();
    windowTitles.clear();
    EnumWindows(enumWindowsProc, 0);
    showWindowSelector = true;
  }
  // Add button to toggle window top-most state
  if (ImGui::Button(isWindowTopMost ? "Disable Top-Most" : "Enable Top-Most")) {
    isWindowTopMost = !isWindowTopMost;
    HWND hwnd = glfwGetWin32Window(mainWindow);
    if (isWindowTopMost) {
      glfwSetWindowAttrib(mainWindow, GLFW_FLOATING, GLFW_TRUE);
    } else {
      glfwSetWindowAttrib(mainWindow, GLFW_FLOATING, GLFW_FALSE);
    }
  }

  ImGui::End();
 
  // Show window selector
  if (showWindowSelector) {
    ImGui::Begin("Select Window", &showWindowSelector);
    for (size_t i = 0; i < windowTitles.size(); ++i) {
      if (ImGui::Button(windowTitles[i].c_str())) {
        // ���ѡ�еĴ����Ƿ���С��
        HWND hwnd = openWindows[i];
        if (IsIconic(hwnd)) {
          continue;
        }
        sourceCapture->setTargetWindow(openWindows[i]);
        showWindowSelector = false;
      }
    }
    ImGui::End();
  }
  

  if (sourceCapture && sourceCapture->isTextureInitialized()) {
    // Render ImGui
    sourceCapture->Render();  // ����Զ���FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  } else {
    sourceImage->Render();
  }
  

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  // Swap buffers
  glfwSwapBuffers(mainWindow);
}

// Clean up resources
void cleanupResources() {
  if (sourceCapture) {
    sourceCapture->stopCapture();
  }
  sourceCapture.reset();
  sourceImage.reset();
  renderSink.reset();
  renderSink1.reset();
  beautyFilter1.reset();
  reshapeFilter1.reset();
  lipstickFilter1.reset();
  blusherFilter1.reset();

  // Cleanup ImGui
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  // Destroy OpenGL context/window managed by GPUPixel.
  GPUPixelContext::destroy();
}

int main() {
  // Initialize window and OpenGL context
  if (!setupGlfwWindow()) {
    return -1;
  }

  // Setup ImGui interface
  setupImGui();

  // Initialize filters and pipeline
  setupFilterPipeline();
  // Main render loop
  auto lastFrameTime = std::chrono::steady_clock::now();
  while (true) {
    glfwPollEvents();
    if (glfwWindowShouldClose(mainWindow)) {
      break;
    }
    renderFrame();
    const bool isFocused =
        glfwGetWindowAttrib(mainWindow, GLFW_FOCUSED) == GLFW_TRUE;
    if (!isFocused) {
      // Keep a stable low refresh when unfocused to avoid scheduler-induced jitter.
      constexpr auto kUnfocusedFrameInterval = std::chrono::milliseconds(33);
      const auto now = std::chrono::steady_clock::now();
      const auto elapsed = now - lastFrameTime;
      if (elapsed < kUnfocusedFrameInterval) {
        std::this_thread::sleep_for(kUnfocusedFrameInterval - elapsed);
      }
    }
    lastFrameTime = std::chrono::steady_clock::now();
  }

  // Cleanup
  cleanupResources();

  return 0;
}
