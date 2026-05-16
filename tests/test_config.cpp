#include "config.h"
#include <iostream>

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

    return failures;
}
