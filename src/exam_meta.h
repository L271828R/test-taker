#pragma once
#include <string>
#include <vector>

struct SessionRecord {
    std::string sessionFile;
    std::string startedAt;
    std::string finishedAt;
    std::string topic;
    std::string difficulty;
    int totalQuestions = 0;
    int totalStars     = 0;  // sum of star values (1-5) for all rated turns
    int skipped        = 0;
    int flaggedCount   = 0;
};

struct ExamProjectMeta {
    std::string               created;
    std::string               lastOpened;
    std::string               llmSource;
    std::vector<SessionRecord> sessions;
};

ExamProjectMeta LoadExamMeta(const std::string& projectDir);
void            SaveExamMeta(const std::string& projectDir, const ExamProjectMeta& meta);

// Upsert a session record matched by sessionFile.
void RecordSession(const std::string& projectDir, const SessionRecord& rec);

void RecordExamOpen(const std::string& projectDir);

// Create .testtaker.json if absent, set llmSource if provided.
void EnsureExamMeta(const std::string& projectDir, const std::string& llmSource = "");
