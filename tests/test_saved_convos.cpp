#include "saved_convos.h"
#include <cassert>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int test_saved_convos() {
    int failures = 0;

    fs::path tmpDir = fs::temp_directory_path() / "tt_saved_convos_test";
    fs::create_directories(tmpDir);
    std::string dir = tmpDir.string();
    (void)fs::remove(fs::path(dir) / "saved_convos.md");

    // Append and load single entry
    {
        bool ok = AppendSavedConvo(dir, "What is RAII?",
                                   "RAII ties resource lifetimes to object scope.",
                                   "2026-05-18");
        if (!ok) {
            std::cerr << "FAIL [saved-append-returns-true]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-append-returns-true]\n";
        }

        auto convos = LoadSavedConvos(dir);
        bool loadOk = convos.size() == 1
                   && convos[0].question    == "What is RAII?"
                   && convos[0].explanation == "RAII ties resource lifetimes to object scope."
                   && convos[0].date        == "2026-05-18";
        if (!loadOk) {
            std::cerr << "FAIL [saved-load-single]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-load-single]\n";
        }
    }

    // Append second entry and confirm both load
    {
        AppendSavedConvo(dir, "What is move semantics?",
                         "Move semantics transfer ownership without copying.",
                         "2026-05-19");
        auto convos = LoadSavedConvos(dir);
        bool ok = convos.size() == 2
               && convos[1].question    == "What is move semantics?"
               && convos[1].explanation == "Move semantics transfer ownership without copying."
               && convos[1].date        == "2026-05-19";
        if (!ok) {
            std::cerr << "FAIL [saved-load-two]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-load-two]\n";
        }
    }

    // Multi-line explanation survives roundtrip
    {
        std::string multiLine = "First line.\nSecond line.\nThird line with a backslash: C:\\path.";
        AppendSavedConvo(dir, "Multi-line question?", multiLine, "2026-05-20");
        auto convos = LoadSavedConvos(dir);
        bool ok = convos.size() == 3
               && convos[2].explanation == multiLine;
        if (!ok) {
            std::cerr << "FAIL [saved-multiline-roundtrip]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-multiline-roundtrip]\n";
        }
    }

    // Load from non-existent dir returns empty
    {
        auto convos = LoadSavedConvos("/tmp/no_such_dir_tt_saved");
        if (!convos.empty()) {
            std::cerr << "FAIL [saved-load-missing-dir]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-load-missing-dir]\n";
        }
    }

    // BuildSavedConvosHTML produces non-empty HTML with question text
    {
        auto convos = LoadSavedConvos(dir);
        std::string html = BuildSavedConvosHTML(convos);
        bool ok = html.find("What is RAII?") != std::string::npos
               && html.find("What is move semantics?") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [saved-html-contains-questions]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-html-contains-questions]\n";
        }
    }

    // BuildSavedConvosHTML includes delete links
    {
        auto convos = LoadSavedConvos(dir);
        std::string html = BuildSavedConvosHTML(convos);
        bool ok = html.find("testtaker://delete-saved/") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [saved-html-has-delete-links]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-html-has-delete-links]\n";
        }
    }

    // Delete middle entry — first and third survive
    {
        // dir now has 3 entries: RAII (0), move semantics (1), multi-line (2)
        bool ok = DeleteSavedConvo(dir, 1);  // remove "move semantics"
        auto convos = LoadSavedConvos(dir);
        bool loadOk = ok
                   && convos.size() == 2
                   && convos[0].question == "What is RAII?"
                   && convos[1].question == "Multi-line question?";
        if (!loadOk) {
            std::cerr << "FAIL [saved-delete-middle]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-delete-middle]\n";
        }
    }

    // Delete out-of-range returns false
    {
        bool ok = !DeleteSavedConvo(dir, 99);
        if (!ok) {
            std::cerr << "FAIL [saved-delete-out-of-range]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-delete-out-of-range]\n";
        }
    }

    // fromGame flag round-trips through file
    {
        AppendSavedConvo(dir, "Game question?", "Correct: B", "2026-05-21", /*fromGame=*/true);
        auto convos = LoadSavedConvos(dir);
        bool ok = convos.size() == 3
               && convos[2].question  == "Game question?"
               && convos[2].fromGame  == true;
        if (!ok) {
            std::cerr << "FAIL [saved-fromgame-roundtrip]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-fromgame-roundtrip]\n";
        }
    }

    // Non-game entry has fromGame == false
    {
        auto convos = LoadSavedConvos(dir);
        bool ok = convos.size() == 3
               && convos[0].fromGame == false;   // RAII entry, no src: game
        if (!ok) {
            std::cerr << "FAIL [saved-fromgame-default-false]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-fromgame-default-false]\n";
        }
    }

    // BuildSavedConvosHTML shows game badge for fromGame entries
    {
        auto convos = LoadSavedConvos(dir);
        std::string html = BuildSavedConvosHTML(convos);
        bool ok = html.find("game-badge") != std::string::npos
               || html.find("\xF0\x9F\x8E\xAE") != std::string::npos;  // 🎮 UTF-8
        if (!ok) {
            std::cerr << "FAIL [saved-fromgame-html-badge]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-fromgame-html-badge]\n";
        }
    }

    // DeleteSavedConvo preserves fromGame on surviving entries
    {
        // dir now has 3 entries: RAII(0,fromGame=false), multi-line(1,fromGame=false), game(2,fromGame=true)
        bool ok = DeleteSavedConvo(dir, 1);  // remove multi-line
        auto convos = LoadSavedConvos(dir);
        bool loadOk = ok
                   && convos.size() == 2
                   && convos[0].fromGame == false
                   && convos[1].fromGame == true;
        if (!loadOk) {
            std::cerr << "FAIL [saved-fromgame-delete-preserves]\n";
            ++failures;
        } else {
            std::cout << "PASS [saved-fromgame-delete-preserves]\n";
        }
    }

    fs::remove_all(tmpDir);
    return failures;
}
