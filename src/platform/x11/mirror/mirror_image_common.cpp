namespace {

#ifndef __APPLE__
#ifndef PFNGLBINDBUFFERPROC
typedef void (*PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
#endif
#endif

struct DecodedImageFramesCommon {
    bool success = false;
    bool isAnimated = false;
    int width = 0;
    int dataHeight = 0;
    int frameHeight = 0;
    int frameCount = 1;
    std::vector<int> frameDelaysMs;
    unsigned char* pixelData = nullptr;
};

template <typename TState>
void ClearAnimatedImageGpuTextures(TState& state) {
    for (GLuint texture : state.frameTextures) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }
    state.frameTextures.clear();
    state.frameDelaysMs.clear();
    state.isAnimated = false;
    state.width = 0;
    state.height = 0;
    state.currentFrameIndex = 0;
    state.hasNextFrameTime = false;
}

#ifdef GL_PIXEL_UNPACK_BUFFER_BINDING
void BindPixelUnpackBuffer(GLuint buffer) {
#ifdef __APPLE__
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
#else
    static PFNGLBINDBUFFERPROC pfnBindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(
        platform::x11::ResolveCurrentGlProcAddress("glBindBuffer"));
    if (pfnBindBuffer) {
        pfnBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
    }
#endif
}
#endif

GLuint CreateRgbaTexture(int width, int height, const unsigned char* pixels) {
    if (width <= 0 || height <= 0 || !pixels) {
        return 0;
    }

    GLint prevTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding);
    GLint prevUnpackAlignment = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef GL_UNPACK_ROW_LENGTH
    GLint prevUnpackRowLength = 0;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevUnpackRowLength);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
#ifdef GL_UNPACK_SKIP_ROWS
    GLint prevUnpackSkipRows = 0;
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &prevUnpackSkipRows);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
#ifdef GL_UNPACK_SKIP_PIXELS
    GLint prevUnpackSkipPixels = 0;
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &prevUnpackSkipPixels);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
#endif
#ifdef GL_PIXEL_UNPACK_BUFFER_BINDING
    GLint prevUnpackBuffer = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &prevUnpackBuffer);
    BindPixelUnpackBuffer(0);
#endif

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
#ifdef GL_PIXEL_UNPACK_BUFFER_BINDING
        BindPixelUnpackBuffer(static_cast<GLuint>(prevUnpackBuffer));
#endif
#ifdef GL_UNPACK_SKIP_PIXELS
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, prevUnpackSkipPixels);
#endif
#ifdef GL_UNPACK_SKIP_ROWS
        glPixelStorei(GL_UNPACK_SKIP_ROWS, prevUnpackSkipRows);
#endif
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, prevUnpackRowLength);
#endif
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTextureBinding));
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#ifdef GL_PIXEL_UNPACK_BUFFER_BINDING
    BindPixelUnpackBuffer(static_cast<GLuint>(prevUnpackBuffer));
#endif
#ifdef GL_UNPACK_SKIP_PIXELS
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, prevUnpackSkipPixels);
#endif
#ifdef GL_UNPACK_SKIP_ROWS
    glPixelStorei(GL_UNPACK_SKIP_ROWS, prevUnpackSkipRows);
#endif
#ifdef GL_UNPACK_ROW_LENGTH
    glPixelStorei(GL_UNPACK_ROW_LENGTH, prevUnpackRowLength);
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTextureBinding));
    return texture;
}

bool HasGifExtension(const std::string& path) {
    if (path.size() < 4) {
        return false;
    }
    std::string extension = path.substr(path.size() - 4);
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".gif";
}

DecodedImageFramesCommon DecodeImageFramesCommon(const std::string& resolvedPath) {
    DecodedImageFramesCommon decoded;
    if (resolvedPath.empty()) {
        return decoded;
    }

    stbi_set_flip_vertically_on_load_thread(1);

    int width = 0;
    int height = 0;
    int channels = 0;
    int frameCount = 0;
    int* delays = nullptr;
    unsigned char* data = nullptr;

    if (HasGifExtension(resolvedPath)) {
        std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
        if (file) {
            const std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            if (size > 0) {
                std::vector<unsigned char> fileData(static_cast<std::size_t>(size));
                if (file.read(reinterpret_cast<char*>(fileData.data()), size)) {
                    data = stbi_load_gif_from_memory(fileData.data(),
                                                     static_cast<int>(fileData.size()),
                                                     &delays,
                                                     &width,
                                                     &height,
                                                     &frameCount,
                                                     &channels,
                                                     4);
                }
            }
        }

        if (!data) {
            frameCount = 0;
            data = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);
        }
    } else {
        data = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);
    }

    if (!data || width <= 0 || height <= 0) {
        if (data) {
            stbi_image_free(data);
        }
        if (delays) {
            stbi_image_free(delays);
        }
        return decoded;
    }

    decoded.success = true;
    decoded.pixelData = data;
    decoded.width = width;
    decoded.dataHeight = height;
    decoded.frameHeight = height;
    decoded.frameCount = 1;

    if (frameCount > 1 && delays) {
        decoded.isAnimated = true;
        decoded.frameCount = frameCount;
        decoded.frameHeight = height;
        decoded.dataHeight = height * frameCount;
        decoded.frameDelaysMs.reserve(static_cast<std::size_t>(frameCount));
        for (int i = 0; i < frameCount; ++i) {
            decoded.frameDelaysMs.push_back((delays[i] > 0) ? delays[i] : 100);
        }
    }

    if (delays) {
        stbi_image_free(delays);
    }
    return decoded;
}

template <typename TState>
bool UploadDecodedImageCommon(TState& state, DecodedImageFramesCommon& decoded) {
    ClearAnimatedImageGpuTextures(state);
    state.decodeFailed = !decoded.success || !decoded.pixelData;
    state.loading = false;
    state.currentFrameIndex = 0;
    state.hasNextFrameTime = false;

    if (!decoded.success || !decoded.pixelData) {
        return false;
    }

    state.decodeFailed = false;
    state.isAnimated = decoded.isAnimated;
    state.width = decoded.width;
    state.height = decoded.isAnimated ? decoded.frameHeight : decoded.dataHeight;

    if (decoded.isAnimated && decoded.frameCount > 1 && decoded.frameHeight > 0) {
        const std::size_t frameByteSize =
            static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.frameHeight) * 4u;
        state.frameTextures.reserve(static_cast<std::size_t>(decoded.frameCount));
        state.frameDelaysMs = decoded.frameDelaysMs;
        if (state.frameDelaysMs.size() < static_cast<std::size_t>(decoded.frameCount)) {
            state.frameDelaysMs.resize(static_cast<std::size_t>(decoded.frameCount), 100);
        }
        for (int frame = 0; frame < decoded.frameCount; ++frame) {
            const unsigned char* framePixels = decoded.pixelData + (static_cast<std::size_t>(frame) * frameByteSize);
            GLuint texture = CreateRgbaTexture(decoded.width, decoded.frameHeight, framePixels);
            if (texture != 0) {
                state.frameTextures.push_back(texture);
            }
        }
    } else {
        GLuint texture = CreateRgbaTexture(decoded.width, decoded.dataHeight, decoded.pixelData);
        if (texture != 0) {
            state.frameTextures.push_back(texture);
        }
    }

    stbi_image_free(decoded.pixelData);
    decoded.pixelData = nullptr;
    return !state.frameTextures.empty();
}

template <typename TState>
GLuint GetAnimatedImageTexture(TState& state) {
    if (state.frameTextures.empty()) {
        return 0;
    }
    if (!state.isAnimated || state.frameTextures.size() == 1) {
        return state.frameTextures.front();
    }

    const auto now = std::chrono::steady_clock::now();
    if (!state.hasNextFrameTime) {
        const int delayMs = (state.frameDelaysMs.empty() ? 100 : std::max(1, state.frameDelaysMs[state.currentFrameIndex]));
        state.nextFrameTime = now + std::chrono::milliseconds(delayMs);
        state.hasNextFrameTime = true;
    } else if (now >= state.nextFrameTime) {
        int safety = 0;
        while (now >= state.nextFrameTime && safety < 32) {
            state.currentFrameIndex = (state.currentFrameIndex + 1) % static_cast<int>(state.frameTextures.size());
            const int delayMs = (state.frameDelaysMs.empty()
                                     ? 100
                                     : std::max(1, state.frameDelaysMs[static_cast<std::size_t>(state.currentFrameIndex)]));
            state.nextFrameTime += std::chrono::milliseconds(delayMs);
            ++safety;
        }
    }

    const std::size_t frameIndex = static_cast<std::size_t>(state.currentFrameIndex);
    if (frameIndex >= state.frameTextures.size()) {
        return state.frameTextures.front();
    }
    return state.frameTextures[frameIndex];
}

} // namespace
