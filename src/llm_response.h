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

// Parses an LLM response of the form:
//   CORRECT: <statement>
//   WRONG: <statement>
// Returns {correct, wrong}. Returns {"", ""} if either line is missing.
std::pair<std::string, std::string> ParseGameChoices(const std::string& response);

// Parses multiple CORRECT/WRONG pairs separated by "---" lines.
// Incomplete blocks (missing CORRECT or WRONG) are silently dropped.
std::vector<std::pair<std::string,std::string>>
    ParseMultipleGameChoices(const std::string& response);
