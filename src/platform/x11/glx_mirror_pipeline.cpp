#include "mirror_image_source.h"

#include "mirror/mirror_runtime.cpp"
#include "mirror/mirror_shaders.cpp"
#include "mirror/mirror_image_common.cpp"
#include "mirror/mirror_worker.cpp"
#include "mirror/mirror_background_images.cpp"
#include "mirror/mirror_overscan.cpp"
#include "mirror/mirror_mac_redirect.cpp"
#include "mirror/mirror_capture.cpp"
#include "mirror/mirror_overlay_render.cpp"

namespace platform::x11 {

bool IsGlxMirrorPipelineEnabled() {
    return IsGlxMirrorPipelineEnabledInternal();
}

void SubmitGlxMirrorCapture(int width, int height) {
    SubmitGlxMirrorCaptureInternal(width, height);
}

void RecordPresentedGameTexture(GLuint texture, int width, int height) {
    RecordPresentedGameTextureInternal(texture, width, height);
}

bool GetPresentedGameTexture(GLuint& texture, int& width, int& height) {
    return GetPresentedGameTextureInternal(texture, width, height);
}

void RecordPresentedGameFramebuffer(GLuint framebuffer, int width, int height, GLenum readBuffer) {
    RecordPresentedGameFramebufferInternal(framebuffer, width, height, readBuffer);
}

bool GetPresentedGameFramebuffer(GLuint& framebuffer, int& width, int& height) {
    return GetPresentedGameFramebufferInternal(framebuffer, width, height);
}

void RecordPhysicalModeResizeTarget(int targetWidth, int targetHeight, int basisWidth, int basisHeight) {
    RecordPhysicalModeResizeTargetInternal(targetWidth, targetHeight, basisWidth, basisHeight);
}

GLuint GetOverscanFboId() {
    return GetOverscanFboIdInternal();
}

GLuint GetMacMirrorRedirectFboId() {
    return GetMacMirrorRedirectFboIdInternal();
}

bool GetMacMirrorRedirectSize(int& width, int& height) {
    return GetMacMirrorRedirectSizeInternal(width, height);
}

bool IsOverscanActive() {
    return IsOverscanActiveInternal();
}

bool IsMacMirrorRedirectActive() {
    return IsMacMirrorRedirectActiveInternal();
}

OverscanDimensions GetOverscanDimensions() {
    return GetOverscanDimensionsInternal();
}

bool IsOverscanFboRendered() {
    return IsOverscanFboRenderedInternal();
}

bool IsMacMirrorRedirectRendered() {
    return IsMacMirrorRedirectRenderedInternal();
}

void MarkOverscanFboRendered() {
    MarkOverscanFboRenderedInternal();
}

void MarkMacMirrorRedirectRendered() {
    MarkMacMirrorRedirectRenderedInternal();
}

void CaptureDefaultFramebufferToMacMirrorRedirectIfNeeded() {
    CaptureDefaultFramebufferToMacMirrorRedirectIfNeededInternal();
}

bool UpdateOverscanState(int windowWidth, int windowHeight) {
    return UpdateOverscanStateInternal(windowWidth, windowHeight);
}

bool UpdateMacMirrorRedirectState(int windowWidth, int windowHeight) {
    return UpdateMacMirrorRedirectStateInternal(windowWidth, windowHeight);
}

void BlitOverscanToWindow(int dstX,
                          int dstY,
                          int dstWidth,
                          int dstHeight,
                          int surfaceWidth,
                          int surfaceHeight) {
    BlitOverscanToWindowInternal(dstX, dstY, dstWidth, dstHeight, surfaceWidth, surfaceHeight);
}

void BlitMacMirrorRedirectToWindow(int dstX,
                                   int dstY,
                                   int dstWidth,
                                   int dstHeight,
                                   int surfaceWidth,
                                   int surfaceHeight) {
    BlitMacMirrorRedirectToWindowInternal(dstX, dstY, dstWidth, dstHeight, surfaceWidth, surfaceHeight);
}

} // namespace platform::x11
