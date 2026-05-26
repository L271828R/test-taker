#pragma once
#include <map>
#include <string>
#include <vector>
#include "project.h"
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

// Apply per-project exam settings (personalities, topic weights, tidbit count) to cfg.
// Called by both StartSession and ResumeSession so neither can miss a field.
void ApplyProjectExamConfig(const ProjectConfig& pcfg, ExamConfig& cfg);

// State snapshot used to render the sticky input footer in the Exam tab.
struct ExamInputState {
    bool        active          = false;  // show input section at all
    bool        busy            = false;  // disable all interactive elements
    bool        hasQuestion     = false;  // a question is currently displayed
    bool        readyForNext    = false;  // no question yet, not busy — show "Next question" button
    bool        canFlag         = false;  // ≥1 turn completed → flag enabled
    bool        lastTurnFlagged = false;  // last turn is flagged → show "Unflag"
    std::string hintText;                 // shown when non-empty
    std::string statusText;               // status line below input row
};

// Build the sticky HTML input footer for the Exam tab.
// Returns empty string when s.active is false.
std::string BuildExamInputSection(const ExamInputState& s);

// Build the current-question block.
// Returns empty string when question is empty and not busy.
// Returns a loading placeholder when question is empty but busy is true.
// Returns the rendered question HTML otherwise.
std::string BuildCurrentQuestionHTML(const std::string& question, bool busy);

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
// thumbnails maps normalized persona name → data URL; used to show persona avatar
// next to explanations that contain a :::tidbit[Name] block.
std::string RenderExamTurns(const std::vector<QuestionTurn>& turns,
                             const std::vector<int>&          chatCounts,
                             const std::vector<std::string>&  moreOfTopics  = {},
                             const std::vector<std::string>&  lessOfTopics  = {},
                             const std::map<std::string, std::string>& thumbnails = {});

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

// Build the opening message for the 🤔 "why not perfect?" chat.
// Explains what was missing from the answer and gives examples of perfect answers.
std::string BuildWhyNotPerfectPrompt(const std::string& question,
                                     const std::string& userAnswer,
                                     const std::string& explanation,
                                     Score              score);

// Data-driven personality system.
// Each entry drives URL handlers, dropdown menus, chat-bubble labels, and prompts.
struct PersonalityDef {
    std::string slug;         // URL path segment: "monkey", "caveman", etc.
    std::string category;     // matches PersonalityCategory::id
    std::string menuLabel;    // HTML for the dropdown item (may contain entity refs)
    std::string displayQ;     // UTF-8 label shown in the chat bubble
    std::string preamble;     // prompt text before the Q / answer / explanation block
    std::string closing;      // prompt text after the explanation block
    std::string subcategory;  // optional: groups this item into a named flyout submenu
};

// An ordered group of personalities that shares one dropdown button.
struct PersonalityCategory {
    std::string id;
    std::string btnLabel;   // HTML inside the dropdown button span
};

extern const std::vector<PersonalityDef>      kPersonalities;
extern const std::vector<PersonalityCategory> kPersonalityCategories;
const PersonalityDef* FindPersonality(const std::string& slug);

// Builds the full LLM prompt for any personality.
std::string BuildPersonalityPrompt(const PersonalityDef& def,
                                   const std::string& question,
                                   const std::string& userAnswer,
                                   const std::string& explanation);

// Renders all category dropdowns back-to-back.
// wrapperClass/btnClass/menuClass: CSS classes for the outer div, button span, menu div.
std::string RenderPersonalityDropdowns(const std::string& urlPrefix,
                                        const std::string& urlSuffix,
                                        const std::string& wrapperClass = "game-drop",
                                        const std::string& btnClass     = "explain-btn",
                                        const std::string& menuClass    = "game-menu");

// Generate CSS for a personality dropdown widget.
// hoverSelector: if non-empty, the wrapper starts hidden (opacity:0) and fades in when
//                this parent selector is hovered (e.g. ".chat-turn", ".saved-entry").
//                Pass "" for always-visible buttons.
std::string PersonalityDropdownCSS(const std::string& wrapperClass,
                                    const std::string& btnClass,
                                    const std::string& menuClass,
                                    const std::string& hoverSelector = "");

// Generate page-level JS click-toggle for personality dropdowns.
// Attaches onclick to every .{btnClass} inside .{wrapperClass}; toggles .open on the wrapper.
// Closes all open wrappers on outside click.
std::string PersonalityDropdownJS(const std::string& wrapperClass,
                                   const std::string& btnClass);

// Render past-session groups as interactive history above the active session.
// Each group has interactive toolbar buttons that use testtaker://h{action}/G/I URLs.
// Includes a clear-history link.
std::string RenderHistoryGroups(const std::vector<HistoryGroup>& groups);

// Build the persona thumbnail map: keyed by both normalized persona name (for tidbit
// turns, e.g. "albert_einstein") and kPersonalities displayQ (for explain turns).
// Returns an empty map when the personas directory doesn't exist or is empty.
std::map<std::string, std::string> LoadPersonalityThumbnails();

// Extract the tidbit persona key from a raw answer string.
// Scans for :::tidbit[Name] and returns NormalizePersonaName(Name), or "" if absent.
std::string TidbitPersonaKey(const std::string& answer);
