//
// Copyright 2011 Tero Saarni
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <android/native_window.h> // requires ndk r5 or newer

#include "logger.h"
#include "renderer.h"

#define LOG_TAG "EglSample"

static void buildShader ();
static void bindProg();

static GLint vertices[][3] = {
        {-0x10000, -0x10000, -0x10000},
        {0x10000,  -0x10000, -0x10000},
        {0x10000,  0x10000,  -0x10000},
        {-0x10000, 0x10000,  -0x10000},
        {-0x10000, -0x10000, 0x10000},
        {0x10000,  -0x10000, 0x10000},
        {0x10000,  0x10000,  0x10000},
        {-0x10000, 0x10000,  0x10000}
};

static float squareCoords[]={
        -0.5f, 0.0f, 0.0f, //top left
        -0.0f, -0.5f, 0.0f, //bottom left
        1.0f, -0.0f, 0.0f, //bottom right
        0.0f, 1.0f, 0.0f, //top right
};
short drawOrder[] = {0,1,2,0,2,3};


static GLint colors[][4] = {
        {0x00000, 0x00000, 0x00000, 0x10000},
        {0x10000, 0x00000, 0x00000, 0x10000},
        {0x10000, 0x10000, 0x00000, 0x10000},
        {0x00000, 0x10000, 0x00000, 0x10000},
        {0x00000, 0x00000, 0x10000, 0x10000},
        {0x10000, 0x00000, 0x10000, 0x10000},
        {0x10000, 0x10000, 0x10000, 0x10000},
        {0x00000, 0x10000, 0x10000, 0x10000}
};

GLubyte indices[] = {
        0, 4, 5, 0, 5, 1,
        1, 5, 6, 1, 6, 2,
        2, 6, 7, 2, 7, 3,
        3, 7, 4, 3, 4, 0,
        4, 7, 6, 4, 6, 5,
        3, 0, 1, 3, 1, 2
};


Renderer::Renderer()
        : _msg(MSG_NONE), _display(0), _surface(0), _context(0), _angle(0) {
    LOG_INFO("Renderer instance created");
    pthread_mutex_init(&_mutex, 0);
    return;
}

Renderer::~Renderer() {
    LOG_INFO("Renderer instance destroyed");
    pthread_mutex_destroy(&_mutex);
    return;
}

void Renderer::start() {
    LOG_INFO("Creating renderer thread");
    pthread_create(&_threadId, 0, threadStartCallback, this);
    return;
}

void Renderer::stop() {
    LOG_INFO("Stopping renderer thread");

    // send message to render thread to stop rendering
    pthread_mutex_lock(&_mutex);
    _msg = MSG_RENDER_LOOP_EXIT;
    pthread_mutex_unlock(&_mutex);

    pthread_join(_threadId, 0);
    LOG_INFO("Renderer thread stopped");

    return;
}

void Renderer::setWindow(ANativeWindow *window) {
    // notify render thread that window has changed
    pthread_mutex_lock(&_mutex);
    _msg = MSG_WINDOW_SET;
    _window = window;
    pthread_mutex_unlock(&_mutex);

    return;
}

void Renderer::renderLoop() {
    bool renderingEnabled = true;

    LOG_INFO("renderLoop()");

    while (renderingEnabled) {

        pthread_mutex_lock(&_mutex);

        // process incoming messages
        switch (_msg) {

            case MSG_WINDOW_SET:
                initialize();
                initShader();
                break;

            case MSG_RENDER_LOOP_EXIT:
                renderingEnabled = false;
                destroy();
                break;

            default:
                break;
        }
        _msg = MSG_NONE;

        if (_display) {
            drawFrame();
//            LOG_INFO("xx....");
            if (!eglSwapBuffers(_display, _surface)) {
                LOG_ERROR("eglSwapBuffers() returned error %d", eglGetError());
            }
        }

        pthread_mutex_unlock(&_mutex);
    }

    LOG_INFO("Render loop exits");

    return;
}

EGLint width;
EGLint height;
EGLDisplay display;
EGLConfig config;
EGLint numConfigs;
EGLint format;
EGLSurface surface;
EGLContext context;

GLfloat ratio;

bool Renderer::initialize() {
    const EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_SAMPLE_BUFFERS, 1,
            EGL_SAMPLES, 4,
            EGL_NONE
    };

    glEnable(GL_MULTISAMPLE);   // Enoch  GL_MULTISAMPLE is undeclared

    EGLint major;
    EGLint minor;

    LOG_INFO("Initializing context");

    if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
        LOG_ERROR("eglGetDisplay() returned error %d", eglGetError());
        return false;
    }

    if (!eglInitialize(display, &major, &minor)) {
        LOG_ERROR("eglInitialize() returned error %d", eglGetError());
        return false;
    } else {
        LOG_INFO("EGL version: major=%d, minor=%d", major, minor);
    }
//-------eglQueryString---------
    const char *vendor;
    const char *version;
    const char *extensions;
    vendor = eglQueryString(display, EGL_VENDOR);
    LOG_INFO("EGL vendor:%s", vendor);

    version = eglQueryString(display, EGL_VERSION);
    LOG_INFO("EGL version:%s", version);

    extensions = eglQueryString(display, EGL_EXTENSIONS);
    LOG_INFO("EGL extensions:%s", extensions);
//---------------------------------

    EGLint num_configs;
    EGLConfig *configs_list;
    //find how many configurations are supported
    if (!eglGetConfigs(display, NULL, 0, &num_configs)) {
        return false;
    } else {
        LOG_INFO("Configurations supported number: %d", num_configs);
    }
    //configs_list = static_cast<EGLConfig *> (malloc(num_configs * sizeof(EGLConfig)));
    configs_list = new EGLConfig[num_configs];

    //Get configurations
    if (!eglGetConfigs(display, configs_list, num_configs, &num_configs)) {
        LOG_ERROR("eglGetConfig() returned error %d", eglGetError());
        destroy();
        return false;
    } else {
        EGLint value;
        LOG_INFO("Configurations get-||||||||");
        for (int i = 0; i < num_configs; i++) {
            if (!eglGetConfigAttrib(display, configs_list[i], EGL_RED_SIZE, &value)) {
                LOG_ERROR("eglGetConfigAttrib() returned error %d", eglGetError());
                return false;
            } else {
                LOG_INFO("     Config[%d] EGL_RED_SIZE:%d", i, value);
            }
        }
    }
    //Get max number of configs choosed
    if (!eglChooseConfig(display, attribs, NULL, 0, &numConfigs)) {
        LOG_ERROR("eglChooseConfig() returned error %d", eglGetError());
        destroy();
        return false;
    } else {
        LOG_INFO("eglChooseConfig get config max number: %d", numConfigs);
    }

    //Just use the first one as our config
    if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)) {
        LOG_ERROR("eglChooseConfig() returned error %d", eglGetError());
        destroy();
        return false;
    } else {
        LOG_INFO("eglChooseConfig get config number: %d", numConfigs);
    }

    if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
        LOG_ERROR("eglGetConfigAttrib() returned error %d", eglGetError());
        destroy();
        return false;
    }

//    ANativeWindow_setBuffersGeometry(_window, 0, 0, format);

    EGLint surfaceAttribList[] = {
            EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
            EGL_NONE
    };
    //if (!(surface = eglCreateWindowSurface(display, config, _window, 0))) {
    if (!(surface = eglCreateWindowSurface(display, config, _window, surfaceAttribList))) {
        LOG_ERROR("eglCreateWindowSurface() returned error %d", eglGetError());
        destroy();
        return false;
    }

    EGLint contextAttribList[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    //if (!(context = eglCreateContext(display, config, 0, 0))) {
    if (!(context = eglCreateContext(display, config, 0, contextAttribList))) {
        LOG_ERROR("eglCreateContext() returned error %x", eglGetError());
        destroy();
        return false;
    }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOG_ERROR("eglMakeCurrent() returned error %d", eglGetError());
        destroy();
        return false;
    }

    if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
        LOG_ERROR("eglQuerySurface() returned error %d", eglGetError());
        destroy();
        return false;
    } else {
        LOG_INFO("Surface size is %d x %d", width, height);
    }

    _display = display;
    _surface = surface;
    _context = context;

/*
//    Origin code, but GL_PERSPECTIVE_CORRECTION_HINT, GL_SMOOTH is undeclared.
//    glDisable(GL_DITHER);
//    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
//    glClearColor(0, 0, 0, 0);
//    glEnable(GL_CULL_FACE);
//    glShadeModel(GL_SMOOTH);
//    glEnable(GL_DEPTH_TEST);
*/

    glViewport(0, 0, width, height);


//    // Add by Enoch : Normal AA, GL_POINT_SMOOTH, GL_POINT_SMOOTH_HINT is undeclared.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    ratio = (GLfloat) width / height;
//    glMatrixMode(GL_PROJECTION);
//    glLoadIdentity();
//    glFrustumf(-ratio, ratio, -1, 1, 1, 10);

    delete configs_list;
    return true;
}

void Renderer::destroy() {
    LOG_INFO("Destroying context");

    eglMakeCurrent(_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(_display, _context);
    eglDestroySurface(_display, _surface);
    eglTerminate(_display);

    _display = EGL_NO_DISPLAY;
    _surface = EGL_NO_SURFACE;
    _context = EGL_NO_CONTEXT;

    return;
}

void Renderer::drawFrame() {
    //LOG_INFO("drawFrame %d x %d", width, height);
    static float r=0.9f;
    static float g=0.2f;
    static float b=0.2f;

    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glViewport(0,0,width,height);
    glScissor(0,0,width,height);

    /*
//    r += 0.01f;
//    if (r > 1.0f) {
//      r = 0.0f;
//    }
//    glMatrixMode(GL_MODELVIEW);
//    glLoadIdentity();
//    glTranslatef(0, 0, -3.0f);
//    glRotatef(_angle, 0, 1, 0);
//    glRotatef(_angle * 0.25f, 1, 0, 0);
//
//    glEnableClientState(GL_VERTEX_ARRAY);
//    glEnableClientState(GL_COLOR_ARRAY);
//
//    glFrontFace(GL_CW);
//    glVertexPointer(3, GL_FIXED, 0, vertices);
//    glColorPointer(4, GL_FIXED, 0, colors);
//    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, indices);
//    _angle += 1.2f;
*/
    bindProg();
}

void Renderer::initShader() {
    buildShader();
}

void *Renderer::threadStartCallback(void *myself) {
    Renderer *renderer = (Renderer *) myself;


    renderer->renderLoop();
    pthread_exit(0);

    return 0;
}

GLuint	program;
GLuint	vertexShader;
GLuint	fragmentShader;
GLuint  uMvp;
GLuint  uColor;
GLuint  p;
GLuint  p1;

const char *vertexSrc =
        "attribute vec4 vPosition;          \n"
                "attribute vec4 vPosition1;         \n"
                "uniform mat4 uMVPMatrix;           \n"
                "void main() {                      \n"
                "  gl_Position = vPosition;\n"
                "  gl_PointSize = 50.0; \n"
                "}                                  \n";

const char *fragmentSrc =
        "precision mediump float;           \n"
                "uniform vec4 vColor;               \n"
                "void main() {                      \n"
                "  gl_FragColor = vec4(0.0,1.0,0.0,1.0);           \n"
                "}                                  \n";


static bool CompileShader( const GLuint shader, const char * src ) {
    glShaderSource( shader, 1, &src, 0 );
    glCompileShader( shader );

    GLint r;
    glGetShaderiv( shader, GL_COMPILE_STATUS, &r );
    if ( r == GL_FALSE )
    {
        LOG_ERROR( "Compiling shader:\n%s\n****** failed ******\n", src );
        GLchar msg[4096];
        glGetShaderInfoLog( shader, sizeof( msg ), 0, msg );
        LOG_ERROR( "%s\n", msg );
        return false;
    }
    return true;
}

static void buildShader () {
    vertexShader = glCreateShader( GL_VERTEX_SHADER );
    LOG_INFO("vertex shader \n%s", vertexSrc);
    if ( !CompileShader( vertexShader, vertexSrc ) )
    {
        LOG_ERROR("Failed to compile vertex shader");

    }
    fragmentShader = glCreateShader( GL_FRAGMENT_SHADER );
    LOG_INFO("fragment shader %s", fragmentSrc);
    if ( !CompileShader( fragmentShader, fragmentSrc ) )
    {
        LOG_ERROR("Failed to compile fragment shader");
    }
    program = glCreateProgram();

    if(!program)
    {
        LOG_ERROR("Failed: glCreateProgram");
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glBindAttribLocation(program, 0, "vPosition");
    glBindAttribLocation(program, 1, "vPosition1");

    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);

    if(!status)
    {
        GLint info_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_length);
        if(info_length)
        {
            char* buf = new char[info_length];
            glGetProgramInfoLog(program, info_length, NULL, buf);
            LOG_ERROR("create program failed\n%s\n", buf);

            delete[] buf;
        }

        glDetachShader(program, vertexShader);
        glDetachShader(program, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(program);
        program = 0;
    }

    uMvp = glGetUniformLocation( program, "uMVPMatrix");
    uColor = glGetUniformLocation( program, "vColor");
}

static void bindProg() {

    const GLfloat landscapeOrientationMatrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };
    const GLfloat color[4] = {
            1.0f, 0.0f, 0.0f, 1.0f
    };
    glUseProgram( program );
    glUniformMatrix4fv(uMvp, 1, GL_FALSE, landscapeOrientationMatrix);
    glUniform4fv(uColor, 1, color);
    p = glGetAttribLocation(program, "vPosition");
    p1 = glGetAttribLocation(program, "vPosition1");

    glEnableVertexAttribArray( p );
    glVertexAttribPointer( p , 3, GL_FLOAT, false, 3 * sizeof( float ), squareCoords);

    glEnable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glLineWidth(80);
    glDrawArrays(GL_POINTS, 0, 4);
    glDisableVertexAttribArray( p );
    glFlush();
}