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

    return failures;
}
