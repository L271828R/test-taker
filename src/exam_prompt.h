#pragma once
#include <string>
#include <vector>
#include "session.h"

struct FocusArea {
    std::string text;
    int         stars = 3; // 1 (low priority) … 5 (high priority)
};

// Weighted random selection — higher-star areas are proportionally more likely.
// Returns "" when the list is empty.
std::string PickFocusArea(const std::vector<FocusArea>& areas);

struct ExamConfig {
    std::string topic;                       // short label shown in Review (one line)
    std::string instructions;                // free-form focus/detail injected into the prompt
    std::string focusAreas;                  // per-question focus (picked randomly from list below)
    std::vector<FocusArea> focusAreaList;    // user-defined list with star weights
    std::string difficulty;                  // "easy" | "medium" | "hard" | "mixed"
    std::string projectContext;              // contents of context.md, injected verbatim
    int         totalQuestions = 10;
    bool        useCorpus      = false;      // when true, corpus chunks are retrieved and injected
    bool        largeModel     = false;      // true for cloud backends that handle complex output
    std::vector<std::string> personalities;  // guest commentators injected as :::tidbit blocks
    int         tidbitCount    = 1;          // how many tidbit blocks per turn (1–10)
    std::vector<std::string> moreOfTopics;   // topics user wants more questions about
    std::vector<std::string> lessOfTopics;   // topics user wants fewer questions about
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
                             const std::vector<int>&          chatCounts,
                             const std::vector<std::string>&  moreOfTopics = {},
                             const std::vector<std::string>&  lessOfTopics = {});

// A past session displayed in the history section of the Exam tab.
// sessionFile is needed so interactive toolbar actions (flag, note, discuss, save)
// can persist changes to the correct file.
struct HistoryGroup {
    std::string              label;
    std::string              sessionFile;
    std::vector<QuestionTurn> turns;
};

// Build a short LLM prompt that asks for one correct and one wrong statement
// about the answer to a question. Response is parsed by ParseGameChoices().
std::string BuildGameChoicesPrompt(const std::string& question,
                                   const std::string& explanation);

// Build a prompt that requests `count` follow-up question pairs (---separated).
// Response is parsed by ParseMultipleGameChoices().
std::string BuildGameSeriesPrompt(const std::string& question,
                                  const std::string& explanation,
                                  int count);

// Build a deep-dive prompt for a question the user got wrong or partially right.
// Response includes full explanation + mermaid diagram if applicable + code if applicable.
std::string BuildLearnMorePrompt(const std::string& question,
                                 const std::string& briefExplanation);

// Build a hint prompt: nudges the student without revealing the answer.
std::string BuildHintPrompt(const std::string& question);

// Build a hint for a game question (choiceA vs choiceB) without revealing which is correct.
std::string BuildGameHintPrompt(const std::string& question,
                                const std::string& choiceA,
                                const std::string& choiceB);

// Render past-session groups as interactive history above the active session.
// Each group has interactive toolbar buttons that use testtaker://h{action}/G/I URLs.
// Includes a clear-history link.
std::string RenderHistoryGroups(const std::vector<HistoryGroup>& groups);
