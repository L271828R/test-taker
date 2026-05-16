#pragma once
#include <string>
#include <vector>
#include <functional>

struct ConversationTurn {
    std::string question;
    std::string answer;   // empty while LLM has not yet responded
};

// Parse Q:/A: pairs from the body of a :::conversation block.
std::vector<ConversationTurn> ParseConversation(const std::string& body);

// Serialize turns to the Q:/A: block body (no opening/closing ::: lines).
std::string SerializeConversationBody(const std::vector<ConversationTurn>& turns);

// Read the existing conversation turns for chId from filePath.
// Returns empty if the file has no :::conversation block for that chapter.
std::vector<ConversationTurn> LoadConversation(const std::string& filePath, int chId);

// Append a completed turn to the :::conversation block for chId.
// Creates the block if none exists, placed before the next chapter separator.
// Returns false on I/O error.
bool AppendTurn(const std::string& filePath, int chId,
                const std::string& chTitle, const ConversationTurn& turn);

// Delete the turn at zero-based index from the :::conversation block for chId.
// If it is the last turn the entire block is removed.
// Returns false on I/O error or out-of-range index.
bool DeleteTurn(const std::string& filePath, int chId, int index);

// Build the LLM prompt for a Q&A request.
std::string BuildQAPrompt(const std::string& docMarkdown,
                          const std::string& chTitle,
                          const std::vector<ConversationTurn>& history,
                          const std::string& question);
