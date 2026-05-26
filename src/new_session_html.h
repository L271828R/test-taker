#pragma once
#include <map>
#include <string>
#include <vector>
#include "exam_prompt.h"  // FocusArea

struct NewSessionFormState {
    std::string projectName;    // empty = no project
    std::string topic;
    std::string instructions;
    std::vector<FocusArea> focusAreas;
    std::string difficulty    = "mixed";
    int         questions     = 10;
    std::string backend       = "claude -p";
    std::string apiKey;
    std::string ollamaModel   = "llama3";
    std::vector<std::string> ollamaModels;   // populated after refresh
    bool        hasCorpus     = false;
    bool        useCorpus     = false;
    std::vector<std::string> selectedPersonalities; // from Personas tab checked set
    int         tidbitCount   = 1;
    bool        darkMode      = false;
    std::string statusMsg;
};

// Build the full HTML page for the New Session form.
// Pure function — no wxWidgets dependency, directly unit-testable.
std::string BuildNewSessionHTML(const NewSessionFormState& s);

// Serialise focus areas to "stars@@text|stars@@text|..." for form transport.
std::string SerializeFocusAreas(const std::vector<FocusArea>& areas);

// Deserialise focus areas from "stars@@text|stars@@text|...".
std::vector<FocusArea> DeserializeFocusAreas(const std::string& s);
