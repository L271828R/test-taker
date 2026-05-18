#pragma once
#include <string>
#include <vector>

struct SavedConvo {
    std::string date;
    std::string question;
    std::string explanation;
};

// Append a Q&A pair to <projectDir>/saved_convos.md.
bool AppendSavedConvo(const std::string& projectDir,
                      const std::string& question,
                      const std::string& explanation,
                      const std::string& date);

// Load all saved entries from <projectDir>/saved_convos.md.
std::vector<SavedConvo> LoadSavedConvos(const std::string& projectDir);

// Delete the entry at zero-based index (file order, oldest = 0).
// Rewrites saved_convos.md without that entry.
// Returns false on I/O error or out-of-range index.
bool DeleteSavedConvo(const std::string& projectDir, int index);

// Render saved convos as an HTML body (no <html>/<body> wrapper).
std::string BuildSavedConvosHTML(const std::vector<SavedConvo>& convos);
