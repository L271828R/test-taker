#pragma once
#include <string>
#include <vector>

struct GameData {
    std::string question;
    std::string choiceA;
    std::string choiceB;
    bool        correctIsA = true;
};

// Single-question file I/O (backward compatible).
bool     WriteGameFile(const std::string& path, const GameData& data);
GameData ReadGameFile (const std::string& path);

// Multi-question file I/O.  Questions are separated by "---" lines.
bool                   WriteGameFiles (const std::string& path, const std::vector<GameData>& items);
std::vector<GameData>  ReadGameFiles  (const std::string& path);

// Append more questions to an existing file (thread-safe: atomic rename on the caller's side
// is not needed because the game only grows the vector forward).
bool                   AppendGameFiles(const std::string& path, const std::vector<GameData>& items);
