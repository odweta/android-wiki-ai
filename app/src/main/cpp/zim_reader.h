#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "zim_format.h"

namespace zim {

struct SearchResult {
    std::string title;
    std::string path;
    std::string snippet;
    std::string content; // full plain-text content, for prompt grounding
    float score = 0.0f;
};

// A small, read-only reader for the openZIM binary format. See zim_format.h
// for the scope/limitations of this implementation.
class Archive {
public:
    static std::unique_ptr<Archive> open(const std::string &path, std::string *error);
    ~Archive();

    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;

    // Returns up to maxResults articles ranked by meaningful title keywords
    // and title similarity, each with plain-text content already extracted.
    std::vector<SearchResult> search(const std::string &query, int maxResults);

    size_t articleCount() const { return titleIndex_.size(); }

private:
    struct TitleIndexEntry {
        std::string title;
        std::string path;
        std::string lowerTitle;
        uint32_t entryIndex = 0; // index into pathPointers_
    };

    Archive() = default;

    bool loadHeader(std::string *error);
    bool loadMimeTypes(std::string *error);
    bool loadPathPointers(std::string *error);
    bool loadClusterPointers(std::string *error);
    bool loadTitleIndex(std::string *error);
    bool readAt(uint64_t offset, void *buffer, size_t size);
    bool readDirentAt(uint64_t offset, DirectoryEntry *out);
    bool readDirentByIndex(uint32_t entryIndex, DirectoryEntry *out);
    // Resolves redirects (bounded hop count) and extracts + decompresses
    // the article content as plain text.
    std::string extractPlainText(uint32_t entryIndex, std::string *titleOut);
    std::string readClusterBlob(uint32_t clusterNumber, uint32_t blobNumber, bool *ok);
    bool decompressCluster(uint32_t clusterNumber, std::string *outBody, bool *outExtended);

    FILE *file_ = nullptr;
    const uint8_t *mappedData_ = nullptr;
    size_t mappedSize_ = 0;
    void *mapping_ = nullptr;
    uint64_t fileSize_ = 0;
    Header header_{};
    std::vector<std::string> mimeTypes_;
    std::vector<uint64_t> pathPointers_;
    std::vector<uint64_t> clusterPointers_;
    std::vector<TitleIndexEntry> titleIndex_;
    // Lowercased word -> indices into titleIndex_ for O(1) candidate lookup.
    std::unordered_map<std::string, std::vector<uint32_t>> wordIndex_;
};

} // namespace zim
