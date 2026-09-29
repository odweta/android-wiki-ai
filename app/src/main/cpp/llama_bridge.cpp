#include "llama_bridge.h"

#include <android/log.h>
#include <llama.h>

#include <algorithm>
#include <vector>

#define LOG_TAG "LlamaBridge"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace llamabridge {

namespace {
bool g_backendInitialized = false;

void ensureBackendInitialized() {
    if (!g_backendInitialized) {
        ggml_backend_load_all();
        llama_backend_init();
        g_backendInitialized = true;
    }
}
} // namespace

std::unique_ptr<Model> Model::load(const std::string &modelPath, int contextSize,
                                    std::string *error) {
    ensureBackendInitialized();

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = 0; // CPU-only: no GPU backend is bundled for this app.

    llama_model *model = llama_model_load_from_file(modelPath.c_str(), modelParams);
    if (!model) {
        *error = "Failed to load the GGUF model file. It may be corrupt or unsupported.";
        return nullptr;
    }

    llama_context_params ctxParams = llama_context_default_params();
    ctxParams.n_ctx = static_cast<uint32_t>(contextSize);
    ctxParams.n_batch = static_cast<uint32_t>(contextSize);
    ctxParams.no_perf = true;

    llama_context *ctx = llama_init_from_model(model, ctxParams);
    if (!ctx) {
        llama_model_free(model);
        *error = "Failed to create the inference context for the loaded model.";
        return nullptr;
    }

    std::unique_ptr<Model> instance(new Model());
    instance->model_ = model;
    instance->ctx_ = ctx;
    instance->contextSize_ = contextSize;
    return instance;
}

Model::~Model() {
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
}

std::string Model::generate(const std::string &prompt, int maxTokens, std::string *error) {
    const llama_vocab *vocab = llama_model_get_vocab(model_);

    int nPrompt = -llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                   nullptr, 0, true, true);
    if (nPrompt <= 0) {
        *error = "Failed to tokenize the prompt.";
        return "";
    }
    std::vector<llama_token> promptTokens(nPrompt);
    if (llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                        promptTokens.data(), static_cast<int32_t>(promptTokens.size()), true,
                        true) < 0) {
        *error = "Failed to tokenize the prompt.";
        return "";
    }

    if (nPrompt + maxTokens > contextSize_) {
        // Keep generation within the configured context window; trim the
        // requested token budget rather than failing outright.
        maxTokens = std::max(1, contextSize_ - nPrompt - 1);
    }

    // Reset any state left over from a previous generate() call so each
    // question starts from a clean context.
    llama_memory_clear(llama_get_memory(ctx_), true);

    llama_sampler_chain_params samplerParams = llama_sampler_chain_default_params();
    samplerParams.no_perf = true;
    llama_sampler *sampler = llama_sampler_chain_init(samplerParams);
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.6f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::string output;
    llama_batch batch = llama_batch_get_one(promptTokens.data(),
                                             static_cast<int32_t>(promptTokens.size()));

    int decoded = 0;
    int position = 0;
    bool failed = false;
    llama_token nextToken = 0;
    while (position + batch.n_tokens <= nPrompt + maxTokens) {
        if (llama_decode(ctx_, batch) != 0) {
            *error = "The model failed to evaluate the prompt/context.";
            failed = true;
            break;
        }
        position += batch.n_tokens;

        llama_token newToken = llama_sampler_sample(sampler, ctx_, -1);
        if (llama_vocab_is_eog(vocab, newToken)) {
            break;
        }

        char buf[256];
        int n = llama_token_to_piece(vocab, newToken, buf, sizeof(buf), 0, true);
        if (n < 0) {
            break;
        }
        output.append(buf, n);
        decoded++;

        nextToken = newToken;
        batch = llama_batch_get_one(&nextToken, 1);
    }

    llama_sampler_free(sampler);
    if (failed && decoded == 0) {
        return "";
    }
    return output;
}

} // namespace llamabridge
