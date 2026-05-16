#include "session.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int test_session() {
    int failures = 0;

    // ScoreFromString / ScoreToString roundtrip
    {
        bool ok = ScoreFromString("correct") == Score::Correct
               && ScoreFromString("partial") == Score::Partial
               && ScoreFromString("missed")  == Score::Missed
               && ScoreFromString("skipped") == Score::Skipped
               && ScoreFromString("garbage") == Score::Skipped
               && ScoreToString(Score::Correct) == "correct"
               && ScoreToString(Score::Partial) == "partial"
               && ScoreToString(Score::Missed)  == "missed"
               && ScoreToString(Score::Skipped) == "skipped";
        if (!ok) {
            std::cerr << "FAIL [score-roundtrip]\n";
            ++failures;
        } else {
            std::cout << "PASS [score-roundtrip]\n";
        }
    }

    // ParseSession / SerializeSessionBody roundtrip with all fields
    {
        QuestionTurn t1;
        t1.question    = "What is RAII?";
        t1.userAnswer  = "Resource Acquisition Is Initialisation.";
        t1.score       = Score::Correct;
        t1.explanation = "Correct. RAII ties resource lifetimes to object scope.";
        t1.flagged     = false;

        QuestionTurn t2;
        t2.question    = "Explain move semantics.";
        t2.userAnswer  = "";
        t2.score       = Score::Skipped;
        t2.explanation = "Move semantics transfer ownership without copying.";
        t2.flagged     = true;

        std::vector<QuestionTurn> turns = {t1, t2};
        std::string body = SerializeSessionBody(turns);
        auto reparsed = ParseSession(body);

        bool ok = reparsed.size() == 2
               && reparsed[0].question    == t1.question
               && reparsed[0].userAnswer  == t1.userAnswer
               && reparsed[0].score       == Score::Correct
               && reparsed[0].explanation == t1.explanation
               && reparsed[0].flagged     == false
               && reparsed[1].question    == t2.question
               && reparsed[1].userAnswer  == ""
               && reparsed[1].score       == Score::Skipped
               && reparsed[1].flagged     == true;
        if (!ok) {
            std::cerr << "FAIL [session-roundtrip]: got " << reparsed.size() << " turns\n";
            if (reparsed.size() >= 1) {
                std::cerr << "  turn0.question='" << reparsed[0].question << "'\n";
                std::cerr << "  turn0.score='"    << ScoreToString(reparsed[0].score) << "'\n";
                std::cerr << "  turn0.flagged="   << reparsed[0].flagged << "\n";
            }
            ++failures;
        } else {
            std::cout << "PASS [session-roundtrip]\n";
        }
    }

    // AppendSessionTurn: creates :::session block when file has none
    {
        auto tmp = fs::temp_directory_path() / "test_session_append_new.md";
        std::ofstream f(tmp);
        f << "# Python Interview — 2026-05-16\n\n**Topic:** Python basics\n";
        f.close();

        QuestionTurn t;
        t.question    = "What is a list comprehension?";
        t.userAnswer  = "A concise way to create lists.";
        t.score       = Score::Correct;
        t.explanation = "Correct. `[x for x in iterable if cond]`";
        t.flagged     = false;

        bool ok = AppendSessionTurn(tmp.string(), t);
        if (!ok) {
            std::cerr << "FAIL [append-session-new]: AppendSessionTurn returned false\n";
            ++failures;
        } else {
            auto loaded = LoadSession(tmp.string());
            if (loaded.size() != 1
                || loaded[0].question   != t.question
                || loaded[0].userAnswer != t.userAnswer
                || loaded[0].score      != Score::Correct
                || loaded[0].flagged    != false) {
                std::cerr << "FAIL [append-session-new]: loaded " << loaded.size() << " turns\n";
                ++failures;
            } else {
                std::cout << "PASS [append-session-new]\n";
            }
        }
        fs::remove(tmp);
    }

    // AppendSessionTurn: appends to existing :::session block
    {
        auto tmp = fs::temp_directory_path() / "test_session_append_existing.md";
        std::ofstream f(tmp);
        f << "# Session\n\n"
             ":::session[Python basics]\n"
             "Q: First question?\n"
             "A: First answer.\n"
             "SCORE: correct\n"
             "FLAG: false\n"
             "EXPLANATION: Good.\n"
             "\n"
             ":::\n";
        f.close();

        QuestionTurn t;
        t.question    = "Second question?";
        t.userAnswer  = "Second answer.";
        t.score       = Score::Partial;
        t.explanation = "Partially right.";
        t.flagged     = true;

        bool ok = AppendSessionTurn(tmp.string(), t);
        if (!ok) {
            std::cerr << "FAIL [append-session-existing]: AppendSessionTurn returned false\n";
            ++failures;
        } else {
            auto loaded = LoadSession(tmp.string());
            if (loaded.size() != 2
                || loaded[1].question != "Second question?"
                || loaded[1].score    != Score::Partial
                || loaded[1].flagged  != true) {
                std::cerr << "FAIL [append-session-existing]: loaded " << loaded.size() << " turns\n";
                ++failures;
            } else {
                std::cout << "PASS [append-session-existing]\n";
            }
        }
        fs::remove(tmp);
    }

    // SetTurnFlagged: toggles flag on a specific turn
    {
        auto tmp = fs::temp_directory_path() / "test_session_flag.md";
        std::ofstream f(tmp);
        f << "# Session\n\n:::session[Topic]\n"
             "Q: Q1?\nA: A1.\nSCORE: correct\nFLAG: false\nEXPLANATION: Good.\n\n"
             "Q: Q2?\nA: A2.\nSCORE: missed\nFLAG: false\nEXPLANATION: Wrong.\n\n"
             ":::\n";
        f.close();

        bool ok = SetTurnFlagged(tmp.string(), 1, true);
        if (!ok) {
            std::cerr << "FAIL [set-turn-flagged]: SetTurnFlagged returned false\n";
            ++failures;
        } else {
            auto loaded = LoadSession(tmp.string());
            if (loaded.size() != 2
                || loaded[0].flagged != false
                || loaded[1].flagged != true) {
                std::cerr << "FAIL [set-turn-flagged]: flags wrong; turn0=" << loaded[0].flagged
                          << " turn1=" << loaded[1].flagged << "\n";
                ++failures;
            } else {
                std::cout << "PASS [set-turn-flagged]\n";
            }
        }
        fs::remove(tmp);
    }

    // LoadSession: returns empty vector when no :::session block exists
    {
        auto tmp = fs::temp_directory_path() / "test_session_empty.md";
        std::ofstream f(tmp);
        f << "# Just a header\n\nNo session block here.\n";
        f.close();

        auto loaded = LoadSession(tmp.string());
        if (!loaded.empty()) {
            std::cerr << "FAIL [load-session-empty]: expected 0 turns, got " << loaded.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [load-session-empty]\n";
        }
        fs::remove(tmp);
    }

    return failures;
}
