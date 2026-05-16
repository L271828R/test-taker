#include "llm_error.h"

static std::string trim(const std::string& s) {
    const std::string ws = " \t\r\n";
    auto start = s.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

std::string FormatLLMError(const std::string& summary,
                           const std::string& commandOutput) {
    std::string details = trim(commandOutput);
    if (details.empty()) return summary;

    constexpr std::size_t kMaxDetails = 1200;
    if (details.size() > kMaxDetails) {
        details = details.substr(0, kMaxDetails) + "\n...";
    }

    return summary + "\n\n" + details;
}
