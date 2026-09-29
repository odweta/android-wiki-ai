package org.odweta.androidwikiai.offlineai;

import java.io.Closeable;

/**
 * JNI wrapper around the app's lightweight native ZIM reader (see
 * app/src/main/cpp/zim_reader.h). Not libzim: implements only what this app
 * needs (title/keyword/fuzzy search, plain-text article extraction).
 */
public final class ZimArchive implements Closeable {
    static {
        System.loadLibrary("androidwikiai");
    }

    /** A single search result: an article ranked as relevant to a query. */
    public static final class Article {
        public final String title;
        public final String snippet;
        public final String content;
        public final float score;

        public Article(String title, String snippet, String content, float score) {
            this.title = title;
            this.snippet = snippet;
            this.content = content;
            this.score = score;
        }
    }

    private long handle;

    /** Opens the archive at {@code path}, or throws if it cannot be read. */
    public ZimArchive(String path) throws java.io.IOException {
        handle = nativeOpen(path);
        if (handle == 0) {
            String error = nativeLastError();
            throw new java.io.IOException(
                    error == null || error.isEmpty() ? "Failed to open ZIM file." : error);
        }
    }

    /**
     * Returns up to {@code maxResults} articles relevant to {@code query},
     * ranked by a hybrid fuzzy + keyword match. Empty when nothing clears
     * the archive's minimum relevance threshold.
     */
    public Article[] search(String query, int maxResults) {
        if (handle == 0) {
            return new Article[0];
        }
        return nativeSearch(handle, query, maxResults);
    }

    @Override
    public void close() {
        if (handle != 0) {
            nativeClose(handle);
            handle = 0;
        }
    }

    private static native long nativeOpen(String path);

    private static native Article[] nativeSearch(long handle, String query, int maxResults);

    private static native void nativeClose(long handle);

    private static native String nativeLastError();
}
