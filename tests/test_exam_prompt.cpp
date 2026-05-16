#include "exam_prompt.h"
#include "session.h"
#include <iostream>

int test_exam_prompt() {
    int failures = 0;

    ExamConfig cfg;
    cfg.topic          = "AWS S3 and IAM";
    cfg.difficulty     = "medium";
    cfg.projectContext = "Focus on bucket policies and IAM roles.";
    cfg.totalQuestions = 5;

    // BuildFirstQuestionPrompt contains topic, difficulty, question count
    {
        std::string p = BuildFirstQuestionPrompt(cfg);
        bool ok = p.find("AWS S3 and IAM")                   != std::string::npos
               && p.find("medium")                           != std::string::npos
               && p.find("5")                                != std::string::npos
               && p.find("bucket policies and IAM roles")    != std::string::npos
               && p.find("question 1")                       != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [first-question-prompt]: missing expected content\n";
            ++failures;
        } else {
            std::cout << "PASS [first-question-prompt]\n";
        }
    }

    // BuildFirstQuestionPrompt without project context omits context section
    {
        ExamConfig cfgNoCtx = cfg;
        cfgNoCtx.projectContext = "";
        std::string p = BuildFirstQuestionPrompt(cfgNoCtx);
        bool ok = p.find("AWS S3 and IAM") != std::string::npos
               && p.find("bucket policies") == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [first-question-no-context]\n";
            ++failures;
        } else {
            std::cout << "PASS [first-question-no-context]\n";
        }
    }

    // BuildScoringAndNextPrompt contains question, answer, history, SCORE/EXPLANATION/NEXT_QUESTION labels
    {
        QuestionTurn past;
        past.question    = "What is S3?";
        past.userAnswer  = "Object storage.";
        past.score       = Score::Correct;
        past.explanation = "Correct.";
        past.flagged     = false;

        std::vector<QuestionTurn> history = {past};
        std::string p = BuildScoringAndNextPrompt(cfg, history,
                                                  "What is an IAM role?",
                                                  "A set of permissions.",
                                                  3);
        bool ok = p.find("What is an IAM role?")  != std::string::npos
               && p.find("A set of permissions.")  != std::string::npos
               && p.find("What is S3?")            != std::string::npos
               && p.find("SCORE:")                 != std::string::npos
               && p.find("EXPLANATION:")           != std::string::npos
               && p.find("NEXT_QUESTION:")         != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [scoring-next-prompt]: missing expected content\n";
            ++failures;
        } else {
            std::cout << "PASS [scoring-next-prompt]\n";
        }
    }

    // BuildScoringAndNextPrompt with empty answer uses skipped indicator
    {
        std::string p = BuildScoringAndNextPrompt(cfg, {}, "What is a VPC?", "", 2);
        bool ok = p.find("skipped") != std::string::npos
               || p.find("did not know") != std::string::npos
               || p.find("no answer") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [scoring-skipped-indicator]\n";
            ++failures;
        } else {
            std::cout << "PASS [scoring-skipped-indicator]\n";
        }
    }

    // BuildScoringAndNextPrompt with questionsRemaining==0 indicates last question
    {
        std::string p = BuildScoringAndNextPrompt(cfg, {}, "Last question?", "My answer.", 0);
        // Should instruct LLM to emit empty NEXT_QUESTION or indicate session end
        bool ok = p.find("NEXT_QUESTION:") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [scoring-last-question]: NEXT_QUESTION marker missing\n";
            ++failures;
        } else {
            std::cout << "PASS [scoring-last-question]\n";
        }
    }

    // ParseScoredResponse: parses well-formed LLM output
    {
        std::string raw =
            "SCORE: partial\n"
            "EXPLANATION: You got the main idea but missed the specifics.\n"
            "NEXT_QUESTION: What is an S3 bucket policy?\n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk
               && resp.score       == Score::Partial
               && resp.explanation == "You got the main idea but missed the specifics."
               && resp.nextQuestion == "What is an S3 bucket policy?";
        if (!ok) {
            std::cerr << "FAIL [parse-scored-response]: parseOk=" << resp.parseOk
                      << " score=" << ScoreToString(resp.score)
                      << " explanation='" << resp.explanation << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-scored-response]\n";
        }
    }

    // ParseScoredResponse: empty NEXT_QUESTION means session over
    {
        std::string raw =
            "SCORE: correct\n"
            "EXPLANATION: Exactly right.\n"
            "NEXT_QUESTION: \n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk
               && resp.score        == Score::Correct
               && resp.nextQuestion == "";
        if (!ok) {
            std::cerr << "FAIL [parse-scored-response-end]\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-scored-response-end]\n";
        }
    }

    // ParseScoredResponse: handles missed and skipped
    {
        std::string raw =
            "SCORE: missed\n"
            "EXPLANATION: The correct answer is X.\n"
            "NEXT_QUESTION: Explain Y.\n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk && resp.score == Score::Missed;
        if (!ok) {
            std::cerr << "FAIL [parse-scored-missed]\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-scored-missed]\n";
        }
    }

    // BuildSessionSummaryPrompt contains all turn data and topic
    {
        std::vector<QuestionTurn> history;
        QuestionTurn t1;
        t1.question = "Q1"; t1.userAnswer = "A1"; t1.score = Score::Correct;
        QuestionTurn t2;
        t2.question = "Q2"; t2.userAnswer = ""; t2.score = Score::Missed;
        history.push_back(t1);
        history.push_back(t2);

        std::string p = BuildSessionSummaryPrompt(cfg, history);
        bool ok = p.find("AWS S3 and IAM") != std::string::npos
               && p.find("Q1")             != std::string::npos
               && p.find("Q2")             != std::string::npos
               && p.find("correct")        != std::string::npos
               && p.find("missed")         != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [session-summary-prompt]\n";
            ++failures;
        } else {
            std::cout << "PASS [session-summary-prompt]\n";
        }
    }

    return failures;
}
