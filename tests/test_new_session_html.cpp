// tests/test_new_session_html.cpp
// Tests for BuildNewSessionHTML — no wxWidgets dependency.

#include "new_session_html.h"
#include <iostream>
#include <string>

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Helper: default state with no special fields set
static NewSessionFormState defaultState() {
    NewSessionFormState s;
    return s;
}

int test_new_session_html() {
    int failures = 0;

    // ── [ns-html-topic-input] ────────────────────────────────────────────────
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool ok = contains(html, "id='ns-topic'") || contains(html, "id=\"ns-topic\"");
        if (!ok) {
            std::cerr << "FAIL [ns-html-topic-input]: no ns-topic input found\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-topic-input]\n";
        }
    }

    // ── [ns-html-instr-textarea] ─────────────────────────────────────────────
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool ok = contains(html, "id='ns-instr'") || contains(html, "id=\"ns-instr\"");
        if (!ok) {
            std::cerr << "FAIL [ns-html-instr-textarea]: no ns-instr textarea found\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-instr-textarea]\n";
        }
    }

    // ── [ns-html-difficulty-select] ──────────────────────────────────────────
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool hasSelect = contains(html, "id='ns-difficulty'") || contains(html, "id=\"ns-difficulty\"");
        bool hasMixed  = contains(html, "mixed");
        bool hasEasy   = contains(html, "easy");
        bool hasMedium = contains(html, "medium");
        bool hasHard   = contains(html, "hard");
        bool ok = hasSelect && hasMixed && hasEasy && hasMedium && hasHard;
        if (!ok) {
            std::cerr << "FAIL [ns-html-difficulty-select]: sel=" << hasSelect
                      << " mixed=" << hasMixed << " easy=" << hasEasy
                      << " medium=" << hasMedium << " hard=" << hasHard << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-difficulty-select]\n";
        }
    }

    // ── [ns-html-questions-input] ────────────────────────────────────────────
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool ok = contains(html, "id='ns-questions'") || contains(html, "id=\"ns-questions\"");
        if (!ok) {
            std::cerr << "FAIL [ns-html-questions-input]: no ns-questions input found\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-questions-input]\n";
        }
    }

    // ── [ns-html-backend-select] ─────────────────────────────────────────────
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool hasSelect = contains(html, "id='ns-backend'") || contains(html, "id=\"ns-backend\"");
        bool hasClaudeP = contains(html, "claude -p");
        bool hasApi    = contains(html, "Anthropic API");
        bool hasOllama = contains(html, "Ollama (local)");
        bool hasClip   = contains(html, "Clipboard (manual)");
        bool ok = hasSelect && hasClaudeP && hasApi && hasOllama && hasClip;
        if (!ok) {
            std::cerr << "FAIL [ns-html-backend-select]: sel=" << hasSelect
                      << " claudep=" << hasClaudeP << " api=" << hasApi
                      << " ollama=" << hasOllama << " clip=" << hasClip << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-backend-select]\n";
        }
    }

    // ── [ns-html-backend-codex-gemini] ──────────────────────────────────────
    // Codex CLI and Gemini CLI must appear in the backend dropdown so the exam
    // tab can use those backends the same way the Create tab can.
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool hasCodex  = contains(html, "Codex CLI");
        bool hasGemini = contains(html, "Gemini CLI");
        if (!hasCodex || !hasGemini) {
            std::cerr << "FAIL [ns-html-backend-codex-gemini]: codex=" << hasCodex
                      << " gemini=" << hasGemini << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-backend-codex-gemini]\n";
        }
    }

    // ── [ns-html-apikey-hidden] ──────────────────────────────────────────────
    // Default backend is "claude -p" → apikey row must be hidden
    {
        NewSessionFormState s = defaultState();
        // backend defaults to "claude -p"
        std::string html = BuildNewSessionHTML(s);
        // The row must exist and have display:none
        bool hasRow    = contains(html, "ns-apikey-row");
        // When hidden, expect display:none in the element
        bool hasHidden = contains(html, "ns-apikey-row") &&
                         (contains(html, "ns-apikey-row' style='display:none'")
                       || contains(html, "ns-apikey-row\" style=\"display:none\"")
                       || contains(html, "ns-apikey-row' style=\"display:none\"")
                       || contains(html, "ns-apikey-row\" style='display:none'")
                       || contains(html, "display:none"));
        bool ok = hasRow && hasHidden;
        if (!ok) {
            std::cerr << "FAIL [ns-html-apikey-hidden]: hasRow=" << hasRow
                      << " hasHidden=" << hasHidden << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-apikey-hidden]\n";
        }
    }

    // ── [ns-html-apikey-shown-api] ───────────────────────────────────────────
    // When backend = "Anthropic API", the apikey row must NOT have display:none
    {
        NewSessionFormState s = defaultState();
        s.backend = "Anthropic API";
        std::string html = BuildNewSessionHTML(s);
        bool hasRow = contains(html, "ns-apikey-row");
        // The row should NOT have display:none when backend is Anthropic API
        // We check by looking for the row without display:none OR no display:none adjacent
        // Simpler: row must be present and the specific "apikey-row...display:none" pattern absent
        // or the apikey-row div does not contain display:none
        // Strategy: search for the row id — if the row exists but display:none is not nearby, ok.
        // The simplest approach: look for "ns-apikey-row" and confirm there's no "display:none"
        // immediately in the same id attribute context.
        bool noHiddenOnRow =
            !contains(html, "id='ns-apikey-row' style='display:none'") &&
            !contains(html, "id=\"ns-apikey-row\" style=\"display:none\"") &&
            !contains(html, "id='ns-apikey-row' style=\"display:none\"") &&
            !contains(html, "id=\"ns-apikey-row\" style='display:none'");
        bool ok = hasRow && noHiddenOnRow;
        if (!ok) {
            std::cerr << "FAIL [ns-html-apikey-shown-api]: hasRow=" << hasRow
                      << " noHidden=" << noHiddenOnRow << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-apikey-shown-api]\n";
        }
    }

    // ── [ns-html-ollama-hidden] ──────────────────────────────────────────────
    {
        NewSessionFormState s = defaultState();
        std::string html = BuildNewSessionHTML(s);
        bool hasRow    = contains(html, "ns-ollama-row");
        bool hasHidden = contains(html, "display:none");
        bool ok = hasRow && hasHidden;
        if (!ok) {
            std::cerr << "FAIL [ns-html-ollama-hidden]: hasRow=" << hasRow
                      << " hasHidden=" << hasHidden << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-ollama-hidden]\n";
        }
    }

    // ── [ns-html-corpus-hidden] ──────────────────────────────────────────────
    {
        NewSessionFormState s = defaultState();
        s.hasCorpus = false;
        std::string html = BuildNewSessionHTML(s);
        bool hasRow = contains(html, "ns-corpus-row");
        bool hasHidden =
            contains(html, "ns-corpus-row' style='display:none'") ||
            contains(html, "ns-corpus-row\" style=\"display:none\"") ||
            contains(html, "ns-corpus-row' style=\"display:none\"") ||
            contains(html, "ns-corpus-row\" style='display:none'");
        bool ok = hasRow && hasHidden;
        if (!ok) {
            std::cerr << "FAIL [ns-html-corpus-hidden]: hasRow=" << hasRow
                      << " hasHidden=" << hasHidden << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-corpus-hidden]\n";
        }
    }

    // ── [ns-html-corpus-shown] ───────────────────────────────────────────────
    {
        NewSessionFormState s = defaultState();
        s.hasCorpus = true;
        std::string html = BuildNewSessionHTML(s);
        bool hasRow = contains(html, "ns-corpus-row");
        bool noHidden =
            !contains(html, "id='ns-corpus-row' style='display:none'") &&
            !contains(html, "id=\"ns-corpus-row\" style=\"display:none\"") &&
            !contains(html, "id='ns-corpus-row' style=\"display:none\"") &&
            !contains(html, "id=\"ns-corpus-row\" style='display:none'");
        bool ok = hasRow && noHidden;
        if (!ok) {
            std::cerr << "FAIL [ns-html-corpus-shown]: hasRow=" << hasRow
                      << " noHidden=" << noHidden << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-corpus-shown]\n";
        }
    }

    // ── [ns-html-focus-area-rendered] ────────────────────────────────────────
    {
        NewSessionFormState s = defaultState();
        s.focusAreas.push_back({"Memory model", 4});
        std::string html = BuildNewSessionHTML(s);
        bool ok = contains(html, "ns-fa-row");
        if (!ok) {
            std::cerr << "FAIL [ns-html-focus-area-rendered]: no ns-fa-row\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-focus-area-rendered]\n";
        }
    }

    // ── [ns-html-start-button] ───────────────────────────────────────────────
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool ok = contains(html, "ns-start-btn");
        if (!ok) {
            std::cerr << "FAIL [ns-html-start-button]: no ns-start-btn\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-start-button]\n";
        }
    }

    // ── [ns-html-nscollect-fn] ───────────────────────────────────────────────
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool ok = contains(html, "nsCollectForm");
        if (!ok) {
            std::cerr << "FAIL [ns-html-nscollect-fn]: no nsCollectForm\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-nscollect-fn]\n";
        }
    }

    // ── [ns-html-nsaction-fn] ────────────────────────────────────────────────
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool ok = contains(html, "nsAction");
        if (!ok) {
            std::cerr << "FAIL [ns-html-nsaction-fn]: no nsAction\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-nsaction-fn]\n";
        }
    }

    // ── [ns-html-project-label] ──────────────────────────────────────────────
    {
        NewSessionFormState s = defaultState();
        s.projectName = "MyProject";
        std::string html = BuildNewSessionHTML(s);
        bool ok = contains(html, "MyProject");
        if (!ok) {
            std::cerr << "FAIL [ns-html-project-label]: projectName not in output\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-project-label]\n";
        }
    }

    // ── [ns-html-no-project-label] ───────────────────────────────────────────
    {
        NewSessionFormState s = defaultState();
        s.projectName = "";
        std::string html = BuildNewSessionHTML(s);
        // some indication that no project is active — e.g. "(none" text
        bool ok = contains(html, "none") || contains(html, "No project")
               || contains(html, "no project") || contains(html, "activate");
        if (!ok) {
            std::cerr << "FAIL [ns-html-no-project-label]: no 'none' indication\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-no-project-label]\n";
        }
    }

    // ── [ns-html-status-shown] ───────────────────────────────────────────────
    {
        NewSessionFormState s = defaultState();
        s.statusMsg = "Loading Ollama models...";
        std::string html = BuildNewSessionHTML(s);
        bool ok = contains(html, "Loading Ollama models...");
        if (!ok) {
            std::cerr << "FAIL [ns-html-status-shown]: statusMsg not in output\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-status-shown]\n";
        }
    }

    // ── [ns-html-tidbit-count-input] ─────────────────────────────────────────
    // A tidbit-count number input (1–5) must be present so the user can control
    // how many persona tidbit blocks the LLM produces per exam question.
    {
        NewSessionFormState s = defaultState();
        s.tidbitCount = 3;
        std::string html = BuildNewSessionHTML(s);
        bool hasInput = contains(html, "ns-tidbit-count");
        bool hasValue = contains(html, "value='3'") || contains(html, "value=\"3\"");
        bool collects = contains(html, "ns-tidbit-count");
        if (!hasInput || !hasValue || !collects) {
            std::cerr << "FAIL [ns-html-tidbit-count-input]:"
                      << " input=" << hasInput
                      << " value=" << hasValue << "\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-tidbit-count-input]\n";
        }
    }

    // ── [ns-html-tidbit-count-collected] ─────────────────────────────────────
    // nsCollectForm() must include tidbitCount so HandleStart receives it.
    {
        std::string html = BuildNewSessionHTML(defaultState());
        bool ok = contains(html, "tidbitCount");
        if (!ok) {
            std::cerr << "FAIL [ns-html-tidbit-count-collected]: tidbitCount missing from JS\n";
            ++failures;
        } else {
            std::cout << "PASS [ns-html-tidbit-count-collected]\n";
        }
    }

    return failures;
}
