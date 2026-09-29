#include "html_strip.h"

#include <cctype>
#include <unordered_map>

namespace html_strip {

namespace {

const std::unordered_map<std::string, std::string> &entityTable() {
    static const std::unordered_map<std::string, std::string> table = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""},
        {"apos", "'"}, {"nbsp", " "}, {"mdash", "-"}, {"ndash", "-"},
        {"rsquo", "'"}, {"lsquo", "'"}, {"rdquo", "\""}, {"ldquo", "\""},
    };
    return table;
}

// Decodes a single &entity; starting at html[pos] (which must point at '&').
// Returns the decoded text and advances pos past the ';'. If the entity is
// not recognized, returns "&" and advances by one.
std::string decodeEntityAt(const std::string &html, size_t *pos) {
    size_t start = *pos;
    size_t semi = html.find(';', start);
    if (semi == std::string::npos || semi - start > 10) {
        *pos = start + 1;
        return "&";
    }
    std::string name = html.substr(start + 1, semi - start - 1);
    *pos = semi + 1;
    if (!name.empty() && name[0] == '#') {
        try {
            int codepoint = (name.size() > 1 && (name[1] == 'x' || name[1] == 'X'))
                                 ? std::stoi(name.substr(2), nullptr, 16)
                                 : std::stoi(name.substr(1));
            if (codepoint < 0x80) {
                return std::string(1, static_cast<char>(codepoint));
            }
            // Minimal UTF-8 encoding for the common BMP range; good enough
            // for readable prompt excerpts without a full entity decoder.
            std::string out;
            if (codepoint < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            return out;
        } catch (...) {
            return "";
        }
    }
    const auto &table = entityTable();
    auto it = table.find(name);
    return it != table.end() ? it->second : "";
}

} // namespace

std::string toPlainText(const std::string &html) {
    std::string text;
    text.reserve(html.size() / 2);

    size_t i = 0;
    const size_t n = html.size();
    while (i < n) {
        if (html[i] == '<') {
            // Detect <script ...>...</script> and <style ...>...</style>
            // and drop their contents entirely.
            auto matchesTagStart = [&](const char *tag) {
                size_t len = std::char_traits<char>::length(tag);
                if (i + 1 + len > n) return false;
                for (size_t k = 0; k < len; k++) {
                    if (std::tolower(static_cast<unsigned char>(html[i + 1 + k])) != tag[k]) {
                        return false;
                    }
                }
                return true;
            };
            if (matchesTagStart("script") || matchesTagStart("style")) {
                bool isScript = matchesTagStart("script");
                size_t closeTagPos = html.find(isScript ? "</script" : "</style", i);
                if (closeTagPos == std::string::npos) {
                    break; // malformed; stop processing
                }
                size_t afterClose = html.find('>', closeTagPos);
                i = (afterClose == std::string::npos) ? n : afterClose + 1;
                continue;
            }
            size_t close = html.find('>', i);
            if (close == std::string::npos) {
                break;
            }
            // Block-level tags become whitespace so words don't run together.
            text.push_back(' ');
            i = close + 1;
        } else if (html[i] == '&') {
            text += decodeEntityAt(html, &i);
        } else {
            text.push_back(html[i]);
            i++;
        }
    }

    // Collapse consecutive whitespace.
    std::string collapsed;
    collapsed.reserve(text.size());
    bool lastWasSpace = false;
    for (char c : text) {
        bool isSpace = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (isSpace) {
            if (!lastWasSpace) {
                collapsed.push_back(' ');
            }
        } else {
            collapsed.push_back(c);
        }
        lastWasSpace = isSpace;
    }
    // Trim.
    size_t start = collapsed.find_first_not_of(' ');
    size_t end = collapsed.find_last_not_of(' ');
    if (start == std::string::npos) {
        return "";
    }
    return collapsed.substr(start, end - start + 1);
}

} // namespace html_strip
