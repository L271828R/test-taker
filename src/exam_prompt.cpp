#include "exam_prompt.h"
#include "markdown.h"
#include <random>
#include <sstream>
#include <utility>

// ---------------------------------------------------------------------------
std::string PickFocusArea(const std::vector<FocusArea>& areas) {
    if (areas.empty()) return "";
    if (areas.size() == 1) return areas[0].text;

    int totalWeight = 0;
    for (const auto& a : areas) totalWeight += std::max(1, a.stars);

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(1, totalWeight);
    int pick = dist(rng);

    int accum = 0;
    for (const auto& a : areas) {
        accum += std::max(1, a.stars);
        if (pick <= accum) return a.text;
    }
    return areas.back().text;
}

// ---------------------------------------------------------------------------
std::string BuildFirstQuestionPrompt(const ExamConfig& cfg) {
    std::ostringstream out;
    out << "You are an expert interviewer and examiner. Your role is to test the user's "
           "knowledge of: " << cfg.topic << ". Difficulty level: " << cfg.difficulty << ".\n\n";

    if (!cfg.instructions.empty()) {
        out << "Specific focus for this session: " << cfg.instructions << "\n\n";
    }

    if (!cfg.focusAreas.empty()) {
        out << "Focus areas (prioritise questions on these sub-topics): "
            << cfg.focusAreas << "\n\n";
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

    if (!cfg.focusAreas.empty()) {
        out << "Focus areas (prioritise questions on these sub-topics): "
            << cfg.focusAreas << "\n\n";
    }

    if (!cfg.projectContext.empty()) {
        out << "Study material:\n\n" << cfg.projectContext << "\n\n";
    }

    if (!history.empty()) {
        out << "Session history so far:\n\n";
        for (const auto& t : history) {
            out << "Q: " << t.question << "\n";
            if (t.userAnswer.empty())
                out << "User: (none)\n";
            else
                out << "User: " << t.userAnswer << "\n";
            out << "SCORE: " << ScoreToString(t.score) << "\n\n";
        }
    }

    int questionNumber = (int)history.size() + 1;

    if (userAnswer.empty()) {
        // Skipped: omit user-answer entirely so the model has nothing to narrate about.
        // The app sets Score::Skipped; only EXPLANATION + NEXT_QUESTION are needed.
        out << "The student skipped question " << questionNumber
            << " of " << cfg.totalQuestions << " (did not attempt an answer).\n\n"
            << "Question: " << currentQuestion << "\n\n";

        out << "Respond with exactly two lines:\n\n"
               "EXPLANATION: <explain the correct answer in 2-5 sentences. Begin directly with the answer.>\n";
        if (questionsRemaining > 0) {
            out << "NEXT_QUESTION: <next question text only>\n\n"
                   "Output format (exactly):\n"
                   "EXPLANATION: ...\n"
                   "NEXT_QUESTION: ...\n";
        } else {
            out << "NEXT_QUESTION: \n\n"
                   "Output format (exactly):\n"
                   "EXPLANATION: ...\n"
                   "NEXT_QUESTION: \n";
        }
    } else {
        out << "The user just answered question " << questionNumber
            << " of " << cfg.totalQuestions << ":\n\n"
            << "Question: " << currentQuestion << "\n"
            << "User's answer: " << userAnswer << "\n\n";

        out << "Your task — respond with exactly three lines in this order, nothing else:\n\n"
               "1. Score the answer:       SCORE: correct  OR  SCORE: partial  OR  SCORE: missed\n"
               "2. Explain the answer:     EXPLANATION: <state the correct answer and explain it in 2-5 sentences."
               " Do not mention the user, their answer, or lack of answer. Begin directly with the correct answer.>\n";

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
    bool inExplanation = false;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("SCORE: ", 0) == 0) {
            inExplanation = false;
            resp.score = ScoreFromString(line.substr(7));
            ++found;
        } else if (line.rfind("EXPLANATION: ", 0) == 0) {
            resp.explanation = line.substr(13);
            inExplanation = true;
            ++found;
        } else if (line.rfind("NEXT_QUESTION: ", 0) == 0) {
            inExplanation = false;
            resp.nextQuestion = line.substr(15);
            ++found;
        } else if (inExplanation && !line.empty()) {
            resp.explanation += " " + line;
        }
    }

    resp.parseOk = (found >= 2); // SCORE + EXPLANATION minimum
    return resp;
}

// ---------------------------------------------------------------------------
std::string RenderExamTurns(const std::vector<QuestionTurn>& turns,
                             const std::vector<int>&          chatCounts) {
    std::ostringstream out;

    out << R"(<style>
.turn { border-bottom:1px solid var(--border); margin-bottom:1.2em;
        border-radius:6px; padding:0.8em;
        transition:background 0.15s; }
.turn:hover { background:var(--surface); }
.turn-toolbar { display:flex; gap:0.4em; margin-bottom:0.6em; }
.turn:hover .flag-btn { opacity:1; }
.turn:hover .note-btn { opacity:1; }
.turn:hover .discuss-btn { opacity:1; }
.turn:hover .save-btn { opacity:1; }
.flag-btn, .note-btn, .discuss-btn, .save-btn {
            opacity:0; transition:opacity 0.15s;
            background:none; border:1px solid var(--border);
            border-radius:4px; padding:0.15em 0.5em;
            font-size:0.82em; cursor:pointer; color:var(--text-muted);
            text-decoration:none; white-space:nowrap; }
.flag-btn.flagged { color:#e3a000; border-color:#e3a000; opacity:1; }
.note-btn.has-note { color:var(--link); border-color:var(--link); opacity:1; }
.discuss-btn.has-chat { color:var(--link); border-color:var(--link); opacity:1; }
.save-btn.saved { color:#1a7f37; border-color:#1a7f37; opacity:1; }
.turn-note { margin-top:0.5em; padding:0.4em 0.6em;
             background:var(--surface); border-left:3px solid var(--link);
             font-size:0.9em; color:var(--text-muted); white-space:pre-wrap; }
.question { font-weight:600; margin-bottom:.4em; }
.answer { color:var(--text-muted); margin-bottom:.3em; font-size:.95em; }
.answer-label { font-style:italic; font-weight:600; }
.answer-body { margin-top:.25em; font-style:normal; color:var(--text); }
.answer-body p:first-child { margin-top:0; }
.answer-body p:last-child  { margin-bottom:0; }
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
        std::string noteClass = t.note.empty() ? "" : " has-note";
        std::string noteLabel = t.note.empty() ? "✎ note" : "✎ note";
        int chatCount = (i < (int)chatCounts.size()) ? chatCounts[i] : 0;
        std::string discussClass = chatCount > 0 ? " has-chat" : "";
        std::string discussLabel = chatCount > 0
            ? "&#x1F4AC; " + std::to_string(chatCount)
            : "&#x1F4AC; discuss";
        std::string saveClass = t.saved ? " saved" : "";
        std::string saveLabel = t.saved ? "&#x1F516; saved" : "&#x1F516; save";

        out << "<div class='turn'>"
            << "<div class='turn-toolbar'>"
            << "<a class='discuss-btn" << discussClass << "' href='testtaker://discuss/"
            << i << "'>" << discussLabel << "</a>"
            << "<a class='note-btn" << noteClass << "' href='testtaker://note/"
            << i << "'>" << noteLabel << "</a>"
            << "<a class='save-btn" << saveClass << "' href='testtaker://save/"
            << i << "'>" << saveLabel << "</a>"
            << "<a class='flag-btn" << flagClass << "' href='testtaker://flag/"
            << i << "'>" << flagLabel << "</a>"
            << "</div>"
            << "<div class='question'>" << RenderMarkdown(t.question) << "</div>"
            << "<div class='answer'>"
            << "<span class='answer-label'>Your answer:</span>"
            << "<div class='answer-body'>"
            << (t.userAnswer.empty() ? "<em>(skipped)</em>" : RenderMarkdown(t.userAnswer))
            << "</div></div>"
            << "<div class='verdict " << scoreClass << "'>"
            << ScoreLabel(t.score) << "</div>"
            << "<div class='explanation'>" << RenderMarkdown(t.explanation) << "</div>";
        if (!t.note.empty())
            out << "<div class='turn-note'>" << EscapeHTML(t.note) << "</div>";
        out << "</div>\n";
    }

    return out.str();
}

// ---------------------------------------------------------------------------
std::string RenderHistoryGroups(const std::vector<HistoryGroup>& groups) {
    std::ostringstream out;

    out << R"(<style>
.history-header { display:flex; justify-content:space-between; align-items:center;
                  margin-bottom:0.8em; }
.history-header h3 { margin:0; font-size:0.9em; color:var(--text-muted); text-transform:uppercase;
                     letter-spacing:0.05em; }
.clear-hist-btn { font-size:0.8em; color:var(--text-muted); text-decoration:none;
                  border:1px solid var(--border); border-radius:4px; padding:0.1em 0.5em; }
.clear-hist-btn:hover { color:#cf222e; border-color:#cf222e; }
.hist-group { margin-bottom:1.6em; }
.hist-group-label { font-size:0.78em; font-weight:600; color:var(--text-muted);
                    text-transform:uppercase; letter-spacing:0.05em;
                    margin-bottom:0.4em; padding-bottom:0.2em;
                    border-bottom:1px solid var(--border); }
.hist-turn { border-bottom:1px solid var(--border); margin-bottom:0.8em;
             border-radius:4px; padding:0.6em 0.8em; opacity:0.85;
             transition:background 0.15s; }
.hist-turn:hover { opacity:1; background:var(--surface); }
.hist-turn-toolbar { display:flex; gap:0.4em; margin-bottom:0.4em; }
.hist-turn:hover .flag-btn  { opacity:1; }
.hist-turn:hover .note-btn  { opacity:1; }
.hist-turn:hover .discuss-btn { opacity:1; }
.hist-turn:hover .save-btn  { opacity:1; }
.flag-btn, .note-btn, .discuss-btn, .save-btn {
    opacity:0; transition:opacity 0.15s;
    background:none; border:1px solid var(--border);
    border-radius:4px; padding:0.15em 0.5em;
    font-size:0.82em; cursor:pointer; color:var(--text-muted);
    text-decoration:none; white-space:nowrap; }
.flag-btn.flagged   { color:#e3a000; border-color:#e3a000; opacity:1; }
.note-btn.has-note  { color:var(--link); border-color:var(--link); opacity:1; }
.save-btn.saved     { color:#1a7f37; border-color:#1a7f37; opacity:1; }
.discuss-btn.has-chat { color:var(--link); border-color:var(--link); opacity:1; }
.hist-question { font-weight:600; margin-bottom:.3em; font-size:0.95em; }
.hist-answer   { color:var(--text-muted); font-size:.88em; margin-bottom:.25em; }
.hist-verdict  { display:inline-block; padding:.1em .5em; border-radius:4px;
                 font-size:.8em; font-weight:600; margin-bottom:.3em; }
.hist-verdict.correct { background:#1a7f37; color:#fff; }
.hist-verdict.partial  { background:#9a6700; color:#fff; }
.hist-verdict.missed   { background:#cf222e; color:#fff; }
.hist-verdict.skipped  { background:#57606a; color:#fff; }
.hist-expl { font-size:.88em; }
.hist-note { margin-top:0.4em; padding:0.4em 0.6em;
             background:var(--surface); border-left:3px solid var(--link);
             font-size:0.9em; color:var(--text-muted); white-space:pre-wrap; }
.hist-separator { border:none; border-top:2px solid var(--border);
                  margin:2em 0 1.4em; }
</style>
)";

    out << "<div class='history-header'>"
        << "<h3>Session history</h3>"
        << "<a class='clear-hist-btn' href='testtaker://clear-history'>&#x2715; Clear history</a>"
        << "</div>\n";

    for (int g = 0; g < (int)groups.size(); ++g) {
        const auto& grp = groups[g];
        out << "<div class='hist-group'>"
            << "<div class='hist-group-label'>" << EscapeHTML(grp.label) << "</div>";
        for (int i = 0; i < (int)grp.turns.size(); ++i) {
            const auto& t = grp.turns[i];
            std::string scoreClass =
                t.score == Score::Correct ? "correct" :
                t.score == Score::Partial ? "partial" :
                t.score == Score::Missed  ? "missed"  : "skipped";
            std::string flagClass  = t.flagged     ? " flagged"  : "";
            std::string flagLabel  = t.flagged     ? "&#x2691; flagged" : "&#x2691; flag";
            std::string noteClass  = t.note.empty() ? ""          : " has-note";
            std::string saveClass  = t.saved        ? " saved"    : "";
            std::string saveLabel  = t.saved        ? "&#x1F516; saved" : "&#x1F516; save";
            std::string gStr       = std::to_string(g);
            std::string iStr       = std::to_string(i);

            out << "<div class='hist-turn'>"
                << "<div class='hist-turn-toolbar'>"
                << "<a class='discuss-btn' href='testtaker://hdiscuss/" << gStr << "/" << iStr
                << "'>&#x1F4AC; discuss</a>"
                << "<a class='note-btn" << noteClass << "' href='testtaker://hnote/" << gStr << "/" << iStr
                << "'>&#x270E; note</a>"
                << "<a class='save-btn" << saveClass << "' href='testtaker://hsave/" << gStr << "/" << iStr
                << "'>" << saveLabel << "</a>"
                << "<a class='flag-btn" << flagClass << "' href='testtaker://hflag/" << gStr << "/" << iStr
                << "'>" << flagLabel << "</a>"
                << "</div>"
                << "<div class='hist-question'>" << RenderMarkdown(t.question) << "</div>"
                << "<div class='hist-answer'>"
                << (t.userAnswer.empty() ? "<em>(skipped)</em>" : RenderMarkdown(t.userAnswer))
                << "</div>"
                << "<div class='hist-verdict " << scoreClass << "'>" << ScoreLabel(t.score) << "</div>"
                << "<div class='hist-expl'>" << RenderMarkdown(t.explanation) << "</div>";
            if (!t.note.empty())
                out << "<div class='hist-note'>" << EscapeHTML(t.note) << "</div>";
            out << "</div>\n";
        }
        out << "</div>\n";
    }

    out << "<hr class='hist-separator'>\n";
    return out.str();
}
