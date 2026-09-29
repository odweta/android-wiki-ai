#pragma once

#include <string>

namespace html_strip {

// Strips tags (including <script>/<style> contents), decodes a handful of
// common HTML entities, and collapses whitespace, producing plain text
// suitable for use as a prompt excerpt.
std::string toPlainText(const std::string &html);

} // namespace html_strip
