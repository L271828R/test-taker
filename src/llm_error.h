#pragma once
#include <string>

// Builds a user-facing LLM backend failure message.
// Includes stderr/stdout when available because CLIs often put actionable
// details there, such as rate-limit reset times.
std::string FormatLLMError(const std::string& summary,
                           const std::string& commandOutput);
