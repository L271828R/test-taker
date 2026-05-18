#include "llm.h"
#include "llm_error.h"
#include "llm_response.h"
#include "logger.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>

namespace fs = std::filesystem;

static std::string temp_prompt_path() {
    auto ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / ("test-taker_prompt_" + std::to_string(ns) + ".txt"))
           .string();
}

static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += "'";
    return out;
}

static LLMResult run_shell(const std::string& cmd) {
    Logger::get().log("run_shell: " + cmd);
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        Logger::get().log("run_shell FAILED: popen returned null");
        return {false, "", "popen failed: " + cmd};
    }
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    int status = pclose(pipe);
    int exitCode = status;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }
    std::string preview = out.size() > 300 ? out.substr(0, 300) + "…" : out;
    Logger::get().log("run_shell exit=" + std::to_string(exitCode)
                      + "  status=" + std::to_string(status)
                      + "  output_len=" + std::to_string(out.size())
                      + "  preview=" + preview);
    if (status != 0) {
        std::string summary;
        if (WIFSIGNALED(status)) {
            summary = "command failed (signal " + std::to_string(WTERMSIG(status)) + ")";
        } else {
            summary = "command failed (exit " + std::to_string(exitCode) + ")";
        }
        return {false, out, FormatLLMError(summary, out)};
    }
    return {true, out, ""};
}

LLMResult InvokeLLM(const std::string& prompt, const LLMConfig& cfg) {
    static const char* kBackendNames[] = {"ClaudeP", "CodexCLI", "GeminiCLI", "Ollama", "API", "Clipboard"};
    int backendIdx = static_cast<int>(cfg.backend);
    Logger::get().log("InvokeLLM backend=" + std::string(kBackendNames[backendIdx])
                      + "  prompt_len=" + std::to_string(prompt.size()));

    if (cfg.backend == LLMBackend::Clipboard) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(prompt)));
            wxTheClipboard->Close();
        }
        Logger::get().log("InvokeLLM: copied to clipboard");
        return {true, "clipboard", ""};
    }

    std::string tmpFile = temp_prompt_path();
    {
        std::ofstream f(tmpFile);
        f << prompt;
        if (!f.good()) return {false, "", "could not write temp file"};
    }

    LLMResult result;

    if (cfg.backend == LLMBackend::ClaudeP) {
        // Use login shell so ~/.nvm, Homebrew, etc. are on PATH.
        std::string inner = "claude -p < " + shell_quote(tmpFile);
        std::string cmd = "bash -l -c " + shell_quote(inner) + " 2>&1";
        result = run_shell(cmd);
        if (!result.ok && result.text.find("not found") != std::string::npos)
            result.error = "claude CLI not found — check PATH or use Clipboard mode";
    }
    else if (cfg.backend == LLMBackend::CodexCLI) {
        std::string outFile = tmpFile + ".codex.md";
        std::string inner =
            "codex --ask-for-approval never exec --sandbox read-only "
            "--skip-git-repo-check --output-last-message " + shell_quote(outFile) +
            " - < " + shell_quote(tmpFile);
        std::string cmd = "bash -l -c " + shell_quote(inner) + " 2>&1";
        auto raw = run_shell(cmd);
        if (!raw.ok) {
            fs::remove(outFile);
            return raw;
        }

        std::ifstream f(outFile);
        if (!f) {
            fs::remove(outFile);
            return {false, raw.text,
                    FormatLLMError("Codex did not write an output file", raw.text)};
        }
        std::string text(std::istreambuf_iterator<char>(f), {});
        fs::remove(outFile);
        if (text.empty())
            return {false, raw.text, FormatLLMError("Codex returned an empty response", raw.text)};
        result = {true, text, ""};
    }
    else if (cfg.backend == LLMBackend::GeminiCLI) {
        std::string inner = "gemini -p < " + shell_quote(tmpFile);
        std::string cmd = "bash -l -c " + shell_quote(inner) + " 2>&1";
        result = run_shell(cmd);
        if (!result.ok && result.text.find("not found") != std::string::npos)
            result.error = "gemini CLI not found — check PATH or use Clipboard mode";
    }
    else if (cfg.backend == LLMBackend::Ollama) {
        std::string jsonFile = tmpFile + ".json";
        {
            std::ofstream jf(jsonFile);
            if (cfg.ollamaStructured) {
                // Content-creation mode: force JSON output with markdown schema.
                std::string sp = BuildOllamaStructuredPrompt(prompt);
                jf << "{\"model\":\"" << JsonEscape(cfg.ollamaModel) << "\","
                   << "\"stream\":false,"
                   << "\"format\":\"json\","
                   << "\"prompt\":\"" << JsonEscape(sp) << "\"}";
            } else {
                // Plain-text mode: exam, chat, turn-chat — just send the prompt as-is.
                jf << "{\"model\":\"" << JsonEscape(cfg.ollamaModel) << "\","
                   << "\"stream\":false,"
                   << "\"prompt\":\"" << JsonEscape(prompt) << "\"}";
            }
        }
        std::string cmd = "curl -s -X POST \"" + cfg.ollamaUrl + "/api/generate\""
                          " -H 'Content-Type: application/json'"
                          " --data-binary @\"" + jsonFile + "\"" " 2>&1";
        auto raw = run_shell(cmd);
        fs::remove(jsonFile);
        if (!raw.ok) return raw;

        std::string text;
        if (cfg.ollamaStructured) {
            text = ExtractOllamaMarkdown(raw.text);
            if (text.empty())
                return {false, "", "Ollama returned unexpected JSON: " + raw.text.substr(0, 200)};
        } else {
            text = ExtractJSONString(raw.text, "response");
            if (text.empty())
                return {false, "", "Ollama returned empty response: " + raw.text.substr(0, 200)};
        }
        result = {true, text, ""};
    }
    else if (cfg.backend == LLMBackend::API) {
        std::string jsonFile = tmpFile + ".json";
        {
            std::ofstream jf(jsonFile);
            jf << "{\"model\":\"claude-sonnet-4-6\",\"max_tokens\":8096,"
               << "\"messages\":[{\"role\":\"user\",\"content\":\""
               << JsonEscape(prompt) << "\"}]}";
        }
        std::string cmd =
            "curl -s -X POST https://api.anthropic.com/v1/messages"
            " -H 'x-api-key: " + cfg.apiKey + "'"
            " -H 'anthropic-version: 2023-06-01'"
            " -H 'Content-Type: application/json'"
            " --data-binary @\"" + jsonFile + "\"" " 2>&1";
        auto raw = run_shell(cmd);
        fs::remove(jsonFile);
        if (!raw.ok) return raw;
        std::string text = ExtractJSONString(raw.text, "text");
        if (text.empty())
            return {false, "", "API returned unexpected JSON: " + raw.text.substr(0, 200)};
        result = {true, text, ""};
    }

    fs::remove(tmpFile);
    if (result.ok)
        Logger::get().log("InvokeLLM OK  response_len=" + std::to_string(result.text.size()));
    else
        Logger::get().log("InvokeLLM FAILED: " + result.error);
    return result;
}
