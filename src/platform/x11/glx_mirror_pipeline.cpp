#include "mirror/mirror_runtime.cpp"
#include "mirror/mirror_shaders.cpp"
#include "mirror/mirror_worker.cpp"
#include "mirror/mirror_background_images.cpp"
#include "mirror/mirror_overscan.cpp"
#include "mirror/mirror_capture.cpp"
#include "mirror/mirror_overlay_render.cpp"

namespace platform::x11 {

bool IsGlxMirrorPipelineEnabled() {
    return IsGlxMirrorPipelineEnabledInternal();
}

void SubmitGlxMirrorCapture(int width, int height) {
    SubmitGlxMirrorCaptureInternal(width, height);
}

GLuint GetOverscanFboId() {
    return GetOverscanFboIdInternal();
}

bool IsOverscanActive() {
    return IsOverscanActiveInternal();
}

OverscanDimensions GetOverscanDimensions() {
    return GetOverscanDimensionsInternal();
}

bool IsOverscanFboRendered() {
    return IsOverscanFboRenderedInternal();
}

void MarkOverscanFboRendered() {
    MarkOverscanFboRenderedInternal();
}

bool UpdateOverscanState(int windowWidth, int windowHeight) {
    return UpdateOverscanStateInternal(windowWidth, windowHeight);
}

void BlitOverscanToWindow(int dstX,
                          int dstY,
                          int dstWidth,
                          int dstHeight,
                          int surfaceWidth,
                          int surfaceHeight) {
    BlitOverscanToWindowInternal(dstX, dstY, dstWidth, dstHeight, surfaceWidth, surfaceHeight);
}

} // namespace platform::x11
