#include "meta.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int test_meta() {
    int failures = 0;

    // RecordOpen writes lastOpened; reload confirms it is set.
    {
        auto dir = fs::temp_directory_path() / "st_meta_open";
        fs::create_directories(dir);
        EnsureProjectMeta(dir.string(), "Claude -p");
        RecordOpen(dir.string());
        auto meta = LoadProjectMeta(dir.string());
        bool ok = meta.lastOpened.size() == 19   // "YYYY-MM-DDTHH:MM:SS"
               && meta.lastOpened[4] == '-'
               && meta.lastOpened[10] == 'T'
               && meta.created.size() == 19
               && meta.source == "Claude -p";
        if (!ok) {
            std::cerr << "FAIL [meta-record-open]: lastOpened='" << meta.lastOpened << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [meta-record-open]\n";
        }
        fs::remove_all(dir);
    }

    // RecordLLMTiming appends an entry that round-trips correctly.
    {
        auto dir = fs::temp_directory_path() / "st_meta_timing";
        fs::create_directories(dir);
        RecordLLMTiming(dir.string(), "generate", "JK Rowling Ch1", 87);
        RecordLLMTiming(dir.string(), "patch",    "fix chapter 2",  12);
        auto meta = LoadProjectMeta(dir.string());
        bool ok = meta.timings.size() == 2
               && meta.timings[0].operation       == "generate"
               && meta.timings[0].topic           == "JK Rowling Ch1"
               && meta.timings[0].durationSeconds == 87
               && meta.timings[1].operation       == "patch"
               && meta.timings[1].durationSeconds == 12;
        if (!ok) {
            std::cerr << "FAIL [meta-record-timing]: got " << meta.timings.size() << " entries\n";
            ++failures;
        } else {
            std::cout << "PASS [meta-record-timing]\n";
        }
        fs::remove_all(dir);
    }

    // SaveProjectMeta / LoadProjectMeta round-trip preserves all fields.
    {
        auto dir = fs::temp_directory_path() / "st_meta_roundtrip";
        fs::create_directories(dir);
        ProjectMeta m;
        m.created = "2026-05-16T14:00:00";
        m.lastOpened = "2026-05-16T14:23:00";
        m.source = "Codex CLI";
        m.timings.push_back({"2026-05-16T14:20:00", "generate", "topic with \"quotes\"", 99});
        SaveProjectMeta(dir.string(), m);
        auto m2 = LoadProjectMeta(dir.string());
        bool ok = m2.created == m.created
               && m2.lastOpened == m.lastOpened
               && m2.source == m.source
               && m2.timings.size() == 1
               && m2.timings[0].topic           == "topic with \"quotes\""
               && m2.timings[0].durationSeconds == 99;
        if (!ok) {
            std::cerr << "FAIL [meta-roundtrip]\n";
            ++failures;
        } else {
            std::cout << "PASS [meta-roundtrip]\n";
        }
        fs::remove_all(dir);
    }

    // RecordProjectSource updates source without losing existing metadata.
    {
        auto dir = fs::temp_directory_path() / "st_meta_source";
        fs::create_directories(dir);
        EnsureProjectMeta(dir.string(), "Claude -p");
        RecordProjectSource(dir.string(), "Codex CLI");
        auto meta = LoadProjectMeta(dir.string());
        bool ok = meta.created.size() == 19 && meta.source == "Codex CLI";
        if (!ok) {
            std::cerr << "FAIL [meta-source]: source='" << meta.source << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [meta-source]\n";
        }
        fs::remove_all(dir);
    }

    return failures;
}
