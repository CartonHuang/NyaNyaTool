#define GLFW_EXPOSE_NATIVE_WIN32
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>  // For Windows API
#include <iostream>
#include <string>
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
// 在app.cc顶部添加全局纹理变量
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
                        GLFW_FALSE);  // 禁用自动最小化
    glfwSetInputMode(mainWindow, GLFW_CURSOR,
                     GLFW_CURSOR_NORMAL);  // 保持鼠标可见  
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
  glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);  // 保持窗口装饰
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
  io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc",  // 微软雅黑字体路径
                               18.0f, nullptr,
                               io.Fonts->GetGlyphRangesChineseFull());

  // 设置其他配置
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  // 初始化后端
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
// 修改 enumWindowsProc 回调
BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
  wchar_t titleW[256];
  char titleUTF8[256 * 4];

  GetWindowTextW(hwnd, titleW, sizeof(titleW) / sizeof(wchar_t));
  WideCharToMultiByte(CP_UTF8, 0, titleW, -1, titleUTF8, sizeof(titleUTF8),
                      NULL, NULL);
  if (IsWindowVisible(hwnd) && titleW[0] != L'\0') {
    openWindows.push_back(hwnd);
    // 添加窗口句柄信息到标题
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s [0x%p]",
             strlen(titleUTF8) > 0 ? titleUTF8 : "(Untitled)", hwnd);
    windowTitles.push_back(buffer);
  }
  return TRUE;
}

// Render a single frame
void renderFrame() {
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
        // 检测选中的窗口是否被最小化
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
    sourceCapture->Render();  // 这会自动绑定FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  } else {
    sourceImage->Render();
  }
  

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  // Swap buffers and poll events
  glfwSwapBuffers(mainWindow);
  glfwPollEvents();
}

// Clean up resources
void cleanupResources() {
  // Cleanup ImGui
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  // Cleanup GLFW
  glfwTerminate();
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
  while (!glfwWindowShouldClose(mainWindow)) {
    renderFrame();
  }

  // Cleanup
  cleanupResources();

  return 0;
}
