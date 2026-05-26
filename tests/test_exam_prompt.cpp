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
        past.score       = Score::Star5;
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
               && p.find("SCORE: 1")               != std::string::npos  // 5-star scale instruction
               && p.find("SCORE: 5")               != std::string::npos
               && p.find("SCORE: correct")         == std::string::npos  // old score labels gone
               && p.find("SCORE: partial")         == std::string::npos
               && p.find("SCORE: missed")          == std::string::npos
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

    // ParseScoredResponse: parses well-formed LLM output with 5-star score
    {
        std::string raw =
            "SCORE: 3\n"
            "EXPLANATION: You got the main idea but missed the specifics.\n"
            "NEXT_QUESTION: What is an S3 bucket policy?\n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk
               && resp.score       == Score::Star3
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
            "SCORE: 5\n"
            "EXPLANATION: Exactly right.\n"
            "NEXT_QUESTION: \n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk
               && resp.score        == Score::Star5
               && resp.nextQuestion == "";
        if (!ok) {
            std::cerr << "FAIL [parse-scored-response-end]\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-scored-response-end]\n";
        }
    }

    // ParseScoredResponse: score 1 (fully wrong)
    {
        std::string raw =
            "SCORE: 1\n"
            "EXPLANATION: The correct answer is X.\n"
            "NEXT_QUESTION: Explain Y.\n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk && resp.score == Score::Star1;
        if (!ok) {
            std::cerr << "FAIL [parse-scored-star1]\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-scored-star1]\n";
        }
    }

    // ParseScoredResponse: old-format backward compat (correct/partial/missed)
    {
        std::string raw =
            "SCORE: correct\n"
            "EXPLANATION: Right.\n"
            "NEXT_QUESTION: Next?\n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk && resp.score == Score::Star5;
        if (!ok) {
            std::cerr << "FAIL [parse-scored-backcompat]\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-scored-backcompat]\n";
        }
    }

    // ParseScoredResponse: multi-line EXPLANATION is concatenated (small models wrap text)
    {
        std::string raw =
            "SCORE: 4\n"
            "EXPLANATION: A VPC is a Virtual Private Cloud.\n"
            "It provides isolated network infrastructure in AWS.\n"
            "NEXT_QUESTION: What is a subnet?\n";
        auto resp = ParseScoredResponse(raw);
        bool ok = resp.parseOk
               && resp.score == Score::Star4
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
        t1.question = "Q1"; t1.userAnswer = "A1"; t1.score = Score::Star5;
        QuestionTurn t2;
        t2.question = "Q2"; t2.userAnswer = ""; t2.score = Score::Star1;
        history.push_back(t1);
        history.push_back(t2);

        std::string p = BuildSessionSummaryPrompt(cfg, history);
        bool ok = p.find("AWS S3 and IAM") != std::string::npos
               && p.find("Q1")             != std::string::npos
               && p.find("Q2")             != std::string::npos
               && p.find("SCORE: 5")       != std::string::npos
               && p.find("SCORE: 1")       != std::string::npos;
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
        t0.score = Score::Star5; t0.flagged = false;
        QuestionTurn t1;
        t1.question = "What is a vtable?"; t1.userAnswer = "Virtual dispatch table.";
        t1.score = Score::Star1; t1.flagged = true;
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
        t0.score = Score::Star5; t0.flagged = false; t0.note = "";
        QuestionTurn t1;
        t1.question = "What is a vtable?"; t1.userAnswer = "Virtual dispatch table.";
        t1.score = Score::Star1; t1.flagged = false;
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
        t0.score = Score::Star5; t0.flagged = false;
        QuestionTurn t1;
        t1.question = "What is a vtable?"; t1.userAnswer = "Virtual dispatch table.";
        t1.score = Score::Star1; t1.flagged = false;
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
        t0.score      = Score::Star3;
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

    // RenderExamTurns: thumb buttons appear on every turn
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question = "What is RAII?"; t0.userAnswer = "x"; t0.score = Score::Star3;
        turns.push_back(t0);
        std::string html = RenderExamTurns(turns, {0});
        bool hasMore = html.find("testtaker://more/0") != std::string::npos;
        bool hasLess = html.find("testtaker://less/0") != std::string::npos;
        if (!hasMore || !hasLess) {
            std::cerr << "FAIL [render-thumb-buttons]: more=" << hasMore << " less=" << hasLess << "\n";
            ++failures;
        } else {
            std::cout << "PASS [render-thumb-buttons]\n";
        }
    }

    // RenderExamTurns: voted class set when question snippet is in moreOfTopics
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question = "What is RAII?"; t0.userAnswer = "x"; t0.score = Score::Star3;
        turns.push_back(t0);
        std::vector<std::string> moreOf = {"What is RAII?"};
        std::string html = RenderExamTurns(turns, {0}, moreOf, {});
        bool moreVoted = html.find("more-btn voted") != std::string::npos
                      || html.find("more-btn\" voted") != std::string::npos
                      || (html.find("testtaker://more/0") != std::string::npos
                          && html.find("voted") != std::string::npos);
        if (!moreVoted) {
            std::cerr << "FAIL [render-more-voted]: voted class not set\n";
            ++failures;
        } else {
            std::cout << "PASS [render-more-voted]\n";
        }
    }

    // RenderExamTurns: voted class set when question snippet is in lessOfTopics
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question = "What is a vtable?"; t0.userAnswer = "x"; t0.score = Score::Star2;
        turns.push_back(t0);
        std::vector<std::string> lessOf = {"What is a vtable?"};
        std::string html = RenderExamTurns(turns, {0}, {}, lessOf);
        bool lessVoted = html.find("less-btn voted") != std::string::npos
                      || html.find("less-btn\" voted") != std::string::npos
                      || (html.find("testtaker://less/0") != std::string::npos
                          && html.find("voted") != std::string::npos);
        if (!lessVoted) {
            std::cerr << "FAIL [render-less-voted]: voted class not set\n";
            ++failures;
        } else {
            std::cout << "PASS [render-less-voted]\n";
        }
    }

    // RenderExamTurns: newlines in user answer produce separate paragraphs/breaks
    {
        std::vector<QuestionTurn> turns;
        QuestionTurn t0;
        t0.question   = "Q";
        t0.userAnswer = "AlphaLine\n\nBetaLine";
        t0.score      = Score::Star5;
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

    // RenderHistoryGroups: contains session label, question text, and clear link
    {
        QuestionTurn t;
        t.question    = "What is RAII?";
        t.userAnswer  = "Resource Acquisition Is Initialisation.";
        t.score       = Score::Star5;
        t.explanation = "Correct. RAII ties resource lifetime to object scope.";

        std::vector<HistoryGroup> groups;
        groups.push_back({"Session — 2026-05-18", "/path/session.md", {t}});

        std::string html = RenderHistoryGroups(groups);
        bool ok = html.find("2026-05-18") != std::string::npos;
        bool hasQ    = html.find("What is RAII?") != std::string::npos;
        bool hasClear = html.find("testtaker://clear-history") != std::string::npos;
        if (!ok || !hasQ || !hasClear) {
            std::cerr << "FAIL [render-history-groups]: ok=" << ok
                      << " hasQ=" << hasQ << " hasClear=" << hasClear << "\n";
            ++failures;
        } else {
            std::cout << "PASS [render-history-groups]\n";
        }
    }

    // RenderHistoryGroups: interactive toolbar buttons with correct group/turn URLs
    {
        QuestionTurn t0;
        t0.question    = "What is RAII?";
        t0.userAnswer  = "Resource lifetime.";
        t0.score       = Score::Star5;
        t0.explanation = "RAII ties resource lifetime to object scope.";
        t0.flagged     = false;
        t0.saved       = false;

        QuestionTurn t1;
        t1.question    = "What is a vtable?";
        t1.userAnswer  = "Virtual dispatch table.";
        t1.score       = Score::Star1;
        t1.explanation = "A vtable is a per-class array of function pointers.";
        t1.flagged     = true;   // already flagged
        t1.saved       = true;   // already saved
        t1.note        = "Review before interview";

        std::vector<HistoryGroup> groups;
        groups.push_back({"Session A", "/path/a.md", {t0, t1}});

        std::string html = RenderHistoryGroups(groups);

        // Discuss button for group 0, turns 0 and 1
        bool hasDiscuss0 = html.find("testtaker://hdiscuss/0/0") != std::string::npos;
        bool hasDiscuss1 = html.find("testtaker://hdiscuss/0/1") != std::string::npos;
        // Flag buttons with correct URLs
        bool hasFlag0    = html.find("testtaker://hflag/0/0")    != std::string::npos;
        bool hasFlag1    = html.find("testtaker://hflag/0/1")    != std::string::npos;
        // Save buttons
        bool hasSave0    = html.find("testtaker://hsave/0/0")    != std::string::npos;
        bool hasSave1    = html.find("testtaker://hsave/0/1")    != std::string::npos;
        // Note buttons
        bool hasNote0    = html.find("testtaker://hnote/0/0")    != std::string::npos;
        bool hasNote1    = html.find("testtaker://hnote/0/1")    != std::string::npos;
        // Flagged state on t1 persists as style
        bool flaggedClass = html.find("flagged")                 != std::string::npos;
        // Saved state on t1 persists
        bool savedClass   = html.find("saved")                   != std::string::npos;
        // Note text for t1 is shown
        bool noteText     = html.find("Review before interview") != std::string::npos;

        bool ok = hasDiscuss0 && hasDiscuss1 && hasFlag0 && hasFlag1
               && hasSave0 && hasSave1 && hasNote0 && hasNote1
               && flaggedClass && savedClass && noteText;
        if (!ok) {
            std::cerr << "FAIL [render-history-groups-interactive]:"
                      << " discuss0=" << hasDiscuss0 << " discuss1=" << hasDiscuss1
                      << " flag0=" << hasFlag0 << " flag1=" << hasFlag1
                      << " save0=" << hasSave0 << " save1=" << hasSave1
                      << " note0=" << hasNote0 << " note1=" << hasNote1
                      << " flaggedClass=" << flaggedClass
                      << " savedClass=" << savedClass
                      << " noteText=" << noteText << "\n";
            ++failures;
        } else {
            std::cout << "PASS [render-history-groups-interactive]\n";
        }
    }

    // RenderHistoryGroups: game button appears for turns that have an explanation
    {
        QuestionTurn withExpl;
        withExpl.question    = "What is a move constructor?";
        withExpl.userAnswer  = "Transfers ownership.";
        withExpl.score       = Score::Star4;
        withExpl.explanation = "A move constructor transfers resources from an rvalue.";

        QuestionTurn noExpl;
        noExpl.question   = "What is RAII?";
        noExpl.score      = Score::Skipped;
        noExpl.silentSkip = true;

        std::vector<HistoryGroup> groups;
        groups.push_back({"Old session", "/path/old.md", {withExpl, noExpl}});

        std::string html = RenderHistoryGroups(groups);

        bool hasGame0  = html.find("testtaker://hgame/0/0") != std::string::npos;
        bool noGame1   = html.find("testtaker://hgame/0/1") == std::string::npos;

        if (!hasGame0 || !noGame1) {
            std::cerr << "FAIL [render-history-groups-game]:"
                      << " hasGame0=" << hasGame0 << " noGame1=" << noGame1 << "\n";
            ++failures;
        } else {
            std::cout << "PASS [render-history-groups-game]\n";
        }
    }

    // ParseScoredResponse: :::tidbit block in EXPLANATION is preserved with
    // correct newlines so RenderMarkdown can render it as a <details> widget.
    // The tidbit opener must be at the START of a line (\n:::tidbit), not
    // space-joined into the preceding prose, otherwise RenderMarkdown can't
    // detect the block boundary.
    {
        std::string raw =
            "SCORE: correct\n"
            "EXPLANATION: A VPC is a Virtual Private Cloud.\n"
            "\n"
            ":::tidbit[Albert Einstein]\n"
            "Think of isolation as curvature in network space-time.\n"
            ":::\n"
            "NEXT_QUESTION: What is a subnet?\n";
        auto resp = ParseScoredResponse(raw);
        bool hasExpl   = resp.explanation.find("Virtual Private Cloud") != std::string::npos;
        // Tidbit opener must be on its own line (preceded by \n), not space-joined.
        bool hasTidbit = resp.explanation.find("\n:::tidbit[Albert Einstein]") != std::string::npos;
        bool hasNext   = resp.nextQuestion == "What is a subnet?";
        if (!resp.parseOk || !hasExpl || !hasTidbit || !hasNext) {
            std::cerr << "FAIL [parse-scored-tidbit]:"
                      << " parseOk=" << resp.parseOk
                      << " expl=" << hasExpl
                      << " tidbit-on-own-line=" << hasTidbit
                      << " next=" << hasNext << "\n"
                      << "  explanation: '" << resp.explanation << "'\n";
            ++failures;
        } else {
            std::cout << "PASS [parse-scored-tidbit]\n";
        }
    }

    // BuildScoringAndNextPrompt: mermaid hint present for large models, absent for small.
    {
        ExamConfig cfgLarge = cfg;
        cfgLarge.largeModel = true;
        std::string pLarge = BuildScoringAndNextPrompt(cfgLarge, {}, "What is a VPC?", "A network.", 2);

        ExamConfig cfgSmall = cfg;
        cfgSmall.largeModel = false;
        std::string pSmall = BuildScoringAndNextPrompt(cfgSmall, {}, "What is a VPC?", "A network.", 2);

        bool largeHas  = pLarge.find("mermaid") != std::string::npos;
        bool smallLacks = pSmall.find("mermaid") == std::string::npos;
        if (!largeHas || !smallLacks) {
            std::cerr << "FAIL [scoring-mermaid-hint]:"
                      << " largeHas=" << largeHas << " smallLacks=" << smallLacks << "\n";
            ++failures;
        } else {
            std::cout << "PASS [scoring-mermaid-hint]\n";
        }
    }

    // BuildScoringAndNextPrompt with personalities: the prompt must name exactly
    // ONE personality in the :::tidbit instruction — not a "pick one of" list.
    // Giving the LLM a list causes it to always pick the first (alphabetically),
    // so we select the character at prompt-build time.
    {
        ExamConfig cfgPersonality = cfg;
        cfgPersonality.personalities = {"Albert Einstein", "Bill Gates"};
        std::string p = BuildScoringAndNextPrompt(cfgPersonality, {},
                                                  "What is a VPC?", "A network.", 2);
        bool hasTidbit    = p.find(":::tidbit") != std::string::npos;
        bool hasEinstein  = p.find("Albert Einstein") != std::string::npos;
        bool hasGates     = p.find("Bill Gates")      != std::string::npos;
        bool hasPickOneOf = p.find("pick one of")     != std::string::npos;
        // Exactly one personality must appear; the "pick one of" list must be gone.
        bool exactlyOne   = (hasEinstein != hasGates);  // XOR: one but not both
        if (!hasTidbit || !exactlyOne || hasPickOneOf) {
            std::cerr << "FAIL [personality-in-prompt]:"
                      << " tidbit=" << hasTidbit
                      << " einstein=" << hasEinstein
                      << " gates=" << hasGates
                      << " pickOneOf=" << hasPickOneOf << "\n";
            ++failures;
        } else {
            std::cout << "PASS [personality-in-prompt]\n";
        }
    }

    // With a single personality the name must appear in the instruction.
    {
        ExamConfig cfgOne = cfg;
        cfgOne.personalities = {"Richard Feynman"};
        std::string p = BuildScoringAndNextPrompt(cfgOne, {},
                                                  "What is spin?", "A quantum number.", 2);
        bool hasFeynman   = p.find("Richard Feynman") != std::string::npos;
        bool hasPickOneOf = p.find("pick one of")     != std::string::npos;
        if (!hasFeynman || hasPickOneOf) {
            std::cerr << "FAIL [personality-single]: feynman=" << hasFeynman
                      << " pickOneOf=" << hasPickOneOf << "\n";
            ++failures;
        } else {
            std::cout << "PASS [personality-single]\n";
        }
    }

    // moreOfTopics injected into BuildFirstQuestionPrompt
    {
        ExamConfig c = cfg;
        c.moreOfTopics = {"bucket versioning", "presigned URLs"};
        std::string p = BuildFirstQuestionPrompt(c);
        bool ok = p.find("bucket versioning") != std::string::npos
               && p.find("presigned URLs")    != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [more-of-in-first-prompt]\n";
            ++failures;
        } else {
            std::cout << "PASS [more-of-in-first-prompt]\n";
        }
    }

    // lessOfTopics injected into BuildFirstQuestionPrompt
    {
        ExamConfig c = cfg;
        c.lessOfTopics = {"basic bucket creation"};
        std::string p = BuildFirstQuestionPrompt(c);
        bool ok = p.find("basic bucket creation") != std::string::npos;
        if (!ok) {
            std::cerr << "FAIL [less-of-in-first-prompt]\n";
            ++failures;
        } else {
            std::cout << "PASS [less-of-in-first-prompt]\n";
        }
    }

    // moreOfTopics + lessOfTopics injected into BuildScoringAndNextPrompt
    {
        ExamConfig c = cfg;
        c.moreOfTopics = {"cross-account roles"};
        c.lessOfTopics = {"ARN syntax"};
        std::vector<QuestionTurn> history;
        std::string p = BuildScoringAndNextPrompt(c, history, "What is IAM?", "A service.", 3);
        bool moreOk = p.find("cross-account roles") != std::string::npos;
        bool lessOk = p.find("ARN syntax")          != std::string::npos;
        if (!moreOk || !lessOk) {
            std::cerr << "FAIL [topic-weights-in-scoring-prompt]: more=" << moreOk
                      << " less=" << lessOk << "\n";
            ++failures;
        } else {
            std::cout << "PASS [topic-weights-in-scoring-prompt]\n";
        }
    }

    // empty moreOfTopics/lessOfTopics produce no extra injection lines
    {
        ExamConfig c = cfg;
        std::string p = BuildFirstQuestionPrompt(c);
        bool noMore = p.find("Explore more") == std::string::npos;
        bool noLess = p.find("Reduce questions") == std::string::npos;
        if (!noMore || !noLess) {
            std::cerr << "FAIL [no-topic-weights-no-injection]\n";
            ++failures;
        } else {
            std::cout << "PASS [no-topic-weights-no-injection]\n";
        }
    }

    // BuildScoringAndNextPrompt omits silently-skipped turns from history
    {
        ExamConfig c = cfg;
        std::vector<QuestionTurn> history;
        QuestionTurn t0;
        t0.question = "What is an S3 bucket?"; t0.userAnswer = "A storage unit.";
        t0.score = Score::Star3; t0.silentSkip = false;
        QuestionTurn t1;
        t1.question = "What is a presigned URL?"; t1.userAnswer = "";
        t1.score = Score::Skipped; t1.silentSkip = true;
        history.push_back(t0);
        history.push_back(t1);
        std::string p = BuildScoringAndNextPrompt(c, history, "What is IAM?", "A service.", 2);
        bool hasS3    = p.find("S3 bucket") != std::string::npos;
        bool noSilent = p.find("presigned URL") == std::string::npos;
        if (!hasS3 || !noSilent) {
            std::cerr << "FAIL [silent-skip-omitted-from-history]: hasS3=" << hasS3
                      << " noSilent=" << noSilent << "\n";
            ++failures;
        } else {
            std::cout << "PASS [silent-skip-omitted-from-history]\n";
        }
    }

    // tidbitCount=2 with exactly 2 personalities → both appear in scoring prompt
    {
        ExamConfig c = cfg;
        c.personalities = {"Alice", "Bob"};
        c.tidbitCount   = 2;
        std::vector<QuestionTurn> history;
        std::string p = BuildScoringAndNextPrompt(c, history, "What is IAM?", "A service.", 3);
        bool hasAlice = p.find(":::tidbit[Alice]") != std::string::npos;
        bool hasBob   = p.find(":::tidbit[Bob]")   != std::string::npos;
        if (!hasAlice || !hasBob) {
            std::cerr << "FAIL [tidbit-count-2]: alice=" << hasAlice << " bob=" << hasBob << "\n";
            ++failures;
        } else {
            std::cout << "PASS [tidbit-count-2]\n";
        }
    }

    // tidbitCount=1 (default) → only one :::tidbit block in scoring prompt
    {
        ExamConfig c = cfg;
        c.personalities = {"Alice", "Bob", "Carol"};
        c.tidbitCount   = 1;
        std::vector<QuestionTurn> history;
        std::string p = BuildScoringAndNextPrompt(c, history, "What is IAM?", "A service.", 3);
        size_t count = 0;
        size_t pos = 0;
        while ((pos = p.find(":::tidbit[", pos)) != std::string::npos) {
            ++count;
            pos += 10;
        }
        // format example repeats each block twice (instruction + example), so count >= 2 for n=1
        bool ok = (count >= 2); // at least instruction + example for the one tidbit
        if (!ok) {
            std::cerr << "FAIL [tidbit-count-1-single]: count=" << count << "\n";
            ++failures;
        } else {
            std::cout << "PASS [tidbit-count-1-single]\n";
        }
    }

    // tidbitCount=3 with 3 personalities → all three appear in scoring prompt
    {
        ExamConfig c = cfg;
        c.personalities = {"Alice", "Bob", "Carol"};
        c.tidbitCount   = 3;
        std::vector<QuestionTurn> history;
        std::string p = BuildScoringAndNextPrompt(c, history, "What is IAM?", "A service.", 3);
        bool hasAlice = p.find(":::tidbit[Alice]") != std::string::npos;
        bool hasBob   = p.find(":::tidbit[Bob]")   != std::string::npos;
        bool hasCarol = p.find(":::tidbit[Carol]")  != std::string::npos;
        if (!hasAlice || !hasBob || !hasCarol) {
            std::cerr << "FAIL [tidbit-count-3]: alice=" << hasAlice
                      << " bob=" << hasBob << " carol=" << hasCarol << "\n";
            ++failures;
        } else {
            std::cout << "PASS [tidbit-count-3]\n";
        }
    }

    // ── BuildGameSeriesPrompt ────────────────────────────────────────────────

    // Prompt must tell the LLM that CORRECT/WRONG are direct answers to the question.
    {
        std::string p = BuildGameSeriesPrompt("What is dynamic_cast?", "It downcasts safely.", 2);
        bool hasQuestion = p.find("QUESTION:") != std::string::npos;
        bool hasCorrect  = p.find("CORRECT:")  != std::string::npos;
        bool hasWrong    = p.find("WRONG:")    != std::string::npos;
        bool hasDirect   = p.find("direct answer") != std::string::npos
                        || p.find("directly answers") != std::string::npos;
        bool hasFalse    = p.find("false answer") != std::string::npos
                        || p.find("incorrect answer") != std::string::npos
                        || p.find("plausible but") != std::string::npos;
        bool hasCount    = p.find("2") != std::string::npos;
        bool ok = hasQuestion && hasCorrect && hasWrong && hasDirect && hasFalse && hasCount;
        if (!ok) {
            std::cerr << "FAIL [game-series-prompt-fields]:"
                      << " q=" << hasQuestion << " c=" << hasCorrect
                      << " w=" << hasWrong << " direct=" << hasDirect
                      << " false=" << hasFalse << " count=" << hasCount << "\n";
            ++failures;
        } else {
            std::cout << "PASS [game-series-prompt-fields]\n";
        }
    }

    // Prompt must include the original question and (truncated) explanation.
    {
        std::string longExpl(600, 'x');
        std::string p = BuildGameSeriesPrompt("Q?", longExpl, 1);
        bool hasQ    = p.find("Q?")    != std::string::npos;
        bool notFull = p.find(longExpl) == std::string::npos; // truncated
        bool ok = hasQ && notFull;
        if (!ok) {
            std::cerr << "FAIL [game-series-prompt-context]: hasQ=" << hasQ
                      << " notFull=" << notFull << "\n";
            ++failures;
        } else {
            std::cout << "PASS [game-series-prompt-context]\n";
        }
    }

    // Personality dropdowns must use click-based .open class, not CSS :hover.
    // CSS :hover on <div> is unreliable in macOS WKWebView.
    {
        QuestionTurn t;
        t.question    = "What is IAM?";
        t.userAnswer  = "Identity and Access Management";
        t.score       = Score::Star5;
        t.explanation = "IAM manages AWS users and permissions.";
        std::string html = RenderExamTurns({t}, {0}, {}, {});
        bool hasOpen  = html.find(".game-drop.open .game-menu") != std::string::npos;
        bool noHover  = html.find(".game-drop:hover") == std::string::npos;
        if (!hasOpen || !noHover) {
            std::cerr << "FAIL [dropdown-open-class]: hasOpen=" << hasOpen
                      << " noHover=" << noHover << "\n";
            ++failures;
        } else {
            std::cout << "PASS [dropdown-open-class]\n";
        }
    }

    // Mermaid hint must include quoted-label guidance so the LLM doesn't emit
    // unquoted node labels with { } or // that break the Mermaid parser.
    {
        ExamConfig cfgLarge = cfg;
        cfgLarge.largeModel = true;
        std::string p = BuildScoringAndNextPrompt(cfgLarge, {}, "What is enum class?", "scoped enums", 2);

        bool hasQuoteGuidance = p.find("quot") != std::string::npos
                             || p.find("\"") != std::string::npos;
        if (!hasQuoteGuidance) {
            std::cerr << "FAIL [mermaid-hint-quoted-labels]: mermaid hint lacks quoted-label guidance\n";
            ++failures;
        } else {
            std::cout << "PASS [mermaid-hint-quoted-labels]\n";
        }
    }

    return failures;
}
