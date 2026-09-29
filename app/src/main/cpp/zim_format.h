#pragma once

// Minimal structures for the openZIM binary file format, as documented at
// https://wiki.openzim.org/wiki/ZIM_file_format
//
// This is NOT libzim: it is a small, purpose-built reader implementing only
// what this app needs (title listing, fuzzy/keyword search, article content
// extraction). It assumes a little-endian host (true for all supported
// Android ABIs: arm64-v8a, x86_64) and only supports uncompressed and
// Zstandard-compressed clusters; LZMA2/XZ compressed clusters (compression
// type 4) are reported as unsupported since vendoring liblzma was judged
// not worth the added build complexity for this app's scope.

#include <cstdint>
#include <string>

namespace zim {

constexpr uint32_t kMagicNumber = 72173914; // 0x044D495A

#pragma pack(push, 1)
struct Header {
    uint32_t magicNumber;
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint8_t uuid[16];
    uint32_t entryCount;
    uint32_t clusterCount;
    uint64_t pathPtrPos;
    uint64_t titlePtrPos;
    uint64_t clusterPtrPos;
    uint64_t mimeListPos;
    uint32_t mainPage;
    uint32_t layoutPage;
    uint64_t checksumPos;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 80, "ZIM header must be 80 bytes");

// Directory entry mimetype markers.
constexpr uint16_t kMimeRedirect = 0xffff;
constexpr uint16_t kMimeLinkTarget = 0xfffe;
constexpr uint16_t kMimeDeleted = 0xfffd;

// Cluster compression types (low 4 bits of the first cluster byte).
enum class ClusterCompression {
    kNone = 1,
    kLzma2 = 4,
    kZstd = 5,
    kUnknown = 0,
};

struct DirectoryEntry {
    uint16_t mimetype = 0;
    uint8_t nsChar = 0;
    bool isRedirect = false;
    uint32_t redirectIndex = 0;   // valid when isRedirect
    uint32_t clusterNumber = 0;   // valid when !isRedirect
    uint32_t blobNumber = 0;      // valid when !isRedirect
    std::string path;
    std::string title; // falls back to path when the stored title is empty
};

// Parses a directory entry starting at `data`, with `available` bytes
// readable from that pointer (enough to cover the fixed prefix and both
// zero-terminated strings). Returns false if the buffer was too short.
bool parseDirectoryEntry(const uint8_t *data, size_t available, DirectoryEntry *out);

} // namespace zim
