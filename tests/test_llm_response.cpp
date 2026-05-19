#include "llm.h"
#include "llm_response.h"
#include <iostream>
#include <string>

int test_llm_response() {
    int failures = 0;

    {
        std::string raw = "{\"response\":\"{\\\"markdown\\\":\\\"# Title\\\\nBody\\\"}\"}";
        std::string md = ExtractOllamaMarkdown(raw);
        if (md != "# Title\nBody") {
            std::cerr << "FAIL [ollama-markdown-json]: md='" << md << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [ollama-markdown-json]\n";
        }
    }

    {
        std::string raw = "{\"response\":\"{\\n  \\\"markdown\\\": \\\"# Title\\\\nBody\\\", \\\"extra\\\": \\\"ignored\\\"}\"}";
        std::string md = ExtractOllamaMarkdown(raw);
        if (md != "# Title\nBody") {
            std::cerr << "FAIL [ollama-markdown-json-spaces]: md='" << md << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [ollama-markdown-json-spaces]\n";
        }
    }

    {
        std::string raw = "{\"response\":\"{\\n  \\\"notMarkdown\\\": \\\"# Wrapped\\\"}\"}";
        std::string md = ExtractOllamaMarkdown(raw);
        if (!md.empty()) {
            std::cerr << "FAIL [ollama-reject-json-wrapper]: md='" << md << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [ollama-reject-json-wrapper]\n";
        }
    }

    {
        std::string raw = "{\"response\":\"# Plain\\nMarkdown\"}";
        std::string md = ExtractOllamaMarkdown(raw);
        if (md != "# Plain\nMarkdown") {
            std::cerr << "FAIL [ollama-markdown-plain]: md='" << md << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [ollama-markdown-plain]\n";
        }
    }

    {
        auto names = ParseOllamaTags("{\"models\":[{\"name\": \"llama3:latest\"},{\"name\":\"mistral\"}]}");
        bool ok = names.size() == 2 && names[0] == "llama3:latest" && names[1] == "mistral";
        if (!ok) {
            std::cerr << "FAIL [ollama-tags]: count=" << names.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ollama-tags]\n";
        }
    }

    {
        std::string prompt = BuildOllamaStructuredPrompt("Write a chapter.");
        bool ok = prompt.find("\"markdown\"") != std::string::npos
               && prompt.find("JSON object") != std::string::npos
               && prompt.find("language: Chinese") != std::string::npos
               && prompt.find("## Chapter N: Title") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [ollama-structured-prompt]\n";
            ++failures;
        } else {
            std::cout << "PASS [ollama-structured-prompt]\n";
        }
    }

    // Plain Ollama response (exam/chat mode): extract response field directly
    {
        std::string raw = R"({"model":"phi4-mini","response":"What is a vtable?","done":true})";
        std::string text = ExtractJSONString(raw, "response");
        bool ok = (text == "What is a vtable?");
        if (!ok) {
            std::cerr << "FAIL [ollama-plain-response]: got '" << text << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [ollama-plain-response]\n";
        }
    }

    // ollamaStructured defaults to false — plain mode is the default
    {
        LLMConfig cfg;
        cfg.backend = LLMBackend::Ollama;
        bool ok = !cfg.ollamaStructured;
        if (!ok) {
            std::cerr << "FAIL [ollama-plain-default]\n";
            ++failures;
        } else {
            std::cout << "PASS [ollama-plain-default]\n";
        }
    }

    // ExtractJSONString decodes \uXXXX escapes (Anthropic API encodes < > & as < etc.)
    {
        // Simulates Anthropic API JSON: < = <, > = >
        std::string json = "{\"text\":\"dynamic_cast\\u003cCircle*\\u003e(shape)\"}";
        std::string val  = ExtractJSONString(json, "text");
        bool ok = val == "dynamic_cast<Circle*>(shape)";
        if (!ok) {
            std::cerr << "FAIL [extract-json-unicode-escape]: got '" << val << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [extract-json-unicode-escape]\n";
        }
    }

    {
        bool ok = BackendFromLabel("Gemini CLI") == LLMBackend::GeminiCLI
               && BackendLabel(LLMBackend::GeminiCLI) == "Gemini CLI";
        if (!ok) {
            std::cerr << "FAIL [backend-gemini-mapping]\n";
            ++failures;
        } else {
            std::cout << "PASS [backend-gemini-mapping]\n";
        }
    }

    // BackendFromLabel("") must NOT return Clipboard — an empty/unset backend label
    // (e.g. new project with no saved examBackend) should fall back to ClaudeP, not
    // silently route all LLM calls through the clipboard.
    {
        bool ok = BackendFromLabel("") == LLMBackend::ClaudeP;
        if (!ok) {
            std::cerr << "FAIL [backend-empty-not-clipboard]: got "
                      << static_cast<int>(BackendFromLabel("")) << "\n";
            ++failures;
        } else {
            std::cout << "PASS [backend-empty-not-clipboard]\n";
        }
    }

    // LLMConfig default backend must not be Clipboard for the same reason.
    {
        LLMConfig cfg;
        bool ok = cfg.backend != LLMBackend::Clipboard;
        if (!ok) {
            std::cerr << "FAIL [llmconfig-default-not-clipboard]\n";
            ++failures;
        } else {
            std::cout << "PASS [llmconfig-default-not-clipboard]\n";
        }
    }

    return failures;
}
