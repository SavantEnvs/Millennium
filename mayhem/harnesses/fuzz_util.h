#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unistd.h>

// Writes the input to a fresh file under $TMPDIR (default /tmp); returns its path, or "" on error.
static inline std::string write_temp_input(const uint8_t* data, size_t size, const char* suffix)
{
    const char* dir = getenv("TMPDIR");
    std::string path = std::string(dir && *dir ? dir : "/tmp") + "/mayhem-fuzz-XXXXXX" + suffix;
    int fd = mkstemps(path.data(), static_cast<int>(std::char_traits<char>::length(suffix)));
    if (fd < 0) return {};
    size_t off = 0;
    while (off < size) {
        ssize_t w = write(fd, data + off, size - off);
        if (w <= 0) { close(fd); unlink(path.c_str()); return {}; }
        off += static_cast<size_t>(w);
    }
    close(fd);
    return path;
}
