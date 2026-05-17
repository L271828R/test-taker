#include "exam_prompt.h"
#include "markdown.h"
#include <sstream>

// ---------------------------------------------------------------------------
std::string BuildFirstQuestionPrompt(const ExamConfig& cfg) {
    std::ostringstream out;
    out << "You are an expert interviewer and examiner. Your role is to test the user's "
           "knowledge of: " << cfg.topic << ". Difficulty level: " << cfg.difficulty << ".\n\n";

    if (!cfg.instructions.empty()) {
        out << "Specific focus for this session: " << cfg.instructions << "\n\n";
    }

    if (!cfg.projectContext.empty()) {
        out << "Study material (use this to calibrate question relevance):\n\n"
            << cfg.projectContext << "\n\n";
    }

    out << "Rules you must follow:\n"
           "1. Ask exactly one question per turn. Never ask multiple questions at once.\n"
           "2. Questions must be specific and answerable in 1-4 sentences.\n"
           "3. Difficulty guide:\n"
           "   easy:   recall and definition questions.\n"
           "   medium: application and comparison questions.\n"
           "   hard:   analysis, edge cases, performance trade-offs.\n"
           "   mixed:  vary across all three levels.\n"
           "4. Do not repeat a question already asked this session.\n"
           "5. Do not give hints or partial answers before the user responds.\n"
           "6. Your response must be ONLY the question text. No preamble, "
               "no 'Question 1:', no numbering.\n\n"
        << "This is question 1 of " << cfg.totalQuestions << ". Ask your first question now.\n";

    return out.str();
}

// ---------------------------------------------------------------------------
std::string BuildScoringAndNextPrompt(const ExamConfig& cfg,
                                      const std::vector<QuestionTurn>& history,
                                      const std::string& currentQuestion,
                                      const std::string& userAnswer,
                                      int questionsRemaining) {
    std::ostringstream out;
    out << "You are an expert interviewer. You are testing knowledge of: "
        << cfg.topic << ". Difficulty: " << cfg.difficulty << ".\n\n";

    if (!cfg.instructions.empty()) {
        out << "Specific focus for this session: " << cfg.instructions << "\n\n";
    }

    if (!cfg.projectContext.empty()) {
        out << "Study material:\n\n" << cfg.projectContext << "\n\n";
    }

    if (!history.empty()) {
        out << "Session history so far:\n\n";
        for (const auto& t : history) {
            out << "Q: " << t.question << "\n";
            if (t.userAnswer.empty())
                out << "User: (skipped — did not know)\n";
            else
                out << "User: " << t.userAnswer << "\n";
            out << "SCORE: " << ScoreToString(t.score) << "\n\n";
        }
    }

    int questionNumber = (int)history.size() + 1;
    out << "The user just answered question " << questionNumber
        << " of " << cfg.totalQuestions << ":\n\n"
        << "Question: " << currentQuestion << "\n";

    if (userAnswer.empty())
        out << "User's answer: (skipped — did not know)\n\n";
    else
        out << "User's answer: " << userAnswer << "\n\n";

    out << "Your task — respond with exactly three lines in this order, nothing else:\n\n"
           "1. Score the answer:       SCORE: correct  OR  SCORE: partial  OR  SCORE: missed  OR  SCORE: skipped\n"
           "2. Explain the answer:     EXPLANATION: <your explanation in 2-5 sentences>\n";

    if (questionsRemaining > 0) {
        out << "3. Ask the next question:  NEXT_QUESTION: <question text only>\n\n"
               "Output format (exactly):\n"
               "SCORE: ...\n"
               "EXPLANATION: ...\n"
               "NEXT_QUESTION: ...\n";
    } else {
        out << "3. No more questions:      NEXT_QUESTION: \n\n"
               "Output format (exactly):\n"
               "SCORE: ...\n"
               "EXPLANATION: ...\n"
               "NEXT_QUESTION: \n";
    }

    return out.str();
}

// ---------------------------------------------------------------------------
std::string BuildSessionSummaryPrompt(const ExamConfig& cfg,
                                      const std::vector<QuestionTurn>& history) {
    std::ostringstream out;
    out << "You are a study coach. Summarize this exam session on: " << cfg.topic << ".\n\n"
           "Session results:\n\n";

    for (const auto& t : history) {
        out << "Q: " << t.question << "\n";
        if (t.userAnswer.empty())
            out << "A: (skipped)\n";
        else
            out << "A: " << t.userAnswer << "\n";
        out << "SCORE: " << ScoreToString(t.score) << "\n\n";
    }

    out << "Write a brief session summary (3-6 sentences) covering:\n"
           "- Overall score and performance level\n"
           "- 2-3 specific weak areas identified from missed/partial answers\n"
           "- One concrete recommendation for what to study next\n\n"
           "Output only the summary as plain text, starting with \"Score:\".\n";

    return out.str();
}

// ---------------------------------------------------------------------------
ScoredResponse ParseScoredResponse(const std::string& llmOutput) {
    ScoredResponse resp;

    std::istringstream ss(llmOutput);
    std::string line;
    int found = 0;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("SCORE: ", 0) == 0) {
            resp.score = ScoreFromString(line.substr(7));
            ++found;
        } else if (line.rfind("EXPLANATION: ", 0) == 0) {
            resp.explanation = line.substr(13);
            ++found;
        } else if (line.rfind("NEXT_QUESTION: ", 0) == 0) {
            resp.nextQuestion = line.substr(15);
            ++found;
        }
    }

    resp.parseOk = (found >= 2); // SCORE + EXPLANATION minimum
    return resp;
}

// ---------------------------------------------------------------------------
std::string RenderExamTurns(const std::vector<QuestionTurn>& turns) {
    std::ostringstream out;

    out << R"(<style>
.turn { border-bottom:1px solid var(--border); margin-bottom:1.2em;
        padding-bottom:1em; border-radius:6px; padding:0.8em;
        transition:background 0.15s; position:relative; }
.turn:hover { background:var(--surface); }
.turn:hover .flag-btn { opacity:1; }
.flag-btn { position:absolute; top:0.6em; right:0.6em;
            opacity:0; transition:opacity 0.15s;
            background:none; border:1px solid var(--border);
            border-radius:4px; padding:0.15em 0.5em;
            font-size:0.85em; cursor:pointer; color:var(--text-muted);
            text-decoration:none; }
.flag-btn.flagged { color:#e3a000; border-color:#e3a000; opacity:1; }
.question { font-weight:600; margin-bottom:.4em; }
.answer { color:var(--text-muted); margin-bottom:.3em; font-style:italic; }
.verdict { display:inline-block; padding:.15em .6em; border-radius:4px;
           font-size:.85em; font-weight:600; margin-bottom:.4em; }
.verdict.correct { background:#1a7f37; color:#fff; }
.verdict.partial  { background:#9a6700; color:#fff; }
.verdict.missed   { background:#cf222e; color:#fff; }
.verdict.skipped  { background:#57606a; color:#fff; }
.explanation { font-size:.95em; }
</style>
)";

    for (int i = 0; i < (int)turns.size(); ++i) {
        const auto& t = turns[i];
        std::string scoreClass =
            t.score == Score::Correct ? "correct" :
            t.score == Score::Partial ? "partial" :
            t.score == Score::Missed  ? "missed"  : "skipped";
        std::string flagClass = t.flagged ? " flagged" : "";
        std::string flagLabel = t.flagged ? "⚑ flagged" : "⚑ flag";

        out << "<div class='turn'>"
            << "<a class='flag-btn" << flagClass << "' href='testtaker://flag/"
            << i << "'>" << flagLabel << "</a>"
            << "<div class='question'>" << RenderMarkdown(t.question) << "</div>"
            << "<div class='answer'><strong>Your answer:</strong> "
            << EscapeHTML(t.userAnswer.empty() ? "(skipped)" : t.userAnswer)
            << "</div>"
            << "<div class='verdict " << scoreClass << "'>"
            << ScoreLabel(t.score) << "</div>"
            << "<div class='explanation'>" << RenderMarkdown(t.explanation) << "</div>"
            << "</div>\n";
    }

    return out.str();
}
