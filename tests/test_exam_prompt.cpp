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
        // instructions field injected when present
        ExamConfig cfgWithInstr = cfg;
        cfgWithInstr.instructions = "Focus specifically on cross-account access patterns.";
        std::string p2 = BuildFirstQuestionPrompt(cfgWithInstr);
        bool instrOk = p2.find("cross-account access patterns") != std::string::npos;
        if (!ok || !instrOk) {
            std::cerr << "FAIL [first-question-prompt]: ok=" << ok << " instrOk=" << instrOk << "\n";
            ++failures;
        } else {
            std::cout << "PASS [first-question-prompt]\n";
        }
    }

    // PickFocusArea: empty list returns ""
    {
        std::vector<FocusArea> empty;
        bool ok = PickFocusArea(empty).empty();
        if (!ok) {
            std::cerr << "FAIL [pick-focus-area-empty]\n";
            ++failures;
        } else {
            std::cout << "PASS [pick-focus-area-empty]\n";
        }
    }

    // PickFocusArea: single item always returns that item
    {
        std::vector<FocusArea> single = {{"Presigned URLs", 3}};
        bool ok = PickFocusArea(single) == "Presigned URLs";
        if (!ok) {
            std::cerr << "FAIL [pick-focus-area-single]\n";
            ++failures;
        } else {
            std::cout << "PASS [pick-focus-area-single]\n";
        }
    }

    // PickFocusArea: multiple items — result is always one of the items
    {
        std::vector<FocusArea> areas = {
            {"Presigned URLs", 5},
            {"Lifecycle rules", 2},
            {"Versioning", 1}
        };
        bool ok = true;
        for (int i = 0; i < 30; ++i) {
            std::string pick = PickFocusArea(areas);
            bool found = (pick == "Presigned URLs" ||
                          pick == "Lifecycle rules" ||
                          pick == "Versioning");
            if (!found) { ok = false; break; }
        }
        if (!ok) {
            std::cerr << "FAIL [pick-focus-area-multiple]: unexpected result\n";
            ++failures;
        } else {
            std::cout << "PASS [pick-focus-area-multiple]\n";
        }
    }

    // PickFocusArea: 5-star item is more likely than 1-star (statistical)
    {
        std::vector<FocusArea> areas = {{"High", 5}, {"Low", 1}};
        int highCount = 0;
        for (int i = 0; i < 100; ++i)
            if (PickFocusArea(areas) == "High") ++highCount;
        // With 5:1 odds, high should win at least 50% of 100 trials
        bool ok = highCount >= 50;
        if (!ok) {
            std::cerr << "FAIL [pick-focus-area-weighted]: highCount=" << highCount << "\n";
            ++failures;
        } else {
            std::cout << "PASS [pick-focus-area-weighted]\n";
        }
    }

    // BuildFirstQuestionPrompt injects focusAreas when set
    {
        ExamConfig cfgFocus = cfg;
        cfgFocus.focusAreas = "Presigned URLs and lifecycle rules";
        std::string p = BuildFirstQuestionPrompt(cfgFocus);
        bool ok = p.find("Presigned URLs and lifecycle rules") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [first-question-focus-areas]\n";
            ++failures;
        } else {
            std::cout << "PASS [first-question-focus-areas]\n";
        }
    }

    // BuildFirstQuestionPrompt omits focus-areas section when empty
    {
        ExamConfig cfgNoFocus = cfg;
        cfgNoFocus.focusAreas = "";
        std::string p = BuildFirstQuestionPrompt(cfgNoFocus);
        // "Focus areas:" header must not appear when field is empty
        bool ok = p.find("Focus areas:") == std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [first-question-no-focus-areas]\n";
            ++failures;
        } else {
            std::cout << "PASS [first-question-no-focus-areas]\n";
        }
    }

    // BuildScoringAndNextPrompt injects focusAreas
    {
        ExamConfig cfgFocus = cfg;
        cfgFocus.focusAreas = "Presigned URLs and lifecycle rules";
        std::string p = BuildScoringAndNextPrompt(cfgFocus, {}, "What is S3?", "Object storage.", 2);
        bool ok = p.find("Presigned URLs and lifecycle rules") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [scoring-focus-areas]\n";
            ++failures;
        } else {
            std::cout << "PASS [scoring-focus-areas]\n";
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

    // BuildScoringAndNextPrompt with empty answer must NOT show "User's answer" at all.
    // Showing any user-answer context (even "(none)") gives small models something to
    // narrate about. The prompt must only ask for an explanation of the correct answer.
    {
        std::string p = BuildScoringAndNextPrompt(cfg, {}, "What is a VPC?", "", 2);
        bool noUserAnswer = p.find("User's answer") == std::string::npos;
        bool hasExplain   = p.find("EXPLANATION:") != std::string::npos;
        if (!noUserAnswer || !hasExplain) {
            std::cerr << "FAIL [scoring-skipped-no-user-context]:"
                      << " noUserAnswer=" << noUserAnswer
                      << " hasExplain=" << hasExplain << "\n";
            ++failures;
        } else {
            std::cout << "PASS [scoring-skipped-no-user-context]\n";
        }
    }

    // BuildScoringAndNextPrompt must NOT offer "skipped" as a model score option.
    // "skipped" is set by the app, not the model; offering it lets small models
    // pick it for real answers.
    {
        std::string p = BuildScoringAndNextPrompt(cfg, {}, "What is a VPC?", "It's a network.", 2);
        bool noSkippedOption = p.find("SCORE: skipped") == std::string::npos;
        if (!noSkippedOption) {
            std::cerr << "FAIL [scoring-no-skipped-option]: prompt offers 'SCORE: skipped'\n";
            ++failures;
        } else {
            std::cout << "PASS [scoring-no-skipped-option]\n";
        }
    }

    // BuildScoringAndNextPrompt: for non-empty answers, EXPLANATION instruction must
    // tell the model to state the correct answer and not narrate the user's response.
    {
        std::string p = BuildScoringAndNextPrompt(cfg, {}, "What is a VPC?", "A network.", 2);
        bool hasAnswer   = p.find("correct answer") != std::string::npos;
        bool noNarrate   = p.find("Do not mention the user") != std::string::npos
                        || p.find("do not mention the user") != std::string::npos;
        if (!hasAnswer || !noNarrate) {
            std::cerr << "FAIL [scoring-explanation-reveals-answer]:"
                      << " hasAnswer=" << hasAnswer
                      << " noNarrate=" << noNarrate << "\n";
            ++failures;
        } else {
            std::cout << "PASS [scoring-explanation-reveals-answer]\n";
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

    // ParseScoredResponse: multi-line EXPLANATION is concatenated (small models wrap text)
    {
        std::string raw =
            "SCORE: correct\n"
            "EXPLANATION: A VPC is a Virtual Private Cloud.\n"
            "It provides isolated network infrastructure in AWS.\n"
            "NEXT_QUESTION: What is a subnet?\n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk
               && resp.score == Score::Correct
               && resp.explanation.find("Virtual Private Cloud") != std::string::npos
               && resp.explanation.find("isolated network") != std::string::npos
               && resp.nextQuestion == "What is a subnet?";
        if (!ok) {
            std::cerr << "FAIL [parse-scored-multiline]: parseOk=" << resp.parseOk
                      << " explanation='" << resp.explanation << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-scored-multiline]\n";
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

    // RenderExamTurns: each turn contains a flag link with its index
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question = "What is RAII?"; t0.userAnswer = "Resource management.";
        t0.score = Score::Correct; t0.flagged = false;
        QuestionTurn t1;
        t1.question = "What is a vtable?"; t1.userAnswer = "Virtual dispatch table.";
        t1.score = Score::Missed; t1.flagged = true;
        turns.push_back(t0);
        turns.push_back(t1);

        std::string html = RenderExamTurns(turns, {0, 0});
        bool hasFlag0    = html.find("testtaker://flag/0") != std::string::npos;
        bool hasFlag1    = html.find("testtaker://flag/1") != std::string::npos;
        bool flagged1    = html.find("flagged") != std::string::npos;
        bool hasHover    = html.find(":hover") != std::string::npos
                        || html.find("hover") != std::string::npos;

        if (!hasFlag0 || !hasFlag1 || !flagged1 || !hasHover) {
            std::cerr << "FAIL [render-exam-turns-flag-links]:"
                      << " flag0=" << hasFlag0 << " flag1=" << hasFlag1
                      << " flagged1=" << flagged1 << " hover=" << hasHover << "\n";
            ++failures;
        } else {
            std::cout << "PASS [render-exam-turns-flag-links]\n";
        }
    }

    // RenderExamTurns: each turn has a note link; note text shown when present
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question = "What is RAII?"; t0.userAnswer = "Resource management.";
        t0.score = Score::Correct; t0.flagged = false; t0.note = "";
        QuestionTurn t1;
        t1.question = "What is a vtable?"; t1.userAnswer = "Virtual dispatch table.";
        t1.score = Score::Missed; t1.flagged = false;
        t1.note = "Remember: vtable is per class, not per object.";
        turns.push_back(t0);
        turns.push_back(t1);

        std::string html = RenderExamTurns(turns, {0, 0});
        bool hasNote0    = html.find("testtaker://note/0") != std::string::npos;
        bool hasNote1    = html.find("testtaker://note/1") != std::string::npos;
        bool noteText    = html.find("vtable is per class") != std::string::npos;

        if (!hasNote0 || !hasNote1 || !noteText) {
            std::cerr << "FAIL [render-exam-turns-note-links]:"
                      << " note0=" << hasNote0 << " note1=" << hasNote1
                      << " noteText=" << noteText << "\n";
            ++failures;
        } else {
            std::cout << "PASS [render-exam-turns-note-links]\n";
        }
    }

    // RenderExamTurns: discuss button shows chat count badge when > 0
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question = "What is RAII?"; t0.userAnswer = "Resource management.";
        t0.score = Score::Correct; t0.flagged = false;
        QuestionTurn t1;
        t1.question = "What is a vtable?"; t1.userAnswer = "Virtual dispatch table.";
        t1.score = Score::Missed; t1.flagged = false;
        turns.push_back(t0);
        turns.push_back(t1);

        // t0 has 0 chats, t1 has 3
        std::string html = RenderExamTurns(turns, {0, 3});

        // t1's discuss button should show the count and have an active style
        bool hasCount  = html.find("3") != std::string::npos;
        bool hasActive = html.find("has-chat") != std::string::npos;
        // t0 should NOT show a count (0 chats)
        bool t0NoCount = html.find("discuss/0\">&#x1F4AC; discuss</a>") != std::string::npos
                      || html.find("discuss/0") != std::string::npos;

        if (!hasCount || !hasActive) {
            std::cerr << "FAIL [render-exam-turns-chat-count]:"
                      << " hasCount=" << hasCount << " hasActive=" << hasActive << "\n";
            ++failures;
        } else {
            std::cout << "PASS [render-exam-turns-chat-count]\n";
        }
    }

    // RenderExamTurns: user answer with code fence is rendered as <pre>/<code>
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question   = "Show a pure virtual function.";
        t0.userAnswer = "Like this:\n```cpp\nclass Monster { virtual void roar() = 0; };\n```";
        t0.score      = Score::Partial;
        t0.flagged    = false;
        turns.push_back(t0);

        std::string html = RenderExamTurns(turns, {0});
        bool hasCode = html.find("<code") != std::string::npos
                    || html.find("<pre")  != std::string::npos;
        if (!hasCode) {
            std::cerr << "FAIL [render-answer-code-fence]: code fence not rendered\n";
            ++failures;
        } else {
            std::cout << "PASS [render-answer-code-fence]\n";
        }
    }

    // RenderExamTurns: newlines in user answer produce separate paragraphs/breaks
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question   = "Q";
        t0.userAnswer = "AlphaLine\n\nBetaLine";
        t0.score      = Score::Correct;
        t0.flagged    = false;
        turns.push_back(t0);

        std::string html = RenderExamTurns(turns, {0});
        // Both lines must appear and be separated by markup (not run together)
        auto posAlpha = html.find("AlphaLine");
        auto posBeta  = html.find("BetaLine");
        bool separated = posAlpha != std::string::npos
                      && posBeta  != std::string::npos
                      && html.substr(posAlpha, posBeta - posAlpha).find('<') != std::string::npos;
        if (!separated) {
            std::cerr << "FAIL [render-answer-newlines]: lines not separated by markup\n";
            ++failures;
        } else {
            std::cout << "PASS [render-answer-newlines]\n";
        }
    }

    return failures;
}
