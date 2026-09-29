# Android Wiki AI

An Android Studio project for a privacy-first, offline Wikipedia assistant.

## Current prototype

The app has no network permission. It can import a `.gguf` model and a `.zim`
Wikipedia dump from Android's document picker and copy both into its private
internal storage. You can download those files separately (for example, with a
browser) and then import them.

**"Ask offline assistant" runs a real, grounded, fully on-device pipeline:**

1. The question is used to search the imported ZIM archive with a hybrid
   keyword + fuzzy (typo-tolerant) match over article titles, followed by a
   content-overlap re-score of the best candidates.
2. If no article clears a minimum relevance threshold (or the model/ZIM
   hasn't been imported yet), the app fails closed with an "I don't know"
   style refusal rather than guessing.
3. Otherwise, the top article excerpt(s) are inserted into a prompt as
   required context, and the imported GGUF model (run on-device via
   llama.cpp) generates an answer instructed to use only that context.
4. The answer is shown together with a "Grounded in: <article titles>"
   citation line.

No network calls happen at runtime — inference and ZIM search both run
entirely on-device. See "Native build requirements" below for the one-time
network access needed to fetch native dependencies during the *build*.

## Architecture

- `app/src/main/java/org/odweta/androidwikiai/AssistantController.java` —
  the search → prompt → generate → cite pipeline described above.
- `app/src/main/java/org/odweta/androidwikiai/offlineai/` — thin Java/JNI
  wrapper classes (`ZimArchive`, `LlamaModel`) around the native layer.
- `app/src/main/cpp/` — the native (C++/JNI) layer:
  - `llama_bridge.{h,cpp}` — loads a GGUF model and runs text generation
    using [llama.cpp](https://github.com/ggml-org/llama.cpp)'s public C API.
  - `zim_reader.{h,cpp}` / `zim_format.{h,cpp}` — a small, purpose-built
    reader for the [openZIM binary format](https://wiki.openzim.org/wiki/ZIM_file_format)
    (title/path pointer lists, cluster + blob layout). This is **not**
    libzim: real libzim depends on Xapian for full-text search and has
    moved its build to Meson, neither of which integrates cleanly with
    Android's CMake/NDK external-native-build. This reader instead uses
    `zstd` for cluster decompression and implements its own fuzzy +
    keyword title search (see below). It only supports uncompressed and
    Zstandard-compressed clusters (the compression used by modern Kiwix
    ZIM dumps); legacy LZMA2/XZ-compressed clusters are skipped.
  - `text_similarity.h` — Levenshtein + Jaro-Winkler string similarity used
    for fuzzy/typo-tolerant article title matching.
  - `html_strip.{h,cpp}` — strips article HTML down to plain text for
    prompting.
  - `jni_bridge.cpp` — registers the native methods via `JNI_OnLoad` /
    `RegisterNatives`.

## Native build requirements

The native layer is built via Android's CMake/NDK external native build
(see `app/build.gradle.kts` and `app/src/main/cpp/CMakeLists.txt`). It
requires:

- The Android NDK (pinned via `ndkVersion` in `app/build.gradle.kts`) and
  CMake, installed automatically by Android Studio/`sdkmanager` when
  missing.
- **Network access the first time CMake configures the native module** on
  a clean checkout: `CMakeLists.txt` uses CMake's `FetchContent` to vendor
  [llama.cpp](https://github.com/ggml-org/llama.cpp) and
  [zstd](https://github.com/facebook/zstd) at pinned release tags, the
  same approach used by llama.cpp's own `examples/llama.android`. Once
  fetched, the sources are cached under `app/.cxx/`/CMake's `_deps`
  directory and subsequent builds are fully offline.

This does not change the app's runtime privacy posture: the installed app
still has no `INTERNET` permission and makes no network calls when running
on a device. Network is only used by the build toolchain on the developer's
or CI machine.

## Known limitations

- Search relevance for very large ZIM dumps (e.g. full English Wikipedia,
  millions of articles) can be slow to index on first archive open, since
  this lightweight reader scans the full title list in memory rather than
  using a persisted full-text search index (as libzim/Xapian would).
  Topic-focused ZIM dumps perform much better.
- Clusters compressed with the legacy LZMA2/XZ codec are not decoded;
  articles stored in such clusters are skipped by search. Modern Kiwix ZIM
  dumps use Zstandard, which is fully supported.

## Build

Open this directory in Android Studio and sync Gradle, or run:

```sh
./gradlew assembleDebug
```

Gradle downloads the Android build plugin and, on a clean checkout, the
native dependencies described above when it is not already cached; the
installed app itself does not make network connections.
