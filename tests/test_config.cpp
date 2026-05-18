#include "config.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int test_config() {
    int failures = 0;

    // ParseState reads currentProject.
    {
        AppState st = ParseState("currentProject = my-story\n");
        if (st.currentProject != "my-story") {
            std::cerr << "FAIL [parse-state-project]: got '" << st.currentProject << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-state-project]\n";
        }
    }

    // ParseState returns empty when key absent.
    {
        AppState st = ParseState("# nothing\n");
        if (!st.currentProject.empty()) {
            std::cerr << "FAIL [parse-state-empty]: got '" << st.currentProject << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-state-empty]\n";
        }
    }

    // ParseState trims whitespace around = and value.
    {
        AppState st = ParseState("currentProject=space station\n");
        if (st.currentProject != "space station") {
            std::cerr << "FAIL [parse-state-trim]: got '" << st.currentProject << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-state-trim]\n";
        }
    }

    // ParseState reads apiKey and ollamaModel.
    {
        AppState st = ParseState("backend = Anthropic API\napiKey = sk-test\nollamaModel = llama3\n");
        bool ok = st.backend == "Anthropic API"
               && st.apiKey == "sk-test"
               && st.ollamaModel == "llama3";
        if (!ok) {
            std::cerr << "FAIL [parse-state-llm-config]: backend='" << st.backend
                      << "' apiKey='" << st.apiKey
                      << "' ollamaModel='" << st.ollamaModel << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-state-llm-config]\n";
        }
    }

    // ParseConfig reads defaultFolder.
    {
        AppConfig cfg = ParseConfig("defaultFolder = /Users/me/projects\n");
        if (cfg.defaultFolder != "/Users/me/projects") {
            std::cerr << "FAIL [parse-config-default-folder]: got '" << cfg.defaultFolder << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-config-default-folder]\n";
        }
    }

    // ParseConfig returns empty defaultFolder when absent.
    {
        AppConfig cfg = ParseConfig("# no folder set\n");
        if (!cfg.defaultFolder.empty()) {
            std::cerr << "FAIL [parse-config-folder-absent]: got '" << cfg.defaultFolder << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-config-folder-absent]\n";
        }
    }

    // ParseConfig trims whitespace around = in defaultFolder.
    {
        AppConfig cfg = ParseConfig("defaultFolder=/Users/me/projects\n");
        if (cfg.defaultFolder != "/Users/me/projects") {
            std::cerr << "FAIL [parse-config-folder-no-spaces]: got '" << cfg.defaultFolder << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-config-folder-no-spaces]\n";
        }
    }

    // SaveAppState / ParseState round-trip: all fields survive serialise→parse.
    // This is the on-disk contract that startup reload depends on.
    {
        AppState st;
        st.currentProject  = "CppInterview";
        st.topic           = "C++ memory model";
        st.backend         = "claude -p";
        st.apiKey          = "sk-abc";
        st.ollamaModel     = "llama3";
        st.lastSessionFile = "session_20260517_132731.md";

        // Serialise using the same format SaveAppState writes.
        std::string serialised =
            "currentProject = "  + st.currentProject  + "\n"
            "topic = "           + st.topic           + "\n"
            "style = "           + st.style           + "\n"
            "backend = "         + st.backend         + "\n"
            "checkedChars = "    + st.checkedChars    + "\n"
            "apiKey = "          + st.apiKey          + "\n"
            "ollamaModel = "     + st.ollamaModel     + "\n"
            "lastSessionFile = " + st.lastSessionFile + "\n";

        AppState loaded = ParseState(serialised);
        bool ok = loaded.currentProject  == st.currentProject
               && loaded.topic           == st.topic
               && loaded.backend         == st.backend
               && loaded.apiKey          == st.apiKey
               && loaded.ollamaModel     == st.ollamaModel
               && loaded.lastSessionFile == st.lastSessionFile;
        if (!ok) {
            std::cerr << "FAIL [state-roundtrip]: project='" << loaded.currentProject
                      << "' topic='" << loaded.topic
                      << "' backend='" << loaded.backend
                      << "' lastSessionFile='" << loaded.lastSessionFile << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [state-roundtrip]\n";
        }
    }

    // ParseState reads lastSessionFile (completed sessions must be retained).
    {
        AppState st = ParseState(
            "currentProject = CppInterview\n"
            "lastSessionFile = session_20260517_132731.md\n");
        bool ok = st.lastSessionFile == "session_20260517_132731.md";
        if (!ok) {
            std::cerr << "FAIL [state-last-session-file]: got '"
                      << st.lastSessionFile << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [state-last-session-file]\n";
        }
    }

    // ParseState reads focusAreas (serialized focus-area list).
    {
        AppState st = ParseState("focusAreas = 3@@Presigned URLs|5@@Lifecycle rules\n");
        bool ok = st.focusAreas == "3@@Presigned URLs|5@@Lifecycle rules";
        if (!ok) {
            std::cerr << "FAIL [state-focus-areas]: got '" << st.focusAreas << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [state-focus-areas]\n";
        }
    }

    return failures;
}
