#include "zim_format.h"

#include <cstring>

namespace zim {

namespace {

std::string readCString(const uint8_t *data, size_t available, size_t *offset, bool *truncated) {
    size_t start = *offset;
    size_t end = start;
    while (end < available && data[end] != 0) {
        end++;
    }
    if (end >= available) {
        *truncated = true;
    }
    std::string result(reinterpret_cast<const char *>(data + start), end - start);
    *offset = (end < available) ? end + 1 : end;
    return result;
}

} // namespace

bool parseDirectoryEntry(const uint8_t *data, size_t available, DirectoryEntry *out) {
    if (available < 4) {
        return false;
    }
    uint16_t mimetype;
    std::memcpy(&mimetype, data + 0, 2);
    uint8_t nsChar = data[3];

    out->mimetype = mimetype;
    out->nsChar = nsChar;

    bool truncated = false;
    if (mimetype == kMimeRedirect) {
        if (available < 12) {
            return false;
        }
        out->isRedirect = true;
        uint32_t redirectIndex;
        std::memcpy(&redirectIndex, data + 8, 4);
        out->redirectIndex = redirectIndex;
        size_t offset = 12;
        out->path = readCString(data, available, &offset, &truncated);
        out->title = readCString(data, available, &offset, &truncated);
    } else {
        if (available < 16) {
            return false;
        }
        out->isRedirect = false;
        uint32_t clusterNumber;
        uint32_t blobNumber;
        std::memcpy(&clusterNumber, data + 8, 4);
        std::memcpy(&blobNumber, data + 12, 4);
        out->clusterNumber = clusterNumber;
        out->blobNumber = blobNumber;
        size_t offset = 16;
        out->path = readCString(data, available, &offset, &truncated);
        out->title = readCString(data, available, &offset, &truncated);
    }
    if (truncated) {
        return false;
    }
    if (out->title.empty()) {
        out->title = out->path;
    }
    return true;
}

} // namespace zim
