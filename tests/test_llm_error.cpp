#include "llm_error.h"
#include "llm.h"
#include <iostream>
#include <string>

int test_llm_error() {
    int failures = 0;

    {
        std::string msg = FormatLLMError("command failed (exit 1)",
                                         "\nYou've hit your limit · resets 3:20am\n");
        bool hasSummary = msg.find("command failed (exit 1)") != std::string::npos;
        bool hasDetails = msg.find("resets 3:20am") != std::string::npos;
        if (!hasSummary || !hasDetails) {
            std::cerr << "FAIL [llm-error-details]: msg='" << msg << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [llm-error-details]\n";
        }
    }

    {
        std::string msg = FormatLLMError("command failed (exit 1)", " \n\t ");
        if (msg != "command failed (exit 1)") {
            std::cerr << "FAIL [llm-error-empty-output]: msg='" << msg << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [llm-error-empty-output]\n";
        }
    }

    // BuildLoginShellCmd must cd to $HOME before invoking bash -l so that
    // a deleted CWD (getcwd failure) doesn't abort shell initialisation.
    {
        std::string cmd = BuildLoginShellCmd("echo hi");
        bool hasHomeCd  = cmd.find("cd \"$HOME\"") != std::string::npos;
        bool hasBashL   = cmd.find("bash -l -c") != std::string::npos;
        bool hasRedirect = cmd.find("2>&1") != std::string::npos;
        if (!hasHomeCd || !hasBashL || !hasRedirect) {
            std::cerr << "FAIL [llm-shell-cmd-cwd]: cmd='" << cmd << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [llm-shell-cmd-cwd]\n";
        }
    }

    return failures;
}
