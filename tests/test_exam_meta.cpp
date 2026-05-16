#include "exam_meta.h"
#include "meta.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int test_exam_meta() {
    int failures = 0;

    // EnsureExamMeta creates .testtaker.json with created timestamp
    {
        auto dir = fs::temp_directory_path() / "tt_meta_ensure";
        fs::create_directories(dir);
        EnsureExamMeta(dir.string(), "claude -p");
        auto meta = LoadExamMeta(dir.string());
        bool ok = meta.created.size() == 19
               && meta.created[4]  == '-'
               && meta.created[10] == 'T'
               && meta.llmSource   == "claude -p"
               && meta.sessions.empty();
        if (!ok) {
            std::cerr << "FAIL [exam-meta-ensure]: created='" << meta.created
                      << "' source='" << meta.llmSource << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [exam-meta-ensure]\n";
        }
        fs::remove_all(dir);
    }

    // RecordExamOpen updates lastOpened
    {
        auto dir = fs::temp_directory_path() / "tt_meta_open";
        fs::create_directories(dir);
        EnsureExamMeta(dir.string());
        RecordExamOpen(dir.string());
        auto meta = LoadExamMeta(dir.string());
        bool ok = meta.lastOpened.size() == 19
               && meta.lastOpened[10] == 'T';
        if (!ok) {
            std::cerr << "FAIL [exam-meta-open]: lastOpened='" << meta.lastOpened << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [exam-meta-open]\n";
        }
        fs::remove_all(dir);
    }

    // RecordSession: adds a session entry, round-trips all fields
    {
        auto dir = fs::temp_directory_path() / "tt_meta_record";
        fs::create_directories(dir);
        EnsureExamMeta(dir.string());

        SessionRecord rec;
        rec.sessionFile   = "session_20260516_143022.md";
        rec.startedAt     = "2026-05-16T14:30:22";
        rec.finishedAt    = "2026-05-16T14:47:55";
        rec.topic         = "Python data structures";
        rec.difficulty    = "medium";
        rec.totalQuestions = 10;
        rec.correct        = 6;
        rec.partial        = 2;
        rec.missed         = 1;
        rec.skipped        = 1;
        rec.flaggedCount   = 3;

        RecordSession(dir.string(), rec);
        auto meta = LoadExamMeta(dir.string());

        bool ok = meta.sessions.size() == 1
               && meta.sessions[0].sessionFile    == rec.sessionFile
               && meta.sessions[0].startedAt      == rec.startedAt
               && meta.sessions[0].finishedAt     == rec.finishedAt
               && meta.sessions[0].topic          == rec.topic
               && meta.sessions[0].difficulty     == rec.difficulty
               && meta.sessions[0].totalQuestions == 10
               && meta.sessions[0].correct        == 6
               && meta.sessions[0].partial        == 2
               && meta.sessions[0].missed         == 1
               && meta.sessions[0].skipped        == 1
               && meta.sessions[0].flaggedCount   == 3;
        if (!ok) {
            std::cerr << "FAIL [exam-meta-record-session]: sessions=" << meta.sessions.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [exam-meta-record-session]\n";
        }
        fs::remove_all(dir);
    }

    // RecordSession: upsert — updating an existing session replaces it
    {
        auto dir = fs::temp_directory_path() / "tt_meta_upsert";
        fs::create_directories(dir);
        EnsureExamMeta(dir.string());

        SessionRecord rec;
        rec.sessionFile    = "session_abc.md";
        rec.startedAt      = "2026-05-16T10:00:00";
        rec.totalQuestions = 5;
        rec.correct        = 3;
        RecordSession(dir.string(), rec);

        // Update same session
        rec.correct = 5;
        rec.finishedAt = "2026-05-16T10:15:00";
        RecordSession(dir.string(), rec);

        auto meta = LoadExamMeta(dir.string());
        bool ok = meta.sessions.size() == 1
               && meta.sessions[0].correct    == 5
               && meta.sessions[0].finishedAt == "2026-05-16T10:15:00";
        if (!ok) {
            std::cerr << "FAIL [exam-meta-upsert]: sessions=" << meta.sessions.size()
                      << " correct=" << (meta.sessions.empty() ? -1 : meta.sessions[0].correct) << "\n";
            ++failures;
        } else {
            std::cout << "PASS [exam-meta-upsert]\n";
        }
        fs::remove_all(dir);
    }

    // Multiple sessions accumulate correctly
    {
        auto dir = fs::temp_directory_path() / "tt_meta_multi";
        fs::create_directories(dir);
        EnsureExamMeta(dir.string());

        for (int i = 0; i < 3; ++i) {
            SessionRecord rec;
            rec.sessionFile    = "session_" + std::to_string(i) + ".md";
            rec.startedAt      = "2026-05-16T10:0" + std::to_string(i) + ":00";
            rec.totalQuestions = 10;
            rec.correct        = i * 2;
            RecordSession(dir.string(), rec);
        }

        auto meta = LoadExamMeta(dir.string());
        bool ok = meta.sessions.size() == 3
               && meta.sessions[0].sessionFile == "session_0.md"
               && meta.sessions[2].correct     == 4;
        if (!ok) {
            std::cerr << "FAIL [exam-meta-multi]: sessions=" << meta.sessions.size() << "\n";
            ++failures;
        } else {
            std::cout << "PASS [exam-meta-multi]\n";
        }
        fs::remove_all(dir);
    }

    return failures;
}
