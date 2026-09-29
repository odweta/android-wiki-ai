#pragma once

#include <memory>
#include <string>

struct llama_model;
struct llama_context;

namespace llamabridge {

// Wraps a loaded GGUF model + inference context using llama.cpp's public C
// API (see tools/simple/simple.cpp in llama.cpp for the reference pattern
// this mirrors). One instance owns exactly one loaded model.
class Model {
public:
    static std::unique_ptr<Model> load(const std::string &modelPath, int contextSize,
                                        std::string *error);
    ~Model();

    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;

    // Runs greedy-ish sampled generation from `prompt` and returns the
    // generated continuation (not including the prompt itself).
    std::string generate(const std::string &prompt, int maxTokens, std::string *error);

private:
    Model() = default;

    llama_model *model_ = nullptr;
    llama_context *ctx_ = nullptr;
    int contextSize_ = 0;
};

} // namespace llamabridge
