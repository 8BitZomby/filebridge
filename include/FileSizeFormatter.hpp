#ifndef FILEBRIDGE_FILE_SIZE_FORMATTER_HPP
#define FILEBRIDGE_FILE_SIZE_FORMATTER_HPP

#include <QString>

#include <cstdint>


/**
 * formatFileSize()
 * Formats a byte count using human-readable binary file-size units.
 */
QString formatFileSize(std::uint64_t bytes);


#endif
