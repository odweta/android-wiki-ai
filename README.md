# Android Wiki AI

An Android Studio project for a privacy-first, offline Wikipedia assistant.

## Current prototype

The app has no network permission. It can import a `.gguf` model and a `.zim`
Wikipedia dump from Android's document picker and copy both into its private
internal storage. You can download those files separately (for example, with a
browser) and then import them.

**This prototype does not yet execute GGUF models or read ZIM files.** It
therefore refuses to answer questions rather than inventing information. A
working answer pipeline still requires an on-device inference runtime and a
ZIM reader, plus evidence retrieval and citation checks. A prompt alone cannot
guarantee that a generative model never hallucinates; the eventual answer path
must be grounded in retrieved articles and fail closed when evidence is
missing.

## Build

Open this directory in Android Studio and sync Gradle, or run:

```sh
./gradlew assembleDebug
```

The app uses platform Android APIs only. Gradle downloads the Android build
plugin when it is not already cached; the installed app itself does not make
network connections.
