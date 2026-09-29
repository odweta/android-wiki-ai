package org.odweta.androidwikiai.offlineai;

import java.io.Closeable;
import java.io.IOException;

/**
 * JNI wrapper around llama.cpp, loading a GGUF model and running text
 * generation entirely on-device (see app/src/main/cpp/llama_bridge.h).
 */
public final class LlamaModel implements Closeable {
    static {
        System.loadLibrary("androidwikiai");
    }

    private long handle;

    /** Loads the model at {@code modelPath} with the given context window size. */
    public LlamaModel(String modelPath, int contextSize) throws IOException {
        handle = nativeLoad(modelPath, contextSize);
        if (handle == 0) {
            String error = nativeLastError();
            throw new IOException(error == null || error.isEmpty()
                    ? "Failed to load the GGUF model." : error);
        }
    }

    /** Generates up to {@code maxTokens} tokens of continuation for {@code prompt}. */
    public String generate(String prompt, int maxTokens) {
        if (handle == 0) {
            return "";
        }
        return nativeGenerate(handle, prompt, maxTokens);
    }

    @Override
    public void close() {
        if (handle != 0) {
            nativeUnload(handle);
            handle = 0;
        }
    }

    private static native long nativeLoad(String path, int contextSize);

    private static native String nativeGenerate(long handle, String prompt, int maxTokens);

    private static native void nativeUnload(long handle);

    private static native String nativeLastError();
}
