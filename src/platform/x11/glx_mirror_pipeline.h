#pragma once

#include "mirror/mirror_mode_state.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <cstdint>

namespace platform::x11 {

bool IsGlxMirrorPipelineEnabled();
void SubmitGlxMirrorCapture(int width, int height);
void RenderGlxMirrorOverlay(int viewportWidth, int viewportHeight);
void RenderGlxEyeZoomOverlay(int viewportWidth, int viewportHeight);
void ShutdownGlxMirrorPipeline();
void ShutdownGlxMirrorPipelineForProcessExit();

MirrorModeState& GetMirrorModeState();

void InitializeMirrorPipelineFromConfig();

GLuint GetOverscanFboId();

bool IsOverscanActive();

bool IsOverscanFboRendered();

void MarkOverscanFboRendered();

struct OverscanDimensions {
    int totalWidth = 0;
    int totalHeight = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    int marginLeft = 0;
    int marginRight = 0;
    int marginTop = 0;
    int marginBottom = 0;
};

OverscanDimensions GetOverscanDimensions();

bool UpdateOverscanState(int windowWidth, int windowHeight);

void SetViewportPlacementBypass(bool bypass);

void BlitOverscanToWindow(int dstX,
                          int dstY,
                          int dstWidth,
                          int dstHeight,
                          int surfaceWidth,
                          int surfaceHeight);

} // namespace platform::x11
