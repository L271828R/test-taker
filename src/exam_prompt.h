#pragma once
#include <string>
#include <vector>
#include "session.h"

struct ExamConfig {
    std::string topic;           // short label shown in Review (one line)
    std::string instructions;    // free-form focus/detail injected into the prompt
    std::string difficulty;      // "easy" | "medium" | "hard" | "mixed"
    std::string projectContext;  // contents of context.md, injected verbatim
    int         totalQuestions = 10;
};

// Build the first prompt: asks LLM to generate question #1.
std::string BuildFirstQuestionPrompt(const ExamConfig& cfg);

// Build the scoring + next-question prompt.
// history = all completed turns so far.
// userAnswer = "" means the user skipped (clicked "I don't know").
// questionsRemaining = how many questions are left after the current one.
std::string BuildScoringAndNextPrompt(const ExamConfig& cfg,
                                      const std::vector<QuestionTurn>& history,
                                      const std::string& currentQuestion,
                                      const std::string& userAnswer,
                                      int questionsRemaining);

// Build the session summary prompt (called after all questions are answered).
std::string BuildSessionSummaryPrompt(const ExamConfig& cfg,
                                      const std::vector<QuestionTurn>& history);

// Parsed result of BuildScoringAndNextPrompt response.
struct ScoredResponse {
    Score       score;
    std::string explanation;
    std::string nextQuestion;  // empty when session is over
    bool        parseOk = false;
};

ScoredResponse ParseScoredResponse(const std::string& llmOutput);

// Render completed turns as HTML body fragment.
// chatCounts[i] is the number of chat exchanges for turn i (0 = no chats yet).
// Each turn gets a hover-highlight and testtaker://flag/N, note/N, discuss/N links.
std::string RenderExamTurns(const std::vector<QuestionTurn>& turns,
                             const std::vector<int>&          chatCounts);
