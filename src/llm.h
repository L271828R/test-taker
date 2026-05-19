#pragma once
#include <string>

enum class LLMBackend { ClaudeP, CodexCLI, GeminiCLI, Ollama, API, Clipboard };

struct LLMConfig {
    LLMBackend  backend          = LLMBackend::ClaudeP;
    std::string apiKey;
    std::string ollamaModel      = "llama3";
    std::string ollamaUrl        = "http://localhost:11434";
    bool        ollamaStructured = false; // true = content-creation (JSON+structured prompt)
                                          // false = plain text (exam/chat)
};

struct LLMResult {
    bool        ok    = false;
    std::string text;
    std::string error;
};

// Number of corpus chunks to retrieve. Large cloud models handle more context.
inline int CorpusTopK(LLMBackend b) {
    return (b == LLMBackend::Ollama || b == LLMBackend::Clipboard) ? 3 : 9;
}

// True for cloud backends capable of complex structured output (diagrams, tidbits, etc.).
inline bool IsLargeModel(LLMBackend b) {
    return b != LLMBackend::Ollama && b != LLMBackend::Clipboard;
}

// Convert between stored labels and backend enum values.
inline LLMBackend BackendFromLabel(const std::string& label) {
    if (label == "claude -p")      return LLMBackend::ClaudeP;
    if (label == "Codex CLI")      return LLMBackend::CodexCLI;
    if (label == "Gemini CLI")     return LLMBackend::GeminiCLI;
    if (label == "Ollama (local)") return LLMBackend::Ollama;
    if (label == "Anthropic API")  return LLMBackend::API;
    return LLMBackend::ClaudeP;
}

inline std::string BackendLabel(LLMBackend backend) {
    switch (backend) {
        case LLMBackend::ClaudeP:   return "claude -p";
        case LLMBackend::CodexCLI:  return "Codex CLI";
        case LLMBackend::GeminiCLI:  return "Gemini CLI";
        case LLMBackend::Ollama:    return "Ollama (local)";
        case LLMBackend::API:       return "Anthropic API";
        case LLMBackend::Clipboard: return "Clipboard (manual)";
    }
    return "Clipboard (manual)";
}

// Invoke the LLM synchronously. Must be called from a background thread for
// all backends except Clipboard (which must run on the main thread).
// For Clipboard: copies the prompt to clipboard and returns ok=true, text="clipboard".
LLMResult InvokeLLM(const std::string& prompt, const LLMConfig& cfg);
