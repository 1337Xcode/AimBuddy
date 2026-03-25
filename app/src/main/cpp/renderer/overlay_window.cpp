#include "overlay_window.h"

namespace ESP {

OverlayWindow::OverlayWindow()
    : display_(EGL_NO_DISPLAY)
    , surface_(EGL_NO_SURFACE)
    , context_(EGL_NO_CONTEXT)
    , window_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false) {
    LOGD("OverlayWindow created");
}

OverlayWindow::~OverlayWindow() {
    shutdown();
}

bool OverlayWindow::initialize(ANativeWindow* window, int width, int height) {
    if (initialized_) {
        LOGW("OverlayWindow already initialized");
        return true;
    }
    
    if (!window) {
        LOGE("Native window is null");
        return false;
    }
    
    window_ = window;
    width_ = width;
    height_ = height;
    
    LOGI("Initializing OverlayWindow %dx%d", width_, height_);
    
    // Get EGL display
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }
    
    // Initialize EGL
    EGLint major, minor;
    if (!eglInitialize(display_, &major, &minor)) {
        LOGE("eglInitialize failed");
        return false;
    }
    LOGI("EGL version: %d.%d", major, minor);
    
    // Choose EGL config with alpha for transparency
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,  // Alpha for transparency
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(display_, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }
    
    // Set native window buffer format to RGBA_8888
    EGLint format;
    eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(window_, 0, 0, format);
    
    // Create EGL surface
    surface_ = eglCreateWindowSurface(display_, config, window_, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError());
        return false;
    }
    
    // Create OpenGL ES 3.1 context
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }
    
    // Make context current
    if (!makeCurrent()) {
        LOGE("Failed to make context current");
        return false;
    }
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Disable depth testing (2D overlay)
    glDisable(GL_DEPTH_TEST);
    
    // Set viewport
    glViewport(0, 0, width_, height_);
    
    LOGI("OpenGL ES version: %s", glGetString(GL_VERSION));
    LOGI("OpenGL ES renderer: %s", glGetString(GL_RENDERER));
    
    initialized_ = true;
    LOGI("OverlayWindow initialized successfully");
    return true;
}

void OverlayWindow::shutdown() {
    if (!initialized_) {
        return;
    }
    
    LOGI("Shutting down OverlayWindow");
    
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
        
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
    
    window_ = nullptr;
    initialized_ = false;
}

bool OverlayWindow::makeCurrent() {
    if (!initialized_ && display_ == EGL_NO_DISPLAY) {
        return false;
    }
    
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        return false;
    }
    return true;
}

bool OverlayWindow::swapBuffers() {
    if (!initialized_) {
        return false;
    }
    
    if (!eglSwapBuffers(display_, surface_)) {
        EGLint error = eglGetError();
        if (error == EGL_BAD_SURFACE) {
            LOGW("EGL surface lost, need to recreate");
            return false;
        }
        LOGE("eglSwapBuffers failed: 0x%x", error);
        return false;
    }
    return true;
}

void OverlayWindow::clear() {
    // Clear with fully transparent background
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void OverlayWindow::resize(int width, int height) {
    width_ = width;
    height_ = height;
    glViewport(0, 0, width_, height_);
    LOGD("OverlayWindow resized to %dx%d", width_, height_);
}

} // namespace ESP
