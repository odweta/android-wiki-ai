#pragma once

// Small header-only string similarity helpers used for fuzzy title/content
// matching. Deliberately simple (no external dependency) since inputs are
// short (titles, query words).

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace text_similarity {

inline std::string toLowerAscii(const std::string &s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

inline std::vector<std::string> tokenizeWords(const std::string &lowered) {
    std::vector<std::string> words;
    std::string current;
    for (unsigned char c : lowered) {
        if (std::isalnum(c)) {
            current.push_back(static_cast<char>(c));
        } else if (!current.empty()) {
            words.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

// Classic Levenshtein edit distance, O(n*m) time/space (fine for short
// strings such as titles and query words, not intended for long text).
inline int levenshteinDistance(const std::string &a, const std::string &b) {
    const size_t n = a.size();
    const size_t m = b.size();
    if (n == 0) return static_cast<int>(m);
    if (m == 0) return static_cast<int>(n);

    std::vector<int> previous(m + 1);
    std::vector<int> current(m + 1);
    for (size_t j = 0; j <= m; j++) {
        previous[j] = static_cast<int>(j);
    }
    for (size_t i = 1; i <= n; i++) {
        current[0] = static_cast<int>(i);
        for (size_t j = 1; j <= m; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        std::swap(previous, current);
    }
    return previous[m];
}

// Normalized similarity in [0, 1], 1 meaning identical strings.
inline float levenshteinSimilarity(const std::string &a, const std::string &b) {
    if (a.empty() && b.empty()) return 1.0f;
    int distance = levenshteinDistance(a, b);
    size_t maxLen = std::max(a.size(), b.size());
    if (maxLen == 0) return 1.0f;
    return 1.0f - static_cast<float>(distance) / static_cast<float>(maxLen);
}

// Jaro-Winkler similarity in [0, 1]; complements Levenshtein for catching
// typo-tolerant partial/prefix matches on short strings like titles.
inline float jaroWinklerSimilarity(const std::string &s1, const std::string &s2) {
    if (s1.empty() && s2.empty()) return 1.0f;
    if (s1.empty() || s2.empty()) return 0.0f;

    const int len1 = static_cast<int>(s1.size());
    const int len2 = static_cast<int>(s2.size());
    const int matchDistance = std::max(len1, len2) / 2 - 1;

    std::vector<bool> s1Matches(len1, false);
    std::vector<bool> s2Matches(len2, false);

    int matches = 0;
    for (int i = 0; i < len1; i++) {
        int start = std::max(0, i - matchDistance);
        int end = std::min(i + matchDistance + 1, len2);
        for (int j = start; j < end; j++) {
            if (s2Matches[j] || s1[i] != s2[j]) continue;
            s1Matches[i] = true;
            s2Matches[j] = true;
            matches++;
            break;
        }
    }
    if (matches == 0) return 0.0f;

    double transpositions = 0;
    int k = 0;
    for (int i = 0; i < len1; i++) {
        if (!s1Matches[i]) continue;
        while (!s2Matches[k]) k++;
        if (s1[i] != s2[k]) transpositions++;
        k++;
    }
    transpositions /= 2.0;

    double m = matches;
    double jaro = (m / len1 + m / len2 + (m - transpositions) / m) / 3.0;

    int prefix = 0;
    int maxPrefix = std::min(4, std::min(len1, len2));
    while (prefix < maxPrefix && s1[prefix] == s2[prefix]) prefix++;

    double winkler = jaro + prefix * 0.1 * (1.0 - jaro);
    return static_cast<float>(winkler);
}

} // namespace text_similarity
