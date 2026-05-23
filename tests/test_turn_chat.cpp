#include "turn_chat.h"
#include "session.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int test_turn_chat() {
    int failures = 0;

    // ParseTurnChat / SerializeTurnChatBody roundtrip
    {
        TurnChatTurn t1{"Why did I get partial?",
                        "Your answer lacked mention of move semantics."};
        TurnChatTurn t2{"What should I have said?", "You needed to explain..."};

        std::string body = SerializeTurnChatBody({t1, t2});
        auto reparsed = ParseTurnChat(body);

        bool ok = reparsed.size() == 2
               && reparsed[0].question == t1.question
               && reparsed[0].answer   == t1.answer
               && reparsed[1].question == t2.question
               && reparsed[1].answer   == t2.answer;
        if (!ok) {
            std::cerr << "FAIL [turn-chat-roundtrip]: got " << reparsed.size() << " turns\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-roundtrip]\n";
        }
    }

    // LoadTurnChat: returns empty when no :::chat block for that index
    {
        auto tmp = fs::temp_directory_path() / "test_turn_chat_empty.md";
        std::ofstream f(tmp);
        f << "# Session\n\n:::session[Topic]\n"
             "Q: Q1?\nA: A1.\nSCORE: correct\nFLAG: false\nEXPLANATION: Good.\n\n"
             ":::\n";
        f.close();

        auto turns = LoadTurnChat(tmp.string(), 0);
        if (!turns.empty()) {
            std::cerr << "FAIL [turn-chat-load-empty]: expected 0 turns, got "
                      << turns.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-load-empty]\n";
        }
        fs::remove(tmp);
    }

    // AppendTurnChatTurn: creates :::chat[N] block and can be loaded back
    {
        auto tmp = fs::temp_directory_path() / "test_turn_chat_append.md";
        std::ofstream f(tmp);
        f << "# Session\n\n:::session[Topic]\n"
             "Q: Q1?\nA: A1.\nSCORE: correct\nFLAG: false\nEXPLANATION: Good.\n\n"
             "Q: Q2?\nA: A2.\nSCORE: missed\nFLAG: false\nEXPLANATION: Wrong.\n\n"
             ":::\n";
        f.close();

        TurnChatTurn t{"Why partial?", "Because you missed X."};
        bool ok = AppendTurnChatTurn(tmp.string(), 1, t);
        if (!ok) {
            std::cerr << "FAIL [turn-chat-append]: AppendTurnChatTurn returned false\n";
            ++failures;
        } else {
            auto loaded = LoadTurnChat(tmp.string(), 1);
            bool loadOk = loaded.size() == 1
                       && loaded[0].question == "Why partial?"
                       && loaded[0].answer   == "Because you missed X.";
            if (!loadOk) {
                std::cerr << "FAIL [turn-chat-append]: loaded " << loaded.size() << " turns"
                          << (loaded.empty() ? "" : " q='" + loaded[0].question + "'") << "\n";
                ++failures;
            } else {
                std::cout << "PASS [turn-chat-append]\n";
            }
        }
        fs::remove(tmp);
    }

    // AppendTurnChatTurn: appends to existing :::chat[N] block
    {
        auto tmp = fs::temp_directory_path() / "test_turn_chat_append2.md";
        std::ofstream f(tmp);
        f << "# Session\n\n:::session[Topic]\n"
             "Q: Q1?\nA: A1.\nSCORE: correct\nFLAG: false\nEXPLANATION: Good.\n\n"
             ":::\n"
             "\n:::chat[0]\n"
             "Q: First chat question?\n"
             "A: First chat answer.\n\n"
             ":::\n";
        f.close();

        TurnChatTurn t{"Second question?", "Second answer."};
        bool ok = AppendTurnChatTurn(tmp.string(), 0, t);
        if (!ok) {
            std::cerr << "FAIL [turn-chat-append2]: AppendTurnChatTurn returned false\n";
            ++failures;
        } else {
            auto loaded = LoadTurnChat(tmp.string(), 0);
            bool loadOk = loaded.size() == 2
                       && loaded[1].question == "Second question?"
                       && loaded[1].answer   == "Second answer.";
            if (!loadOk) {
                std::cerr << "FAIL [turn-chat-append2]: loaded " << loaded.size() << " turns\n";
                ++failures;
            } else {
                std::cout << "PASS [turn-chat-append2]\n";
            }
        }
        fs::remove(tmp);
    }

    // LoadTurnChat: only loads the block for the requested turn index
    {
        auto tmp = fs::temp_directory_path() / "test_turn_chat_isolation.md";
        std::ofstream f(tmp);
        f << "# Session\n\n:::session[Topic]\n"
             "Q: Q1?\nA: A1.\nSCORE: correct\nFLAG: false\nEXPLANATION: Good.\n\n"
             "Q: Q2?\nA: A2.\nSCORE: missed\nFLAG: false\nEXPLANATION: Wrong.\n\n"
             ":::\n"
             "\n:::chat[0]\n"
             "Q: Chat for turn 0?\nA: Answer 0.\n\n"
             ":::\n"
             "\n:::chat[1]\n"
             "Q: Chat for turn 1?\nA: Answer 1.\n\n"
             ":::\n";
        f.close();

        auto turns0 = LoadTurnChat(tmp.string(), 0);
        auto turns1 = LoadTurnChat(tmp.string(), 1);
        bool ok = turns0.size() == 1 && turns0[0].question == "Chat for turn 0?"
               && turns1.size() == 1 && turns1[0].question == "Chat for turn 1?";
        if (!ok) {
            std::cerr << "FAIL [turn-chat-isolation]: t0=" << turns0.size()
                      << " t1=" << turns1.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-isolation]\n";
        }
        fs::remove(tmp);
    }

    // BuildTurnChatPrompt: includes question, answer, score, explanation, history
    {
        QuestionTurn examTurn;
        examTurn.question    = "What is RAII?";
        examTurn.userAnswer  = "Resource management.";
        examTurn.score       = Score::Star3;
        examTurn.explanation = "Partially correct — you missed the scope aspect.";

        TurnChatTurn h1{"Why partial?", "Because you omitted the scope binding."};
        std::vector<TurnChatTurn> history = {h1};

        std::string prompt = BuildTurnChatPrompt(examTurn, history, "What should I have said?");

        bool hasQuestion    = prompt.find("What is RAII?")          != std::string::npos;
        bool hasAnswer      = prompt.find("Resource management.")    != std::string::npos;
        bool hasScore       = prompt.find("partial")                 != std::string::npos;
        bool hasExplanation = prompt.find("scope aspect")            != std::string::npos;
        bool hasHistory     = prompt.find("Why partial?")            != std::string::npos;
        bool hasNewQ        = prompt.find("What should I have said?") != std::string::npos;

        bool ok = hasQuestion && hasAnswer && hasScore && hasExplanation && hasHistory && hasNewQ;
        if (!ok) {
            std::cerr << "FAIL [turn-chat-prompt]:"
                      << " q=" << hasQuestion << " a=" << hasAnswer
                      << " s=" << hasScore    << " e=" << hasExplanation
                      << " h=" << hasHistory  << " nq=" << hasNewQ << "\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-prompt]\n";
        }
    }

    // BuildTurnChatPrompt injects corpus context when provided
    {
        QuestionTurn qt;
        qt.question   = "What is test control?";
        qt.userAnswer = "I don't know";
        qt.score      = Score::Star1;
        qt.explanation = "Test control is taking corrective actions based on monitoring.";

        std::string corpus = "Excerpt: test control involves adjusting the plan when deviations occur.";
        std::string prompt = BuildTurnChatPrompt(qt, {}, "explain in more detail", corpus);

        bool hasCorpus = prompt.find(corpus) != std::string::npos;
        if (!hasCorpus) {
            std::cerr << "FAIL [turn-chat-corpus-context]: corpus excerpt not in prompt\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-corpus-context]\n";
        }
    }

    // BuildTurnChatPrompt without corpus context works as before
    {
        QuestionTurn qt;
        qt.question    = "What is X?";
        qt.userAnswer  = "Y";
        qt.score       = Score::Star5;
        qt.explanation = "Correct.";

        std::string prompt = BuildTurnChatPrompt(qt, {}, "tell me more");
        bool hasQ = prompt.find("What is X?") != std::string::npos;
        if (!hasQ) {
            std::cerr << "FAIL [turn-chat-no-corpus]: question not in prompt\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-no-corpus]\n";
        }
    }

    // ParseTurnChat must preserve blank lines within an answer so that
    // multi-paragraph LLM responses survive the serialize → parse round-trip.
    {
        TurnChatTurn t{"How does RAII work?",
                       "First paragraph about RAII.\n\nSecond paragraph with more detail."};

        std::string body = SerializeTurnChatBody({t});
        auto reparsed = ParseTurnChat(body);

        bool ok = reparsed.size() == 1
               && reparsed[0].answer.find("\n\n") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [turn-chat-multiline-answer]: paragraph break lost in round-trip; "
                      << "answer='" << (reparsed.empty() ? "(empty)" : reparsed[0].answer) << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-multiline-answer]\n";
        }
    }

    // BuildTurnChatHTML must NOT include the ctx context strip (score + explanation).
    // The left-hand exam panel already shows the question context; duplicating it
    // in the side panel serves no purpose and clutters the view.
    {
        QuestionTurn turn;
        turn.question    = "What is RAII?";
        turn.userAnswer  = "Resource management.";
        turn.score       = Score::Star3;
        turn.explanation = "Partially correct — missed scope binding.";

        std::string html = BuildTurnChatHTML(turn, 0, {}, false);

        bool hasCtx = html.find("class='ctx'") != std::string::npos;
        if (hasCtx) {
            std::cerr << "FAIL [turn-chat-no-ctx-strip]: ctx div found in HTML\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-no-ctx-strip]\n";
        }

        bool hasExplanation = html.find("missed scope binding") != std::string::npos;
        if (hasExplanation) {
            std::cerr << "FAIL [turn-chat-no-ctx-explanation]: explanation text leaked into HTML\n";
            ++failures;
        } else {
            std::cout << "PASS [turn-chat-no-ctx-explanation]\n";
        }
    }

    return failures;
}
