#include "glx_mirror_pipeline.h"

#include "glx_shared_contexts.h"
#include "x11_runtime.h"

#include "../common/anchor_coords.h"
#include "../common/config_io.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <deque>
#include <mutex>
#include <cstring>
#include <thread>
#include <fstream>
#include <unordered_map>
#include <vector>

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

namespace platform::x11 {

namespace {

static const char* kVertShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
})";

static const char* kFilterFragShader = R"(#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTexture;
uniform vec4 u_sourceRect;
uniform int u_gammaMode;
uniform vec3 u_targetColors[8];
uniform int u_targetColorCount;
uniform vec4 outputColor;
uniform float u_sensitivity;

vec3 SRGBToLinear(vec3 c) {
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    vec3 low = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, vec3(cutoff));
}
void main() {
    vec2 srcCoord = u_sourceRect.xy + TexCoord * u_sourceRect.zw;
    vec3 screenColor = texture(screenTexture, srcCoord).rgb;
    vec3 screenColorLinear = SRGBToLinear(screenColor);

    bool matches = false;
    for (int i = 0; i < u_targetColorCount; i++) {
        vec3 targetColorSRGB = u_targetColors[i];
        vec3 targetColorLinear = SRGBToLinear(targetColorSRGB);

        float dist;
        if (u_gammaMode == 2) {
            dist = distance(screenColor, targetColorLinear);
        } else if (u_gammaMode == 1) {
            dist = distance(screenColorLinear, targetColorLinear);
        } else {
            float distSRGB = distance(screenColor, targetColorSRGB);
            float distLinear = distance(screenColorLinear, targetColorLinear);
            dist = min(distSRGB, distLinear);
        }

        if (dist < u_sensitivity) {
            matches = true;
            break;
        }
    }

    if (matches) {
        FragColor = outputColor;
    } else {
        FragColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
})";

static const char* kFilterPassthroughFragShader = R"(#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTexture;
uniform vec4 u_sourceRect;
uniform int u_gammaMode;
uniform vec3 u_targetColors[8];
uniform int u_targetColorCount;
uniform float u_sensitivity;

vec3 SRGBToLinear(vec3 c) {
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    vec3 low = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, vec3(cutoff));
}
void main() {
    vec2 srcCoord = u_sourceRect.xy + TexCoord * u_sourceRect.zw;
    vec3 screenColor = texture(screenTexture, srcCoord).rgb;
    vec3 screenColorLinear = SRGBToLinear(screenColor);

    bool matches = false;
    for (int i = 0; i < u_targetColorCount; i++) {
        vec3 targetColorSRGB = u_targetColors[i];
        vec3 targetColorLinear = SRGBToLinear(targetColorSRGB);

        float dist;
        if (u_gammaMode == 2) {
            dist = distance(screenColor, targetColorLinear);
        } else if (u_gammaMode == 1) {
            dist = distance(screenColorLinear, targetColorLinear);
        } else {
            float distSRGB = distance(screenColor, targetColorSRGB);
            float distLinear = distance(screenColorLinear, targetColorLinear);
            dist = min(distSRGB, distLinear);
        }

        if (dist < u_sensitivity) {
            matches = true;
            break;
        }
    }

    if (matches) {
        FragColor = vec4(screenColor, 1.0);
    } else {
        FragColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
})";

static const char* kPassthroughFragShader = R"(#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTexture;
uniform vec4 u_sourceRect;
void main() {
    vec2 srcCoord = u_sourceRect.xy + TexCoord * u_sourceRect.zw;
    vec4 c = texture(screenTexture, srcCoord);
    // Force alpha=1 to avoid propagating undefined/junk alpha from game textures.
    FragColor = vec4(c.rgb, 1.0);
})";

static const char* kRenderFragShader = R"(#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D filterTexture;
uniform int u_borderWidth;
uniform vec4 u_outputColor;
uniform vec4 u_borderColor;
uniform vec2 u_screenPixel;
void main() {
    vec4 texColor = texture(filterTexture, TexCoord);
    // Check if this pixel was a color match (alpha > 0.5 in filter output)
    if (texColor.a > 0.5) {
        FragColor = u_outputColor;
        return;
    }
    // Check neighbors for border (only consider pixels that were matches)
    float maxA = 0.0;
    for (int x = -u_borderWidth; x <= u_borderWidth; x++) {
        for (int y = -u_borderWidth; y <= u_borderWidth; y++) {
            if (x == 0 && y == 0) continue;
            vec2 offset = vec2(x, y) * u_screenPixel;
            maxA = max(maxA, texture(filterTexture, TexCoord + offset).a);
        }
    }
    if (maxA > 0.5) {
        FragColor = u_borderColor;
    } else {
        discard;
    }
})";

static const char* kRenderPassthroughFragShader = R"(#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D filterTexture;
uniform int u_borderWidth;
uniform vec4 u_borderColor;
uniform vec2 u_screenPixel;
void main() {
    vec4 texColor = texture(filterTexture, TexCoord);
    if (texColor.a > 0.5) {
        FragColor = vec4(texColor.rgb, 1.0);
        return;
    }
    float maxA = 0.0;
    for (int x = -u_borderWidth; x <= u_borderWidth; x++) {
        for (int y = -u_borderWidth; y <= u_borderWidth; y++) {
            if (x == 0 && y == 0) continue;
            vec2 offset = vec2(x, y) * u_screenPixel;
            maxA = max(maxA, texture(filterTexture, TexCoord + offset).a);
        }
    }
    if (maxA > 0.5) {
        FragColor = u_borderColor;
    } else {
        discard;
    }
})";

// Static border SDF shader (ported from Windows render_thread.cpp lines 484-560)
static const char* kStaticBorderFragShader = R"(#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform int u_shape;         // 0=Rectangle (with optional rounded corners), 1=Circle/Ellipse
uniform vec4 u_borderColor;
uniform float u_thickness;   // Border thickness in pixels
uniform float u_radius;      // Corner radius for Rectangle in pixels (0 = sharp corners)
uniform vec2 u_size;         // BASE shape size (width/height) - NOT the expanded quad size
uniform vec2 u_quadSize;     // Actual expanded quad size rendered by GPU

// SDF for a rounded rectangle (works for sharp corners when r=0)
float sdRoundedBox(vec2 p, vec2 b, float r) {
    // Clamp radius to not exceed half of the smaller box dimension
    float maxR = min(b.x, b.y);
    r = clamp(r, 0.0, maxR);
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// SDF for an ellipse - proper signed distance approximation
float sdEllipse(vec2 p, vec2 ab) {
    // Normalize to unit circle space
    vec2 pn = p / ab;
    float len = length(pn);
    if (len < 0.0001) return -min(ab.x, ab.y); // At center
    
    // Distance in normalized space
    float d = len - 1.0;
    
    // Correct for the stretching using the gradient magnitude
    vec2 grad = pn / (ab * len);
    float gradLen = length(grad);
    
    // Scale distance back to pixel space
    return d / gradLen;
}

void main() {
    // Map TexCoord (0-1) to pixel coordinates within the actual GPU quad
    vec2 pixelPos = TexCoord * u_quadSize;
    
    // Offset so (0,0) is at the center of the quad
    vec2 centeredPixelPos = pixelPos - u_quadSize * 0.5;
    
    // Calculate distance in pixels from the shape edge
    // The shape has size u_size, centered at origin
    vec2 halfSize = max(u_size * 0.5, vec2(1.0, 1.0));
    
    float dist;
    
    if (u_shape == 0) {
        // Rectangle (with optional rounded corners via u_radius)
        dist = sdRoundedBox(centeredPixelPos, halfSize, u_radius);
    } else {
        // Circle/Ellipse
        dist = sdEllipse(centeredPixelPos, halfSize);
    }
    
    // Border is drawn at the shape edge (dist=0) outward to thickness
    float innerEdge = 0.0;
    float outerEdge = u_thickness;
    
    // Add small epsilon for floating-point precision at quad boundaries
    float epsilon = 0.5;
    
    if (dist >= innerEdge - epsilon && dist <= outerEdge + epsilon) {
        FragColor = u_borderColor;
    } else {
        discard;
    }
})";

// Gradient shader for multi-stop linear gradients with angle and animation support
static const char* kGradientFragShader = R"(#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

#define MAX_STOPS 8
#define ANIM_NONE 0
#define ANIM_ROTATE 1
#define ANIM_SLIDE 2
#define ANIM_WAVE 3
#define ANIM_SPIRAL 4
#define ANIM_FADE 5

uniform int u_numStops;
uniform vec4 u_stopColors[MAX_STOPS];
uniform float u_stopPositions[MAX_STOPS];
uniform float u_angle; // radians (base angle)
uniform float u_time;  // animation time in seconds
uniform int u_animationType;
uniform float u_animationSpeed;
uniform bool u_colorFade;

vec4 getGradientColorSeamless(float t) {
    t = fract(t);

    float lastPos = u_stopPositions[u_numStops - 1];
    float firstPos = u_stopPositions[0];
    float wrapSize = (1.0 - lastPos) + firstPos;

    if (t <= firstPos && wrapSize > 0.001) {
        float wrapT = (firstPos - t) / wrapSize;
        return mix(u_stopColors[0], u_stopColors[u_numStops - 1], wrapT);
    } else if (t >= lastPos && wrapSize > 0.001) {
        float wrapT = (t - lastPos) / wrapSize;
        return mix(u_stopColors[u_numStops - 1], u_stopColors[0], wrapT);
    }

    vec4 color = u_stopColors[0];
    for (int i = 0; i < u_numStops - 1; i++) {
        if (t >= u_stopPositions[i] && t <= u_stopPositions[i + 1]) {
            float segmentT = (t - u_stopPositions[i]) / max(u_stopPositions[i + 1] - u_stopPositions[i], 0.0001);
            color = mix(u_stopColors[i], u_stopColors[i + 1], segmentT);
            break;
        }
    }
    return color;
}

vec4 getGradientColor(float t, float timeOffset) {
    float adjustedT = t;
    if (u_colorFade) {
        adjustedT = fract(t + timeOffset * 0.1);
    }
    adjustedT = clamp(adjustedT, 0.0, 1.0);

    vec4 color = u_stopColors[0];
    for (int i = 0; i < u_numStops - 1; i++) {
        if (adjustedT >= u_stopPositions[i] && adjustedT <= u_stopPositions[i + 1]) {
            float segmentT = (adjustedT - u_stopPositions[i]) / max(u_stopPositions[i + 1] - u_stopPositions[i], 0.0001);
            color = mix(u_stopColors[i], u_stopColors[i + 1], segmentT);
            break;
        }
    }
    if (adjustedT >= u_stopPositions[u_numStops - 1]) {
        color = u_stopColors[u_numStops - 1];
    }
    return color;
}

vec4 getFadeColor(float timeOffset) {
    float cyclePos = fract(timeOffset * 0.1);

    vec4 color = u_stopColors[0];
    for (int i = 0; i < u_numStops - 1; i++) {
        if (cyclePos >= u_stopPositions[i] && cyclePos <= u_stopPositions[i + 1]) {
            float segmentT = (cyclePos - u_stopPositions[i]) / max(u_stopPositions[i + 1] - u_stopPositions[i], 0.0001);
            color = mix(u_stopColors[i], u_stopColors[i + 1], segmentT);
            break;
        }
    }
    if (cyclePos > u_stopPositions[u_numStops - 1]) {
        float wrapRange = 1.0 - u_stopPositions[u_numStops - 1] + u_stopPositions[0];
        float wrapT = (cyclePos - u_stopPositions[u_numStops - 1]) / max(wrapRange, 0.0001);
        color = mix(u_stopColors[u_numStops - 1], u_stopColors[0], wrapT);
    } else if (cyclePos < u_stopPositions[0]) {
        float wrapRange = 1.0 - u_stopPositions[u_numStops - 1] + u_stopPositions[0];
        float wrapT = (u_stopPositions[0] - cyclePos) / max(wrapRange, 0.0001);
        color = mix(u_stopColors[0], u_stopColors[u_numStops - 1], wrapT);
    }
    return color;
}

void main() {
    vec2 center = vec2(0.5, 0.5);
    vec2 uv = TexCoord - center;
    float t = 0.0;
    float timeOffset = u_time * u_animationSpeed;

    if (u_animationType == ANIM_NONE) {
        vec2 dir = vec2(cos(u_angle), sin(u_angle));
        t = dot(uv, dir) + 0.5;
        t = clamp(t, 0.0, 1.0);
        FragColor = getGradientColor(t, timeOffset);
    } else if (u_animationType == ANIM_ROTATE) {
        float effectiveAngle = u_angle + timeOffset;
        vec2 dir = vec2(cos(effectiveAngle), sin(effectiveAngle));
        t = dot(uv, dir) + 0.5;
        t = clamp(t, 0.0, 1.0);
        FragColor = getGradientColor(t, timeOffset);
    } else if (u_animationType == ANIM_SLIDE) {
        vec2 dir = vec2(cos(u_angle), sin(u_angle));
        t = dot(uv, dir) + 0.5 + timeOffset * 0.2;
        FragColor = getGradientColorSeamless(t);
    } else if (u_animationType == ANIM_WAVE) {
        vec2 dir = vec2(cos(u_angle), sin(u_angle));
        vec2 perpDir = vec2(-sin(u_angle), cos(u_angle));
        float perpPos = dot(uv, perpDir);
        float wave = sin(perpPos * 8.0 + timeOffset * 2.0) * 0.08;
        t = dot(uv, dir) + 0.5 + wave;
        t = clamp(t, 0.0, 1.0);
        FragColor = getGradientColor(t, timeOffset);
    } else if (u_animationType == ANIM_SPIRAL) {
        float dist = length(uv) * 2.0;
        float angle = atan(uv.y, uv.x);
        t = dist + angle / 6.28318 - timeOffset * 0.3;
        FragColor = getGradientColorSeamless(t);
    } else if (u_animationType == ANIM_FADE) {
        FragColor = getFadeColor(timeOffset);
    } else {
        t = clamp(t, 0.0, 1.0);
        FragColor = getGradientColor(t, timeOffset);
    }
})";

struct GlFunctions {
    // FBO
    PFNGLGENFRAMEBUFFERSPROC genFramebuffers = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC deleteFramebuffers = nullptr;
    PFNGLBINDFRAMEBUFFERPROC bindFramebuffer = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC framebufferTexture2D = nullptr;
    PFNGLBLITFRAMEBUFFERPROC blitFramebuffer = nullptr;
    PFNGLGENRENDERBUFFERSPROC genRenderbuffers = nullptr;
    PFNGLDELETERENDERBUFFERSPROC deleteRenderbuffers = nullptr;
    PFNGLBINDRENDERBUFFERPROC bindRenderbuffer = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC renderbufferStorage = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC framebufferRenderbuffer = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC checkFramebufferStatus = nullptr;

    // Shaders
    PFNGLCREATESHADERPROC createShader = nullptr;
    PFNGLSHADERSOURCEPROC shaderSource = nullptr;
    PFNGLCOMPILESHADERPROC compileShader = nullptr;
    PFNGLGETSHADERIVPROC getShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC getShaderInfoLog = nullptr;
    PFNGLDELETESHADERPROC deleteShader = nullptr;
    PFNGLCREATEPROGRAMPROC createProgram = nullptr;
    PFNGLATTACHSHADERPROC attachShader = nullptr;
    PFNGLLINKPROGRAMPROC linkProgram = nullptr;
    PFNGLUSEPROGRAMPROC useProgram = nullptr;
    PFNGLDELETEPROGRAMPROC deleteProgram = nullptr;
    PFNGLGETPROGRAMIVPROC getProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog = nullptr;

    // Uniforms
    PFNGLGETUNIFORMLOCATIONPROC getUniformLocation = nullptr;
    PFNGLUNIFORM1IPROC uniform1i = nullptr;
    PFNGLUNIFORM1FPROC uniform1f = nullptr;
    PFNGLUNIFORM1FVPROC uniform1fv = nullptr;
    PFNGLUNIFORM2FPROC uniform2f = nullptr;
    PFNGLUNIFORM3FPROC uniform3f = nullptr;
    PFNGLUNIFORM4FPROC uniform4f = nullptr;
    PFNGLUNIFORM3FVPROC uniform3fv = nullptr;
    PFNGLUNIFORM4FVPROC uniform4fv = nullptr;

    // VAO/VBO
    PFNGLGENVERTEXARRAYSPROC genVertexArrays = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC deleteVertexArrays = nullptr;
    PFNGLBINDVERTEXARRAYPROC bindVertexArray = nullptr;
    PFNGLGENBUFFERSPROC genBuffers = nullptr;
    PFNGLDELETEBUFFERSPROC deleteBuffers = nullptr;
    PFNGLBINDBUFFERPROC bindBuffer = nullptr;
    PFNGLBUFFERDATAPROC bufferData = nullptr;
    PFNGLBUFFERSUBDATAPROC bufferSubData = nullptr;
    PFNGLMAPBUFFERRANGEPROC mapBufferRange = nullptr;
    PFNGLUNMAPBUFFERPROC unmapBuffer = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC vertexAttribPointer = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enableVertexAttribArray = nullptr;

    // Texture
    PFNGLACTIVETEXTUREPROC activeTexture = nullptr;

    // Blend
    PFNGLBLENDFUNCSEPARATEPROC blendFuncSeparate = nullptr;

    // Sync
    PFNGLFENCESYNCPROC fenceSync = nullptr;
    PFNGLCLIENTWAITSYNCPROC clientWaitSync = nullptr;
    PFNGLDELETESYNCPROC deleteSync = nullptr;
};

struct FilterShaderLocs {
    GLint screenTexture = -1;
    GLint sourceRect = -1;
    GLint gammaMode = -1;
    GLint targetColors = -1;
    GLint targetColorCount = -1;
    GLint outputColor = -1;
    GLint sensitivity = -1;
};

struct FilterPassthroughShaderLocs {
    GLint screenTexture = -1;
    GLint sourceRect = -1;
    GLint gammaMode = -1;
    GLint targetColors = -1;
    GLint targetColorCount = -1;
    GLint sensitivity = -1;
};

struct PassthroughShaderLocs {
    GLint screenTexture = -1;
    GLint sourceRect = -1;
};

struct RenderShaderLocs {
    GLint filterTexture = -1;
    GLint borderWidth = -1;
    GLint outputColor = -1;
    GLint borderColor = -1;
    GLint screenPixel = -1;
};

struct RenderPassthroughShaderLocs {
    GLint filterTexture = -1;
    GLint borderWidth = -1;
    GLint borderColor = -1;
    GLint screenPixel = -1;
};

struct StaticBorderShaderLocs {
    GLint shape = -1;
    GLint borderColor = -1;
    GLint thickness = -1;
    GLint radius = -1;
    GLint size = -1;
    GLint quadSize = -1;
};

struct GradientShaderLocs {
    GLint numStops = -1;
    GLint stopColors = -1;
    GLint stopPositions = -1;
    GLint angle = -1;
    GLint time = -1;
    GLint animationType = -1;
    GLint animationSpeed = -1;
    GLint colorFade = -1;
};

struct MirrorShaderPrograms {
    GLuint filterProgram = 0;
    GLuint filterPassthroughProgram = 0;
    GLuint passthroughProgram = 0;
    GLuint renderProgram = 0;
    GLuint renderPassthroughProgram = 0;
    GLuint staticBorderProgram = 0;

    FilterShaderLocs filterLocs;
    FilterPassthroughShaderLocs filterPassthroughLocs;
    PassthroughShaderLocs passthroughLocs;
    RenderShaderLocs renderLocs;
    RenderPassthroughShaderLocs renderPassthroughLocs;
    StaticBorderShaderLocs staticBorderLocs;

    GLuint quadVao = 0;
    GLuint quadVbo = 0;
    bool ready = false;
};

struct X11MirrorInstance {
    GLuint filterFbo = 0;
    GLuint filterTexture = 0;
    GLuint finalFbo[2] = {};
    GLuint finalTexture[2] = {};
    GLuint contentDownsampleFbo = 0;
    GLuint contentDownsampleTex = 0;
    GLuint contentDetectionPbo = 0;
    int filterW = 0, filterH = 0;
    int finalW = 0, finalH = 0;
    int contentDownW = 0, contentDownH = 0;
    int contentPboW = 0, contentPboH = 0;
    std::atomic<int> frontIdx{ 0 };
    std::chrono::steady_clock::time_point lastCaptureTime{};
    bool hasValidContent = false;
    bool hasFrameContent = false;
};

GlFunctions g_gl;
std::mutex g_stateMutex;
std::mutex g_glResolveMutex;
std::atomic<bool> g_glReady{ false };
std::atomic<std::uint64_t> g_lastGeneration{ 0 };

std::unordered_map<std::string, X11MirrorInstance> g_instances;
MirrorShaderPrograms g_shaders;
std::vector<ResolvedMirrorRender> g_mirrorConfigs;
bool g_configsLoaded = false;
MirrorModeState g_modeState;
std::string g_currentActiveMode;
int g_lastOverlayViewportWidth = 0;
int g_lastOverlayViewportHeight = 0;

GLuint g_gameFrameTexture = 0;
GLuint g_gameFrameFbo = 0;
int g_gameFrameW = 0;
int g_gameFrameH = 0;

// Overscan FBO state
GLuint g_overscanFbo = 0;
GLuint g_overscanColorTex = 0;
GLuint g_overscanDepthRb = 0;
std::atomic<bool> g_overscanActive{false};
OverscanDimensions g_overscanDims;

// Cached GPU texture size limit (queried once on init)
int g_maxTextureSize = 0;
// Set to true once the game has rendered into the overscan FBO (via the
// glBindFramebuffer hook redirect). False immediately after FBO creation so
// the first swap hook call does not blit uninitialized FBO content to FBO 0.
bool g_overscanFboRendered = false;

static std::atomic<uint64_t> g_lastConfigVersion{0};
static std::mutex g_configRefreshMutex;

// Game-context-local overlay VAO/VBO (VAOs are per-context, can't share with worker)
static GLuint g_overlayVao = 0;
static GLuint g_overlayVbo = 0;
static bool g_overlayGeomReady = false;

// Overlay-context shader program (shared programs work across shared GLX contexts,
// but we also need a passthrough compiled on the game context for the overlay)
static GLuint g_overlayProgram = 0;
static GLint g_overlayLocScreenTexture = -1;
static GLint g_overlayLocSourceRect = -1;
static GLint g_overlayLocOpacity = -1;
static bool g_overlayProgramReady = false;
static GLuint g_solidColorProgram = 0;
static GLint g_solidColorLocColor = -1;
static bool g_solidColorProgramReady = false;
static GLuint g_gradientProgram = 0;
static GradientShaderLocs g_gradientLocs;
static bool g_gradientProgramReady = false;
static GLuint g_overlayStaticBorderProgram = 0;
static StaticBorderShaderLocs g_overlayStaticBorderLocs;
static bool g_overlayStaticBorderProgramReady = false;

struct ModeBackgroundImageGpu {
    std::string resolvedPath;
    bool loading = false;
    bool decodeFailed = false;
    bool isAnimated = false;
    int width = 0;
    int height = 0;
    std::vector<GLuint> frameTextures;
    std::vector<int> frameDelaysMs;
    int currentFrameIndex = 0;
    bool hasNextFrameTime = false;
    std::chrono::steady_clock::time_point nextFrameTime{};
};

struct BackgroundDecodeRequest {
    std::string modeName;
    std::string resolvedPath;
};

struct DecodedModeBackgroundImage {
    std::string modeName;
    std::string resolvedPath;
    bool success = false;
    bool isAnimated = false;
    int width = 0;
    int dataHeight = 0;
    int frameHeight = 0;
    int frameCount = 1;
    std::vector<int> frameDelaysMs;
    unsigned char* pixelData = nullptr;
};

std::unordered_map<std::string, ModeBackgroundImageGpu> g_modeBackgroundImages;
std::deque<BackgroundDecodeRequest> g_backgroundDecodeRequests;
std::deque<DecodedModeBackgroundImage> g_decodedModeBackgroundImages;
std::mutex g_backgroundDecodeMutex;
std::condition_variable g_backgroundDecodeCv;
std::thread g_backgroundDecodeThread;
std::atomic<bool> g_backgroundDecodeStop{false};
std::atomic<bool> g_backgroundDecodeStarted{false};

std::string g_stickyBackgroundModeName;
bool g_stickyBackgroundPending = false;
std::string g_lastRenderedBackgroundActiveMode;

struct MirrorFrameSlot {
    int width = 0;
    int height = 0;
    GLsync fence = nullptr;
    std::uint64_t generation = 0;

    int containerWidth = 0;
    int containerHeight = 0;
    int viewportTopLeftX = 0;
    int viewportTopLeftY = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
    int textureOriginTopLeftX = 0;
    int textureOriginTopLeftY = 0;

    bool overscanActive = false;
    int overscanWindowWidth = 0;
    int overscanWindowHeight = 0;
    int overscanMarginLeft = 0;
    int overscanMarginBottom = 0;
};

MirrorFrameSlot g_slotPool[2];
std::atomic<int> g_pendingSlotIdx{ -1 };
std::mutex g_slotMutex;
std::condition_variable g_slotCV;
std::atomic<bool> g_stopWorker{ false };
std::thread g_workerThread;
std::once_flag g_workerStartOnce;
std::atomic<bool> g_workerStarted{ false };
std::mutex g_workerExitMutex;
std::condition_variable g_workerExitCV;
bool g_workerExited = false;

struct StaleFenceNode {
    GLsync fence;
    StaleFenceNode* next;
};
std::mutex g_staleFenceMutex;
StaleFenceNode* g_staleFenceHead = nullptr;

// Worker generation tracking
std::atomic<std::uint64_t> g_workerGeneration{ 0 };
std::atomic<std::uint64_t> g_slotOverwriteCount{ 0 };
std::atomic<std::uint64_t> g_staleFenceDrainCount{ 0 };
std::atomic<std::uint64_t> g_fenceTimeoutCount{ 0 };

bool IsTruthyEnvValue(const char* value) {
    if (!value || value[0] == '\0') { return false; }
    return (value[0] == '1' && value[1] == '\0') || (value[0] == 'y' || value[0] == 'Y') || (value[0] == 't' || value[0] == 'T');
}

bool IsDebugEnabled() {
    static bool checked = false;
    static bool debug = false;
    if (!checked) {
        debug = IsTruthyEnvValue(std::getenv("LINUXSCREEN_X11_DEBUG"));
        checked = true;
    }
    return debug;
}

bool EndsWith(const std::string& value, const char* suffix) {
    if (!suffix) {
        return false;
    }

    const std::size_t suffixLen = std::strlen(suffix);
    if (value.size() < suffixLen) {
        return false;
    }

    return value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

bool IsPieAnchor(const std::string& relativeTo) {
    std::string anchor = relativeTo;
    if (EndsWith(anchor, "Viewport")) {
        anchor = anchor.substr(0, anchor.size() - 8);
    } else if (EndsWith(anchor, "Screen")) {
        anchor = anchor.substr(0, anchor.size() - 6);
    }
    return anchor == "pieLeft" || anchor == "pieRight";
}

bool ShouldUseViewportAnchor(const std::string& relativeTo) {
    return EndsWith(relativeTo, "Viewport") || IsPieAnchor(relativeTo);
}

std::string GetAnchorBase(const std::string& relativeTo) {
    std::string anchor = relativeTo;
    if (EndsWith(anchor, "Viewport")) {
        anchor = anchor.substr(0, anchor.size() - 8);
    } else if (EndsWith(anchor, "Screen")) {
        anchor = anchor.substr(0, anchor.size() - 6);
    }
    return anchor;
}

bool IsLeftAlignedAnchor(const std::string& anchorBase) {
    return anchorBase == "topLeft" || anchorBase == "middleLeft" || anchorBase == "bottomLeft";
}

bool IsRightAlignedAnchor(const std::string& anchorBase) {
    return anchorBase == "topRight" || anchorBase == "middleRight" || anchorBase == "bottomRight";
}

float ResolveUniformScaleByFitMode(float scaleX, float scaleY, const std::string& fitMode) {
    if (fitMode == "fitWidth") {
        return scaleX;
    }
    if (fitMode == "fitHeight") {
        return scaleY;
    }
    return std::min(scaleX, scaleY);
}

void ResolveMirrorCaptureInputCoords(const std::string& relativeTo,
                                     int configX,
                                     int configY,
                                     int captureW,
                                     int captureH,
                                     const MirrorFrameSlot& slot,
                                     int frameWidth,
                                     int frameHeight,
                                     int& outX,
                                     int& outY) {
    const int containerWidth = (slot.containerWidth > 0) ? slot.containerWidth : frameWidth;
    const int containerHeight = (slot.containerHeight > 0) ? slot.containerHeight : frameHeight;
    const int viewportWidth = (slot.viewportWidth > 0) ? slot.viewportWidth : containerWidth;
    const int viewportHeight = (slot.viewportHeight > 0) ? slot.viewportHeight : containerHeight;

    int globalX = 0;
    int globalY = 0;
    if (ShouldUseViewportAnchor(relativeTo)) {
        platform::config::GetRelativeCoords(relativeTo,
                                            configX,
                                            configY,
                                            captureW,
                                            captureH,
                                            viewportWidth,
                                            viewportHeight,
                                            globalX,
                                            globalY);
        globalX += slot.viewportTopLeftX;
        globalY += slot.viewportTopLeftY;
    } else {
        platform::config::GetRelativeCoords(relativeTo,
                                            configX,
                                            configY,
                                            captureW,
                                            captureH,
                                            containerWidth,
                                            containerHeight,
                                            globalX,
                                            globalY);
    }

    outX = globalX - slot.textureOriginTopLeftX;
    outY = globalY - slot.textureOriginTopLeftY;
}

struct ModeViewportRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

bool ResolveModeViewportRect(int containerWidth, int containerHeight, ModeViewportRect& outRect) {
    outRect = ModeViewportRect{};
    if (containerWidth <= 0 || containerHeight <= 0) {
        return false;
    }

    auto config = g_modeState.GetConfigSnapshot();
    if (!config) {
        return false;
    }

    const std::string activeMode = g_modeState.GetActiveModeName();
    if (activeMode.empty()) {
        return false;
    }

    const platform::config::ModeConfig* activeModeCfg = nullptr;
    for (const auto& mode : config->modes) {
        if (mode.name == activeMode) {
            activeModeCfg = &mode;
            break;
        }
    }
    if (!activeModeCfg) {
        return false;
    }

    int modeWidth = 0;
    int modeHeight = 0;
    MirrorModeState::CalculateModeDimensions(*activeModeCfg, containerWidth, containerHeight, modeWidth, modeHeight);
    if (modeWidth <= 0 || modeHeight <= 0) {
        return false;
    }

    int topLeftX = 0;
    int topLeftY = 0;
    platform::config::GetRelativeCoords(activeModeCfg->positionPreset,
                                        activeModeCfg->x,
                                        activeModeCfg->y,
                                        modeWidth,
                                        modeHeight,
                                        containerWidth,
                                        containerHeight,
                                        topLeftX,
                                        topLeftY);

    outRect.x = topLeftX;
    outRect.y = topLeftY;
    outRect.width = modeWidth;
    outRect.height = modeHeight;
    outRect.valid = true;
    return true;
}

void ResolveEyeZoomAspectBasis(const platform::config::EyeZoomConfig& zoomConfig, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    if (zoomConfig.zoomAreaWidth > 0) {
        outWidth = zoomConfig.zoomAreaWidth;
    } else if (zoomConfig.stretchWidth > 0) {
        outWidth = zoomConfig.stretchWidth;
    }

    if (zoomConfig.zoomAreaHeight > 0) {
        outHeight = zoomConfig.zoomAreaHeight;
    } else if (zoomConfig.outputHeight > 0) {
        outHeight = zoomConfig.outputHeight;
    }

    if (outWidth <= 0) {
        outWidth = std::max(1, zoomConfig.cloneWidth);
    }
    if (outHeight <= 0) {
        outHeight = std::max(1, zoomConfig.cloneHeight);
    }
}

void ResolveMirrorAnchorCoords(const std::string& relativeTo,
                               int configX,
                               int configY,
                               int captureW,
                               int captureH,
                               int containerWidth,
                               int containerHeight,
                               const ModeViewportRect& modeViewport,
                               int& outX,
                               int& outY) {
    int anchorWidth = containerWidth;
    int anchorHeight = containerHeight;
    int anchorOriginX = 0;
    int anchorOriginY = 0;

    if (modeViewport.valid && ShouldUseViewportAnchor(relativeTo)) {
        anchorWidth = modeViewport.width;
        anchorHeight = modeViewport.height;
        anchorOriginX = modeViewport.x;
        anchorOriginY = modeViewport.y;
    }

    platform::config::GetRelativeCoords(relativeTo,
                                        configX,
                                        configY,
                                        captureW,
                                        captureH,
                                        anchorWidth,
                                        anchorHeight,
                                        outX,
                                        outY);
    outX += anchorOriginX;
    outY += anchorOriginY;
}

void ResolveEyeZoomRelativeOutputSize(const platform::config::EyeZoomConfig& zoomConfig,
                                      int viewportWidth,
                                      int viewportHeight,
                                      const ModeViewportRect& modeViewport,
                                      int& outWidth,
                                      int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    if (viewportWidth < 1 || viewportHeight < 1) {
        return;
    }

    const std::string relativeTo = zoomConfig.outputRelativeTo.empty() ? "middleLeftScreen" : zoomConfig.outputRelativeTo;

    int outputContainerWidth = viewportWidth;
    int outputContainerHeight = viewportHeight;
    if (modeViewport.valid && ShouldUseViewportAnchor(relativeTo)) {
        outputContainerWidth = modeViewport.width;
        outputContainerHeight = modeViewport.height;
    }

    const float relativeWidth = std::clamp(zoomConfig.outputRelativeWidth, 0.01f, 20.0f);
    const float relativeHeight = std::clamp(zoomConfig.outputRelativeHeight, 0.01f, 20.0f);
    int width = std::max(1, static_cast<int>(static_cast<float>(outputContainerWidth) * relativeWidth));
    int height = std::max(1, static_cast<int>(static_cast<float>(outputContainerHeight) * relativeHeight));

    if (zoomConfig.outputPreserveAspectRatio) {
        int baseWidth = 0;
        int baseHeight = 0;
        ResolveEyeZoomAspectBasis(zoomConfig, baseWidth, baseHeight);
        const float scaleX = static_cast<float>(width) / static_cast<float>(baseWidth);
        const float scaleY = static_cast<float>(height) / static_cast<float>(baseHeight);
        const float uniformScale = ResolveUniformScaleByFitMode(scaleX, scaleY, zoomConfig.outputAspectFitMode);
        if (uniformScale > 0.0f) {
            width = std::max(1, static_cast<int>(static_cast<float>(baseWidth) * uniformScale));
            height = std::max(1, static_cast<int>(static_cast<float>(baseHeight) * uniformScale));
        }
    }

    if (width > viewportWidth) {
        width = viewportWidth;
    }
    if (height > viewportHeight) {
        height = viewportHeight;
    }

    outWidth = width;
    outHeight = height;
}

int ResolveEyeZoomOutputWidth(const platform::config::EyeZoomConfig& zoomConfig,
                              int viewportWidth,
                              int viewportHeight,
                              const ModeViewportRect& modeViewport,
                              int outputX) {
    if (viewportWidth < 1 || viewportHeight < 1) {
        return 0;
    }

    if (zoomConfig.outputUseRelativeSize) {
        int width = 0;
        int height = 0;
        ResolveEyeZoomRelativeOutputSize(zoomConfig, viewportWidth, viewportHeight, modeViewport, width, height);
        (void)height;
        return width;
    }

    const std::string relativeTo = zoomConfig.outputRelativeTo.empty() ? "middleLeftScreen" : zoomConfig.outputRelativeTo;

    if (zoomConfig.stretchWidth > 0) {
        return std::min(viewportWidth, zoomConfig.stretchWidth);
    }

    int width = viewportWidth;
    if (modeViewport.valid && !ShouldUseViewportAnchor(relativeTo)) {
        const std::string anchorBase = GetAnchorBase(relativeTo);
        const int modeRight = modeViewport.x + modeViewport.width;
        if (IsLeftAlignedAnchor(anchorBase)) {
            width = modeViewport.x - outputX;
        } else if (IsRightAlignedAnchor(anchorBase)) {
            width = viewportWidth - modeRight - outputX;
        }
    }

    if (width < 1) {
        width = 1;
    }
    if (width > viewportWidth) {
        width = viewportWidth;
    }
    return width;
}

int ResolveEyeZoomOutputHeight(const platform::config::EyeZoomConfig& zoomConfig,
                               int viewportWidth,
                               int viewportHeight,
                               const ModeViewportRect& modeViewport) {
    if (viewportWidth < 1 || viewportHeight < 1) {
        return 0;
    }

    if (zoomConfig.outputUseRelativeSize) {
        int width = 0;
        int height = 0;
        ResolveEyeZoomRelativeOutputSize(zoomConfig, viewportWidth, viewportHeight, modeViewport, width, height);
        (void)width;
        return height;
    }

    int height = zoomConfig.outputHeight;
    if (height <= 0) {
        height = viewportHeight - (2 * zoomConfig.verticalMargin);
        int minHeight = static_cast<int>(0.2f * viewportHeight);
        if (height < minHeight) {
            height = minHeight;
        }
    }
    if (height > viewportHeight) {
        height = viewportHeight;
    }
    return height;
}

void* ResolveGlProc(const char* name) {
    if (!name) { return nullptr; }
    void* ptr = reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
    if (!ptr) { ptr = reinterpret_cast<void*>(glXGetProcAddress(reinterpret_cast<const GLubyte*>(name))); }
    return ptr;
}

bool EnsureGlFunctions() {
    if (g_glReady.load(std::memory_order_acquire)) { return true; }

    std::lock_guard<std::mutex> lock(g_glResolveMutex);
    if (g_glReady.load(std::memory_order_acquire)) { return true; }

    // FBO
    g_gl.genFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(ResolveGlProc("glGenFramebuffers"));
    g_gl.deleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(ResolveGlProc("glDeleteFramebuffers"));
    g_gl.bindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(ResolveGlProc("glBindFramebuffer"));
    g_gl.framebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(ResolveGlProc("glFramebufferTexture2D"));
    g_gl.blitFramebuffer = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(ResolveGlProc("glBlitFramebuffer"));
    g_gl.genRenderbuffers = reinterpret_cast<PFNGLGENRENDERBUFFERSPROC>(ResolveGlProc("glGenRenderbuffers"));
    g_gl.deleteRenderbuffers = reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(ResolveGlProc("glDeleteRenderbuffers"));
    g_gl.bindRenderbuffer = reinterpret_cast<PFNGLBINDRENDERBUFFERPROC>(ResolveGlProc("glBindRenderbuffer"));
    g_gl.renderbufferStorage = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEPROC>(ResolveGlProc("glRenderbufferStorage"));
    g_gl.framebufferRenderbuffer = reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(ResolveGlProc("glFramebufferRenderbuffer"));
    g_gl.checkFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(ResolveGlProc("glCheckFramebufferStatus"));

    // Shaders
    g_gl.createShader = reinterpret_cast<PFNGLCREATESHADERPROC>(ResolveGlProc("glCreateShader"));
    g_gl.shaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(ResolveGlProc("glShaderSource"));
    g_gl.compileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(ResolveGlProc("glCompileShader"));
    g_gl.getShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(ResolveGlProc("glGetShaderiv"));
    g_gl.getShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(ResolveGlProc("glGetShaderInfoLog"));
    g_gl.deleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(ResolveGlProc("glDeleteShader"));
    g_gl.createProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(ResolveGlProc("glCreateProgram"));
    g_gl.attachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(ResolveGlProc("glAttachShader"));
    g_gl.linkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(ResolveGlProc("glLinkProgram"));
    g_gl.useProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(ResolveGlProc("glUseProgram"));
    g_gl.deleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(ResolveGlProc("glDeleteProgram"));
    g_gl.getProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(ResolveGlProc("glGetProgramiv"));
    g_gl.getProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(ResolveGlProc("glGetProgramInfoLog"));

    // Uniforms
    g_gl.getUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(ResolveGlProc("glGetUniformLocation"));
    g_gl.uniform1i = reinterpret_cast<PFNGLUNIFORM1IPROC>(ResolveGlProc("glUniform1i"));
    g_gl.uniform1f = reinterpret_cast<PFNGLUNIFORM1FPROC>(ResolveGlProc("glUniform1f"));
    g_gl.uniform1fv = reinterpret_cast<PFNGLUNIFORM1FVPROC>(ResolveGlProc("glUniform1fv"));
    g_gl.uniform2f = reinterpret_cast<PFNGLUNIFORM2FPROC>(ResolveGlProc("glUniform2f"));
    g_gl.uniform3f = reinterpret_cast<PFNGLUNIFORM3FPROC>(ResolveGlProc("glUniform3f"));
    g_gl.uniform4f = reinterpret_cast<PFNGLUNIFORM4FPROC>(ResolveGlProc("glUniform4f"));
    g_gl.uniform3fv = reinterpret_cast<PFNGLUNIFORM3FVPROC>(ResolveGlProc("glUniform3fv"));
    g_gl.uniform4fv = reinterpret_cast<PFNGLUNIFORM4FVPROC>(ResolveGlProc("glUniform4fv"));

    // VAO/VBO
    g_gl.genVertexArrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(ResolveGlProc("glGenVertexArrays"));
    g_gl.deleteVertexArrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(ResolveGlProc("glDeleteVertexArrays"));
    g_gl.bindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(ResolveGlProc("glBindVertexArray"));
    g_gl.genBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(ResolveGlProc("glGenBuffers"));
    g_gl.deleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(ResolveGlProc("glDeleteBuffers"));
    g_gl.bindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(ResolveGlProc("glBindBuffer"));
    g_gl.bufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(ResolveGlProc("glBufferData"));
    g_gl.bufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(ResolveGlProc("glBufferSubData"));
    g_gl.mapBufferRange = reinterpret_cast<PFNGLMAPBUFFERRANGEPROC>(ResolveGlProc("glMapBufferRange"));
    g_gl.unmapBuffer = reinterpret_cast<PFNGLUNMAPBUFFERPROC>(ResolveGlProc("glUnmapBuffer"));
    g_gl.vertexAttribPointer = reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(ResolveGlProc("glVertexAttribPointer"));
    g_gl.enableVertexAttribArray = reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(ResolveGlProc("glEnableVertexAttribArray"));

    // Texture
    g_gl.activeTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(ResolveGlProc("glActiveTexture"));

    // Blend
    g_gl.blendFuncSeparate = reinterpret_cast<PFNGLBLENDFUNCSEPARATEPROC>(ResolveGlProc("glBlendFuncSeparate"));

    // Sync
    g_gl.fenceSync = reinterpret_cast<PFNGLFENCESYNCPROC>(ResolveGlProc("glFenceSync"));
    g_gl.clientWaitSync = reinterpret_cast<PFNGLCLIENTWAITSYNCPROC>(ResolveGlProc("glClientWaitSync"));
    g_gl.deleteSync = reinterpret_cast<PFNGLDELETESYNCPROC>(ResolveGlProc("glDeleteSync"));

    const bool ready =
        g_gl.genFramebuffers && g_gl.deleteFramebuffers && g_gl.bindFramebuffer &&
        g_gl.framebufferTexture2D && g_gl.blitFramebuffer &&
        g_gl.createShader && g_gl.shaderSource && g_gl.compileShader &&
        g_gl.getShaderiv && g_gl.getShaderInfoLog && g_gl.deleteShader &&
        g_gl.createProgram && g_gl.attachShader && g_gl.linkProgram &&
        g_gl.useProgram && g_gl.deleteProgram && g_gl.getProgramiv && g_gl.getProgramInfoLog &&
        g_gl.getUniformLocation && g_gl.uniform1i && g_gl.uniform1f && g_gl.uniform1fv &&
        g_gl.uniform2f && g_gl.uniform3f && g_gl.uniform4f && g_gl.uniform3fv && g_gl.uniform4fv &&
        g_gl.genVertexArrays && g_gl.deleteVertexArrays && g_gl.bindVertexArray &&
        g_gl.genBuffers && g_gl.deleteBuffers && g_gl.bindBuffer &&
        g_gl.bufferData && g_gl.bufferSubData && g_gl.mapBufferRange && g_gl.unmapBuffer &&
        g_gl.vertexAttribPointer && g_gl.enableVertexAttribArray &&
        g_gl.activeTexture && g_gl.blendFuncSeparate &&
        g_gl.fenceSync && g_gl.clientWaitSync && g_gl.deleteSync;

    if (ready && g_maxTextureSize == 0) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_maxTextureSize);
        if (g_maxTextureSize <= 0) { g_maxTextureSize = 16384; }
        if (IsDebugEnabled()) {
            fprintf(stderr, "[Linuxscreen][mirror] GL_MAX_TEXTURE_SIZE = %d\n", g_maxTextureSize);
        }
    }

    g_glReady.store(ready, std::memory_order_release);
    return ready;
}
