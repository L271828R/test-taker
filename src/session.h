#pragma once
#include <string>
#include <vector>

enum class Score { Star1, Star2, Star3, Star4, Star5, Skipped };

struct QuestionTurn {
    std::string question;
    std::string userAnswer;
    Score       score       = Score::Skipped;
    std::string explanation;
    bool        flagged     = false;
    bool        saved       = false;
    bool        silentSkip  = false;
    std::string note;
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

// Set the note field on the turn at zero-based index.
// Rewrites the whole :::session block in place.
// Returns false on I/O error or out-of-range index.
bool SetTurnNote(const std::string& filePath, int index, const std::string& note);

// Set the saved field on the turn at zero-based index.
bool SetTurnSaved(const std::string& filePath, int index, bool saved);

Score       ScoreFromString(const std::string& s);
std::string ScoreToString(Score s);
std::string ScoreLabel(Score s);

// Parse the markdown header of a session file into topic/difficulty/totalQuestions.
struct SessionHeader {
    std::string topic;
    std::string instructions;
    std::string difficulty;
    std::string backend;
    int         totalQuestions = 0;
};
SessionHeader LoadSessionHeader(const std::string& filePath);
