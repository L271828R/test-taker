#pragma once
#include <set>
#include <string>
#include <vector>
#include "session.h"

struct TurnChatTurn {
    std::string question;
    std::string answer;
};

// Parse Q:/A: pairs from the body of a :::chat[N] block.
std::vector<TurnChatTurn> ParseTurnChat(const std::string& body);

// Serialize turns to the block body (no opening/closing ::: lines).
std::string SerializeTurnChatBody(const std::vector<TurnChatTurn>& turns);

// Load the chat history for the turn at zero-based turnIndex from filePath.
// Returns empty if no :::chat[turnIndex] block exists.
std::vector<TurnChatTurn> LoadTurnChat(const std::string& filePath, int turnIndex);

// Append a completed turn to the :::chat[turnIndex] block.
// Creates the block if none exists, placed after the :::session block.
// Returns false on I/O error.
bool AppendTurnChatTurn(const std::string& filePath, int turnIndex,
                        const TurnChatTurn& turn);

// Build the LLM prompt for a follow-up chat on a completed exam turn.
// corpusContext is prepended when non-empty (RAG excerpts from the project corpus).
std::string BuildTurnChatPrompt(const QuestionTurn& examTurn,
                                const std::vector<TurnChatTurn>& history,
                                const std::string& question,
                                const std::string& corpusContext = "");

// Build the full HTML page for the discussion panel.
// savedIndices marks which chat turns already have a saved-convo entry.
// pendingQ is shown as a "thinking" bubble when an LLM call is in flight.
std::string BuildTurnChatHTML(const QuestionTurn& examTurn,
                               int turnIndex,
                               const std::vector<TurnChatTurn>& turns,
                               bool darkMode,
                               const std::set<int>& savedIndices = {},
                               const std::string& pendingQ = "");
