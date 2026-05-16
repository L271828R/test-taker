#pragma once
#include <string>
#include <vector>

struct GenerationRequest {
    std::string topic;
    std::string style;
    std::vector<std::string> characters;
    std::string projectContext;
};

// Builds a full LLM prompt from the request.
// llmReadme is injected from GetLLMReadme(); callers may pass "" in tests.
std::string BuildPrompt(const GenerationRequest& req, const std::string& llmReadme);

struct ValidationResult {
    bool ok;
    std::string error;
};

// Checks that generated content has the minimum required structure
// before it is stamped and saved.
ValidationResult ValidateGeneratedStory(const std::string& content);

// Builds a follow-up prompt after a generated story failed validation.
std::string BuildRepairPrompt(const std::string& originalPrompt,
                              const std::string& validationError);

// Builds a patch prompt asking the LLM to rewrite a single block per the instruction.
// chapterContext: the full chapter text — gives the LLM story context for the rewrite.
std::string BuildPatchPrompt(const std::string& originalBlock,
                             const std::string& instruction,
                             const std::string& llmReadme,
                             const std::string& chapterContext = "");

std::string BuildTranslationPrompt(const std::string& sourceMarkdown,
                                   const std::string& targetLanguage,
                                   const std::string& llmReadme,
                                   const std::string& extraInstruction = "");

// Removes common LLM wrappers around returned markdown, such as an outer
// ```markdown fence and short prose before the actual document begins.
std::string CleanMarkdownResponse(const std::string& response);

// Injects <!-- ch:N --> markers before each "## Chapter N:" heading.
// Returns the stamped text and the count of chapters found.
struct StampResult { std::string text; int count; };
StampResult StampChapters(const std::string& content, int baseId);

// Returns a filename like "ch03_black_holes.md" from a topic and chapter number.
std::string ChapterFilename(const std::string& topic, int chapterNumber);

// Writes content to <projectDir>/<filename> and returns the full path.
// Creates the file; returns "" on failure.
std::string SaveChapter(const std::string& projectDir,
                        const std::string& filename,
                        const std::string& content);
