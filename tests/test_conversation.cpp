#include "conversation.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>

namespace fs = std::filesystem;

int test_conversation() {
    int failures = 0;

    // ParseConversation: basic Q/A parsing
    {
        std::string body =
            "Q: What is a stack frame?\n"
            "A: A contiguous region of stack memory.\n\n"
            "Q: Does Valgrind see them?\n"
            "A: No, only heap.\n\n";
        auto turns = ParseConversation(body);
        if (turns.size() != 2
            || turns[0].question != "What is a stack frame?"
            || turns[0].answer   != "A contiguous region of stack memory."
            || turns[1].question != "Does Valgrind see them?"
            || turns[1].answer   != "No, only heap.") {
            std::cerr << "FAIL [parse-conversation]: got " << turns.size() << " turns\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-conversation]\n";
        }
    }

    // SerializeConversationBody: roundtrip
    {
        std::vector<ConversationTurn> turns = {
            {"What is RAII?", "Resource Acquisition Is Initialisation."},
            {"Why use it?",   "Guarantees cleanup on scope exit."},
        };
        std::string body = SerializeConversationBody(turns);
        auto reparsed = ParseConversation(body);
        if (reparsed.size() != 2
            || reparsed[0].question != turns[0].question
            || reparsed[1].answer   != turns[1].answer) {
            std::cerr << "FAIL [serialize-conversation-roundtrip]\n";
            ++failures;
        } else {
            std::cout << "PASS [serialize-conversation-roundtrip]\n";
        }
    }

    // AppendTurn: creates a block when none exists
    {
        auto tmp = fs::temp_directory_path() / "test_conv_append.md";
        std::ofstream f(tmp);
        f << "<!-- ch:2 -->\n## Chapter 2: The Heap\n\nContent here.\n\n---\n\n"
             "<!-- ch:3 -->\n## Chapter 3: RAII\n\nMore content.\n";
        f.close();

        ConversationTurn t{"What is the heap?", "A pool of unstructured memory."};
        bool ok = AppendTurn(tmp.string(), 2, "Chapter 2: The Heap", t);
        if (!ok) {
            std::cerr << "FAIL [append-turn-creates]: AppendTurn returned false\n";
            ++failures;
        } else {
            auto loaded = LoadConversation(tmp.string(), 2);
            if (loaded.size() != 1
                || loaded[0].question != t.question
                || loaded[0].answer   != t.answer) {
                std::cerr << "FAIL [append-turn-creates]: loaded " << loaded.size() << " turns\n";
                ++failures;
            } else {
                std::cout << "PASS [append-turn-creates]\n";
            }
        }
        fs::remove(tmp);
    }

    // AppendTurn: appends to existing block
    {
        auto tmp = fs::temp_directory_path() / "test_conv_existing.md";
        std::ofstream f(tmp);
        f << "<!-- ch:0 -->\n## Chapter 1: Basics\n\n"
             ":::conversation[Chapter 1: Basics]\n"
             "Q: First question?\nA: First answer.\n\n"
             ":::\n";
        f.close();

        ConversationTurn t{"Second question?", "Second answer."};
        AppendTurn(tmp.string(), 0, "Chapter 1: Basics", t);
        auto loaded = LoadConversation(tmp.string(), 0);
        if (loaded.size() != 2
            || loaded[1].question != "Second question?"
            || loaded[1].answer   != "Second answer.") {
            std::cerr << "FAIL [append-turn-existing]: loaded " << loaded.size() << " turns\n";
            ++failures;
        } else {
            std::cout << "PASS [append-turn-existing]\n";
        }
        fs::remove(tmp);
    }

    // DeleteTurn: removes middle turn; block and remaining turns survive
    {
        auto tmp = fs::temp_directory_path() / "test_conv_delete_mid.md";
        std::ofstream f(tmp);
        f << "<!-- ch:1 -->\n## Chapter 1\n\n"
             ":::conversation[Chapter 1]\n"
             "Q: First?\nA: First answer.\n\n"
             "Q: Second?\nA: Second answer.\n\n"
             "Q: Third?\nA: Third answer.\n\n"
             ":::\n";
        f.close();

        bool ok = DeleteTurn(tmp.string(), 1, 1); // delete "Second?"
        if (!ok) {
            std::cerr << "FAIL [delete-turn-middle]: DeleteTurn returned false\n";
            ++failures;
        } else {
            auto loaded = LoadConversation(tmp.string(), 1);
            if (loaded.size() != 2
                || loaded[0].question != "First?"
                || loaded[1].question != "Third?") {
                std::cerr << "FAIL [delete-turn-middle]: got " << loaded.size() << " turns\n";
                ++failures;
            } else {
                std::cout << "PASS [delete-turn-middle]\n";
            }
        }
        fs::remove(tmp);
    }

    // DeleteTurn: deleting the only turn removes the whole :::conversation block
    {
        auto tmp = fs::temp_directory_path() / "test_conv_delete_last.md";
        std::ofstream f(tmp);
        f << "<!-- ch:0 -->\n## Chapter 0\n\nParagraph.\n\n"
             ":::conversation[Chapter 0]\n"
             "Q: Only question?\nA: Only answer.\n\n"
             ":::\n"
             "<!-- ch:1 -->\n## Chapter 1\n\nNext.\n";
        f.close();

        bool ok = DeleteTurn(tmp.string(), 0, 0);
        if (!ok) {
            std::cerr << "FAIL [delete-turn-only]: DeleteTurn returned false\n";
            ++failures;
        } else {
            auto loaded = LoadConversation(tmp.string(), 0);
            // Read file to confirm block is gone
            std::ifstream rf(tmp);
            std::string contents((std::istreambuf_iterator<char>(rf)), {});
            bool blockGone = contents.find(":::conversation[") == std::string::npos;
            bool ch1intact = contents.find("<!-- ch:1 -->") != std::string::npos;
            if (!loaded.empty() || !blockGone || !ch1intact) {
                std::cerr << "FAIL [delete-turn-only]: block not removed; turns=" << loaded.size() << "\n";
                ++failures;
            } else {
                std::cout << "PASS [delete-turn-only]\n";
            }
        }
        fs::remove(tmp);
    }

    // BuildQAPrompt: contains chapter title and question
    {
        std::vector<ConversationTurn> history = {{"Q1", "A1"}};
        std::string prompt = BuildQAPrompt("doc content", "Chapter 2: RAII",
                                           history, "What is RAII?");
        bool hasTitle    = prompt.find("Chapter 2: RAII") != std::string::npos;
        bool hasQuestion = prompt.find("What is RAII?")   != std::string::npos;
        bool hasHistory  = prompt.find("Q1") != std::string::npos;
        if (!hasTitle || !hasQuestion || !hasHistory) {
            std::cerr << "FAIL [build-qa-prompt]\n";
            ++failures;
        } else {
            std::cout << "PASS [build-qa-prompt]\n";
        }
    }

    return failures;
}
