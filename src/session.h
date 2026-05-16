#pragma once
#include <string>
#include <vector>

enum class Score { Correct, Partial, Missed, Skipped };

struct QuestionTurn {
    std::string question;
    std::string userAnswer;
    Score       score       = Score::Skipped;
    std::string explanation;
    bool        flagged     = false;
};

// Parse/serialize the body of a :::session[...] block.
std::vector<QuestionTurn> ParseSession(const std::string& body);
std::string SerializeSessionBody(const std::vector<QuestionTurn>& turns);

// Load all turns from the :::session block in filePath.
// Returns empty vector if no block exists.
std::vector<QuestionTurn> LoadSession(const std::string& filePath);

// Append a completed turn, creating the :::session block if absent.
// Returns false on I/O error.
bool AppendSessionTurn(const std::string& filePath, const QuestionTurn& turn);

// Set the flagged field on the turn at zero-based index.
// Rewrites the whole :::session block in place.
// Returns false on I/O error or out-of-range index.
bool SetTurnFlagged(const std::string& filePath, int index, bool flagged);

Score       ScoreFromString(const std::string& s);
std::string ScoreToString(Score s);
std::string ScoreLabel(Score s);
