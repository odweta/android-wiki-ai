#include "zim_reader.h"

#include <android/log.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "html_strip.h"
#include "text_similarity.h"

#define LOG_TAG "ZimReader"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace zim {

namespace {
constexpr int kMaxRedirectHops = 6;
constexpr size_t kMaxContentBytesForScoring = 20000;
constexpr size_t kMaxDecompressedClusterBytes = 128 * 1024 * 1024;
}

std::unique_ptr<Archive> Archive::open(const std::string &path, std::string *error) {
    std::unique_ptr<Archive> archive(new Archive());
    archive->file_ = fopen(path.c_str(), "rb");
    if (!archive->file_) {
        *error = "Could not open ZIM file.";
        return nullptr;
    }
    struct stat fileStat {};
    if (fstat(fileno(archive->file_), &fileStat) != 0 || fileStat.st_size <= 0 ||
        static_cast<uint64_t>(fileStat.st_size) > std::numeric_limits<size_t>::max()) {
        *error = "Could not determine the ZIM file size.";
        return nullptr;
    }
    archive->fileSize_ = static_cast<uint64_t>(fileStat.st_size);
    archive->mappedSize_ = static_cast<size_t>(archive->fileSize_);
    archive->mapping_ = mmap(nullptr, archive->mappedSize_, PROT_READ, MAP_PRIVATE,
                             fileno(archive->file_), 0);
    if (archive->mapping_ == MAP_FAILED) {
        archive->mapping_ = nullptr;
        *error = "Could not map the ZIM file for fast reading.";
        return nullptr;
    }
    archive->mappedData_ = static_cast<const uint8_t *>(archive->mapping_);

    if (!archive->loadHeader(error)) return nullptr;
    if (!archive->loadMimeTypes(error)) return nullptr;
    if (!archive->loadPathPointers(error)) return nullptr;
    if (!archive->loadClusterPointers(error)) return nullptr;
    if (!archive->loadTitleIndex(error)) return nullptr;
    return archive;
}

Archive::~Archive() {
    if (mapping_) munmap(mapping_, mappedSize_);
    if (file_) fclose(file_);
}

bool Archive::readAt(uint64_t offset, void *buffer, size_t size) {
    if (offset > fileSize_ || size > fileSize_ - offset) return false;
    std::memcpy(buffer, mappedData_ + offset, size);
    return true;
}

bool Archive::loadHeader(std::string *error) {
    if (!readAt(0, &header_, sizeof(Header))) {
        *error = "ZIM file is too small to contain a header.";
        return false;
    }
    if (header_.magicNumber != kMagicNumber) {
        *error = "Not a ZIM file (magic number mismatch).";
        return false;
    }
    if (header_.majorVersion > 6) {
        LOGW("ZIM major version %u is newer than tested; attempting best-effort read.",
             header_.majorVersion);
    }
    return true;
}

bool Archive::loadMimeTypes(std::string *error) {
    uint64_t pos = header_.mimeListPos;
    // The MIME list is a sequence of zero-terminated strings ending with an
    // empty string; read it in chunks until we find the terminator.
    std::string buffer;
    const size_t kChunk = 4096;
    while (true) {
        size_t oldSize = buffer.size();
        buffer.resize(oldSize + kChunk);
        size_t toRead = std::min<uint64_t>(kChunk, fileSize_ - (pos + oldSize));
        if (toRead == 0) break;
        if (!readAt(pos + oldSize, &buffer[oldSize], toRead)) {
            buffer.resize(oldSize);
            break;
        }
        buffer.resize(oldSize + toRead);
        // Stop once we've seen "\0\0" (empty terminating entry) or hit EOF.
        if (buffer.find(std::string("\0\0", 2), oldSize > 0 ? oldSize - 1 : 0) != std::string::npos) {
            break;
        }
        if (toRead < kChunk) break;
    }

    size_t i = 0;
    while (i < buffer.size()) {
        size_t end = buffer.find('\0', i);
        if (end == std::string::npos) break;
        if (end == i) break; // empty string marks the end of the list
        mimeTypes_.push_back(buffer.substr(i, end - i));
        i = end + 1;
    }
    return true;
}

bool Archive::loadPathPointers(std::string *error) {
    pathPointers_.resize(header_.entryCount);
    if (header_.entryCount == 0) return true;
    if (!readAt(header_.pathPtrPos, pathPointers_.data(), header_.entryCount * sizeof(uint64_t))) {
        *error = "Failed to read the ZIM path pointer list.";
        return false;
    }
    return true;
}

bool Archive::loadClusterPointers(std::string *error) {
    clusterPointers_.resize(header_.clusterCount);
    if (header_.clusterCount == 0) return true;
    if (!readAt(header_.clusterPtrPos, clusterPointers_.data(),
                header_.clusterCount * sizeof(uint64_t))) {
        *error = "Failed to read the ZIM cluster pointer list.";
        return false;
    }
    return true;
}

bool Archive::readDirentAt(uint64_t offset, DirectoryEntry *out) {
    if (offset >= fileSize_) return false;
    size_t available = static_cast<size_t>(
        std::min<uint64_t>(fileSize_ - offset, 65536));
    return parseDirectoryEntry(mappedData_ + offset, available, out);
}

bool Archive::readDirentByIndex(uint32_t entryIndex, DirectoryEntry *out) {
    if (entryIndex >= pathPointers_.size()) return false;
    return readDirentAt(pathPointers_[entryIndex], out);
}

bool Archive::loadTitleIndex(std::string *error) {
    titleIndex_.reserve(header_.entryCount);
    for (uint32_t i = 0; i < header_.entryCount; i++) {
        DirectoryEntry entry;
        if (!readDirentByIndex(i, &entry)) {
            continue; // skip unreadable/malformed entries rather than failing the whole archive
        }
        bool isArticle = false;
        if (entry.isRedirect) {
            isArticle = true;
        } else if (entry.mimetype < mimeTypes_.size()) {
            const std::string &mime = mimeTypes_[entry.mimetype];
            isArticle = mime.rfind("text/html", 0) == 0;
        }
        if (!isArticle || entry.title.empty()) continue;

        TitleIndexEntry indexEntry;
        indexEntry.title = entry.title;
        indexEntry.path = entry.path;
        indexEntry.lowerTitle = text_similarity::toLowerAscii(entry.title);
        indexEntry.entryIndex = i;

        uint32_t position = static_cast<uint32_t>(titleIndex_.size());
        for (const auto &word : text_similarity::tokenizeWords(indexEntry.lowerTitle)) {
            wordIndex_[word].push_back(position);
        }
        titleIndex_.push_back(std::move(indexEntry));
    }
    if (titleIndex_.empty()) {
        *error = "The ZIM file has no readable HTML articles.";
        return false;
    }
    return true;
}

bool Archive::decompressCluster(uint32_t clusterNumber, std::string *outBody, bool *outExtended) {
    if (clusterNumber >= clusterPointers_.size()) return false;
    uint64_t start = clusterPointers_[clusterNumber];
    uint64_t end = (clusterNumber + 1 < clusterPointers_.size())
                       ? clusterPointers_[clusterNumber + 1]
                       : header_.checksumPos;
    if (end <= start || end - start < 1) return false;

    std::string raw(end - start, '\0');
    if (!readAt(start, &raw[0], raw.size())) return false;

    uint8_t infoByte = static_cast<uint8_t>(raw[0]);
    int compressionType = infoByte & 0x0F;
    *outExtended = (infoByte & 0x10) != 0;
    std::string compressed = raw.substr(1);

    if (compressionType == static_cast<int>(ClusterCompression::kNone)) {
        *outBody = std::move(compressed);
        return true;
    }
    if (compressionType == static_cast<int>(ClusterCompression::kZstd)) {
        ZSTD_DStream *stream = ZSTD_createDStream();
        if (!stream) return false;
        size_t initResult = ZSTD_initDStream(stream);
        if (ZSTD_isError(initResult)) {
            LOGW("Cluster %u: could not initialize zstd decompression: %s", clusterNumber,
                 ZSTD_getErrorName(initResult));
            ZSTD_freeDStream(stream);
            return false;
        }

        ZSTD_inBuffer input{compressed.data(), compressed.size(), 0};
        std::array<char, 64 * 1024> chunk{};
        std::string decompressed;
        size_t remaining = 1;
        while (input.pos < input.size || remaining != 0) {
            ZSTD_outBuffer output{chunk.data(), chunk.size(), 0};
            remaining = ZSTD_decompressStream(stream, &output, &input);
            if (ZSTD_isError(remaining)) {
                LOGW("Cluster %u: zstd decompression failed: %s", clusterNumber,
                     ZSTD_getErrorName(remaining));
                ZSTD_freeDStream(stream);
                return false;
            }
            if (output.pos > kMaxDecompressedClusterBytes - decompressed.size()) {
                LOGW("Cluster %u: decompressed data exceeds the safety limit.", clusterNumber);
                ZSTD_freeDStream(stream);
                return false;
            }
            decompressed.append(chunk.data(), output.pos);
            if (input.pos == input.size && output.pos == 0 && remaining != 0) {
                LOGW("Cluster %u: incomplete zstd frame.", clusterNumber);
                ZSTD_freeDStream(stream);
                return false;
            }
        }
        ZSTD_freeDStream(stream);
        *outBody = std::move(decompressed);
        return true;
    }
    // LZMA2/XZ (type 4) clusters are not supported by this lightweight
    // reader; see zim_format.h for the rationale. Skip gracefully.
    LOGW("Cluster %u uses unsupported compression type %d (LZMA2/XZ); skipping.",
         clusterNumber, compressionType);
    return false;
}

std::string Archive::readClusterBlob(uint32_t clusterNumber, uint32_t blobNumber, bool *ok) {
    *ok = false;
    std::string body;
    bool extended = false;
    if (!decompressCluster(clusterNumber, &body, &extended)) return "";

    size_t offsetSize = extended ? 8 : 4;
    if (body.size() < offsetSize) return "";

    auto readOffset = [&](size_t index) -> uint64_t {
        uint64_t value = 0;
        std::memcpy(&value, body.data() + index * offsetSize, offsetSize);
        return value;
    };

    uint64_t firstOffset = readOffset(0);
    size_t offsetCount = static_cast<size_t>(firstOffset / offsetSize);
    if (blobNumber + 1 >= offsetCount) return "";

    uint64_t blobStart = readOffset(blobNumber);
    uint64_t blobEnd = readOffset(blobNumber + 1);
    if (blobEnd < blobStart || blobEnd > body.size()) return "";

    *ok = true;
    return body.substr(blobStart, blobEnd - blobStart);
}

std::string Archive::extractPlainText(uint32_t entryIndex, std::string *titleOut) {
    DirectoryEntry entry;
    uint32_t current = entryIndex;
    for (int hop = 0; hop < kMaxRedirectHops; hop++) {
        if (!readDirentByIndex(current, &entry)) return "";
        if (!entry.isRedirect) break;
        current = entry.redirectIndex;
        if (hop == kMaxRedirectHops - 1) return "";
    }
    if (titleOut) *titleOut = entry.title;
    if (entry.isRedirect) return ""; // redirect loop guard exhausted
    if (entry.mimetype >= mimeTypes_.size() ||
        mimeTypes_[entry.mimetype].rfind("text/html", 0) != 0) {
        return "";
    }
    bool ok = false;
    std::string html = readClusterBlob(entry.clusterNumber, entry.blobNumber, &ok);
    if (!ok) return "";
    return html_strip::toPlainText(html);
}

std::vector<SearchResult> Archive::search(const std::string &query, int maxResults) {
    using text_similarity::jaroWinklerSimilarity;
    using text_similarity::levenshteinSimilarity;
    using text_similarity::toLowerAscii;
    using text_similarity::tokenizeWords;

    std::vector<SearchResult> results;
    std::string lowerQuery = toLowerAscii(query);
    static const std::unordered_map<std::string, bool> stopWords = {
        {"a", true}, {"an", true}, {"and", true}, {"are", true}, {"as", true},
        {"at", true}, {"be", true}, {"by", true}, {"can", true}, {"could", true},
        {"do", true}, {"does", true}, {"for", true}, {"from", true}, {"give", true},
        {"how", true}, {"i", true}, {"in", true}, {"is", true}, {"it", true},
        {"me", true}, {"of", true}, {"on", true}, {"or", true}, {"please", true},
        {"should", true}, {"tell", true}, {"that", true}, {"the", true}, {"this", true},
        {"to", true}, {"was", true}, {"what", true}, {"when", true}, {"where", true},
        {"which", true}, {"who", true}, {"why", true}, {"with", true}, {"would", true},
        {"you", true}, {"your", true}, {"definition", true}, {"define", true},
        {"explain", true}, {"tutorial", true}, {"guide", true}, {"step", true},
        {"steps", true}, {"stop", true}, {"prevent", true}, {"treat", true},
    };
    std::vector<std::string> queryWords;
    for (const auto &word : tokenizeWords(lowerQuery)) {
        if (word.size() > 1 && stopWords.find(word) == stopWords.end()) {
            queryWords.push_back(word);
        }
    }
    if (queryWords.empty() || titleIndex_.empty()) return results;

    // Pass 1: cheap candidate gathering via exact meaningful-word matches.
    // Avoid a full vocabulary/title scan for unmatched words on large archives.
    std::unordered_map<uint32_t, float> candidateScores;
    for (const auto &word : queryWords) {
        auto exact = wordIndex_.find(word);
        if (exact == wordIndex_.end()) continue;
        for (uint32_t idx : exact->second) {
            candidateScores[idx] += 1.0f;
        }
    }

    if (candidateScores.empty()) return results;

    // Pass 2: precise re-scoring on the (bounded) candidate set, combining
    // word overlap with whole-string fuzzy similarity.
    struct Scored {
        uint32_t idx;
        float score;
    };
    std::vector<Scored> scored;
    scored.reserve(candidateScores.size());
    for (const auto &[idx, cheapScore] : candidateScores) {
        const auto &titleEntry = titleIndex_[idx];
        float fuzzy = std::max(levenshteinSimilarity(lowerQuery, titleEntry.lowerTitle),
                                jaroWinklerSimilarity(lowerQuery, titleEntry.lowerTitle));
        float wordOverlap = cheapScore / static_cast<float>(queryWords.size());
        float substringBonus =
            titleEntry.lowerTitle.find(lowerQuery) != std::string::npos ? 0.25f : 0.0f;
        float combined = 0.5f * std::min(wordOverlap, 1.0f) + 0.35f * fuzzy + substringBonus;
        scored.push_back({idx, combined});
    }
    std::sort(scored.begin(), scored.end(),
              [](const Scored &a, const Scored &b) { return a.score > b.score; });

    // Pass 3: fetch content for the best candidates and refine the score
    // with content keyword overlap; this is the expensive step so it is
    // bounded to a small shortlist.
    int contentShortlist = std::min<int>(static_cast<int>(scored.size()), std::max(maxResults * 2, 4));
    std::vector<SearchResult> refined;
    for (int i = 0; i < contentShortlist; i++) {
        const auto &titleEntry = titleIndex_[scored[i].idx];
        std::string resolvedTitle;
        std::string content = extractPlainText(titleEntry.entryIndex, &resolvedTitle);

        float finalScore = scored[i].score;
        std::string snippet;
        if (!content.empty()) {
            std::string lowerContent =
                toLowerAscii(content.substr(0, std::min(content.size(), kMaxContentBytesForScoring)));
            int hits = 0;
            for (const auto &word : queryWords) {
                if (lowerContent.find(word) != std::string::npos) hits++;
            }
            float contentOverlap = static_cast<float>(hits) / static_cast<float>(queryWords.size());
            finalScore = 0.65f * scored[i].score + 0.35f * contentOverlap;

            size_t firstHit = std::string::npos;
            for (const auto &word : queryWords) {
                size_t pos = lowerContent.find(word);
                if (pos != std::string::npos && (firstHit == std::string::npos || pos < firstHit)) {
                    firstHit = pos;
                }
            }
            size_t snippetStart = firstHit == std::string::npos
                                       ? 0
                                       : (firstHit > 80 ? firstHit - 80 : 0);
            snippet = content.substr(snippetStart, 280);
        }

        SearchResult result;
        result.title = resolvedTitle.empty() ? titleEntry.title : resolvedTitle;
        result.path = titleEntry.path;
        result.content = content;
        result.snippet = snippet.empty() ? result.title : snippet;
        result.score = finalScore;
        refined.push_back(std::move(result));
    }

    std::sort(refined.begin(), refined.end(),
              [](const SearchResult &a, const SearchResult &b) { return a.score > b.score; });

    // Fail closed: drop anything that isn't at least a plausible match.
    constexpr float kMinimumRelevance = 0.42f;
    for (auto &result : refined) {
        if (result.score < kMinimumRelevance) continue;
        results.push_back(std::move(result));
        if (static_cast<int>(results.size()) >= maxResults) break;
    }
    return results;
}

} // namespace zim
