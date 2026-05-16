#pragma once
#include <string>
#include <vector>

std::string JsonEscape(const std::string& text);
std::string ExtractJSONString(const std::string& json, const std::string& key);

// Ollama /api/generate returns a JSON envelope with a response string. For
// For LLM prompts we ask that response string to be either raw Markdown or
// a JSON object with a markdown/content field.
std::string ExtractOllamaMarkdown(const std::string& ollamaResponse);
std::string BuildOllamaStructuredPrompt(const std::string& prompt);
std::vector<std::string> ParseOllamaTags(const std::string& json);
