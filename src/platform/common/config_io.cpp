#include "config/io_defaults.cpp"
#include "config/io_save_thread.cpp"
#include "config/io_paths.cpp"
#include "config/io_runtime.cpp"
#include "config/io_theme.cpp"

namespace platform::config {

LinuxscreenConfig LoadEmbeddedDefaultConfig() {
    return LoadEmbeddedDefaultConfigImpl();
}

} // namespace platform::config
