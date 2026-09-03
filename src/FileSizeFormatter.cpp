#include "FileSizeFormatter.hpp"

#include <array>
#include <cstddef>


/**
 * formatFileSize()
 * Formats a byte count using human-readable binary file-size units.
 */
QString formatFileSize(std::uint64_t bytes) {
    static constexpr std::array<const char *, 7> UNITS {
        "bytes",
        "KB",
        "MB",
        "GB",
        "TB",
        "PB",
        "EB"
    };

    if(bytes < 1000) {
        if(bytes == 1) {
            return "1 byte";
        }

        return QString::number(bytes) + " bytes";
    }

    double value = static_cast<double>(bytes);
    std::size_t unitIndex = 0;

    while(value >= 1000.0 && unitIndex < UNITS.size() - 1) {
        value /= 1000.0;
        ++unitIndex;
    }

    return QString::number(value, 'f', 2) + " " + UNITS[unitIndex];
}
