#include "llm_error.h"
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

    return failures;
}
