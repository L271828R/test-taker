#include "exam_prompt.h"
#include "markdown.h"
#include <random>
#include <sstream>

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

static void AppendTopicWeights(std::ostringstream& out, const ExamConfig& cfg) {
    if (!cfg.moreOfTopics.empty()) {
        out << "Explore more of these topics: ";
        for (size_t i = 0; i < cfg.moreOfTopics.size(); ++i) {
            if (i) out << ", ";
            out << cfg.moreOfTopics[i];
        }
        out << "\n";
    }
    if (!cfg.lessOfTopics.empty()) {
        out << "Reduce questions about: ";
        for (size_t i = 0; i < cfg.lessOfTopics.size(); ++i) {
            if (i) out << ", ";
            out << cfg.lessOfTopics[i];
        }
        out << "\n";
    }
    if (!cfg.moreOfTopics.empty() || !cfg.lessOfTopics.empty()) out << "\n";
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

    AppendTopicWeights(out, cfg);

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

    AppendTopicWeights(out, cfg);

    if (!cfg.projectContext.empty()) {
        out << "Study material:\n\n" << cfg.projectContext << "\n\n";
    }

    if (!history.empty()) {
        bool anyVisible = false;
        for (const auto& t : history) if (!t.silentSkip) { anyVisible = true; break; }
        if (anyVisible) {
            out << "Session history so far:\n\n";
            for (const auto& t : history) {
                if (t.silentSkip) continue;
                out << "Q: " << t.question << "\n";
                if (t.userAnswer.empty())
                    out << "User: (none)\n";
                else
                    out << "User: " << t.userAnswer << "\n";
                out << "SCORE: " << ScoreToString(t.score) << "\n\n";
            }
        }
    }

    int questionNumber = (int)history.size() + 1;

    // Mermaid hint — only for large cloud models that reliably generate fenced code blocks.
    const std::string mermaidHint = cfg.largeModel
        ? " If a diagram would clarify the concept, add a ```mermaid``` code block"
          " after the explanation text."
        : "";

    // Pick one personality at random and bake it into the instruction.
    // Giving the LLM a list and asking it to "pick one" causes it to always
    // Pick up to tidbitCount distinct personalities for this turn.
    std::string tidbitInstruction;
    std::string tidbitFormatExample;
    if (!cfg.personalities.empty()) {
        static std::mt19937 rng(std::random_device{}());
        std::vector<std::string> pool = cfg.personalities;
        std::shuffle(pool.begin(), pool.end(), rng);
        int n = std::max(1, std::min(cfg.tidbitCount, (int)pool.size()));
        pool.resize(n);

        for (const auto& p : pool) {
            tidbitFormatExample +=
                ":::tidbit[" + p + "]\n"
                "<comment>\n"
                ":::\n";
        }
        if (n == 1) {
            const std::string& p = pool[0];
            tidbitInstruction =
                " After the explanation, on a new line add a :::tidbit[" + p + "] block"
                " in " + p + "'s characteristic voice"
                " for a brief insight (1-3 sentences). Format exactly:\n"
                ":::tidbit[" + p + "]\n"
                "<comment>\n"
                ":::";
        } else {
            tidbitInstruction =
                " After the explanation, add " + std::to_string(n) +
                " tidbit blocks, each in the named character's voice (1-3 sentences each)."
                " Format exactly:\n" + tidbitFormatExample;
        }
    }

    if (userAnswer.empty()) {
        // Skipped: omit user-answer entirely so the model has nothing to narrate about.
        // The app sets Score::Skipped; only EXPLANATION + NEXT_QUESTION are needed.
        out << "The student skipped question " << questionNumber
            << " of " << cfg.totalQuestions << " (did not attempt an answer).\n\n"
            << "Question: " << currentQuestion << "\n\n";

        out << "Respond with exactly two sections:\n\n"
               "EXPLANATION: <explain the correct answer in 2-5 sentences. Begin directly with the answer."
            << mermaidHint << tidbitInstruction << ">\n";
        if (questionsRemaining > 0) {
            out << "NEXT_QUESTION: <next question text only>\n\n"
                   "Output format (exactly):\n"
                   "EXPLANATION: ...\n"
                << tidbitFormatExample
                << "NEXT_QUESTION: ...\n";
        } else {
            out << "NEXT_QUESTION: \n\n"
                   "Output format (exactly):\n"
                   "EXPLANATION: ...\n"
                << tidbitFormatExample
                << "NEXT_QUESTION: \n";
        }
    } else {
        out << "The user just answered question " << questionNumber
            << " of " << cfg.totalQuestions << ":\n\n"
            << "Question: " << currentQuestion << "\n"
            << "User's answer: " << userAnswer << "\n\n";

        out << "Your task — respond in this order, nothing else:\n\n"
               "1. Score the answer:       SCORE: 1  through  SCORE: 5\n"
               "   1 = completely wrong, 2 = major gaps, 3 = half right,\n"
               "   4 = mostly right with minor gaps, 5 = fully right\n"
               "2. Explain the answer:     EXPLANATION: <state the correct answer and explain it in 3-6 sentences."
               " Include a concrete example when helpful. Do not mention the user, their answer, or lack of answer. Begin directly with the correct answer."
            << mermaidHint << tidbitInstruction << ">\n";

        if (questionsRemaining > 0) {
            out << "3. Ask the next question:  NEXT_QUESTION: <question text only>\n\n"
                   "Output format (exactly):\n"
                   "SCORE: ...\n"
                   "EXPLANATION: ...\n"
                << tidbitFormatExample
                << "NEXT_QUESTION: ...\n";
        } else {
            out << "3. No more questions:      NEXT_QUESTION: \n\n"
                   "Output format (exactly):\n"
                   "SCORE: ...\n"
                   "EXPLANATION: ...\n"
                << tidbitFormatExample
                << "NEXT_QUESTION: \n";
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
        } else if (inExplanation) {
            resp.explanation += "\n" + line;
        }
    }

    resp.parseOk = (found >= 2); // SCORE + EXPLANATION minimum
    return resp;
}

// ---------------------------------------------------------------------------
std::string RenderExamTurns(const std::vector<QuestionTurn>& turns,
                             const std::vector<int>&          chatCounts,
                             const std::vector<std::string>&  moreOfTopics,
                             const std::vector<std::string>&  lessOfTopics) {
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
.flag-btn, .note-btn, .discuss-btn, .save-btn, .more-btn, .less-btn,
.game-btn, .explain-btn, .learn-btn {
            opacity:0; transition:opacity 0.15s;
            background:none; border:1px solid var(--border);
            border-radius:4px; padding:0.15em 0.5em;
            font-size:0.82em; cursor:pointer; color:var(--text-muted);
            text-decoration:none; white-space:nowrap; }
.turn:hover .game-btn    { opacity:1; }
.turn:hover .explain-btn { opacity:1; }
.turn:hover .learn-btn   { opacity:1; }
.whynot-btn { opacity:0; transition:opacity 0.15s; text-decoration:none;
              margin-left:5px; font-size:0.95em; cursor:pointer; vertical-align:middle; }
.turn:hover .whynot-btn { opacity:1; }
.whynot-btn.open        { opacity:1; }
/* Keep all toolbar buttons visible for whichever turn's chat panel is open */
.turn.active .flag-btn, .turn.active .note-btn, .turn.active .discuss-btn,
.turn.active .save-btn, .turn.active .game-btn, .turn.active .explain-btn,
.turn.active .learn-btn, .turn.active .more-btn, .turn.active .less-btn,
.turn.active .whynot-btn { opacity:1; }
.turn.active { background:var(--surface); }
.game-drop { position:relative; display:inline-block; }
.game-drop:hover .game-menu,
.game-drop:focus-within .game-menu { display:block; }
.game-menu { display:none; position:absolute; top:100%; left:0;
             min-width:130px; background:var(--surface);
             border:1px solid var(--border); border-radius:4px;
             box-shadow:0 4px 12px rgba(0,0,0,.18); z-index:999; padding:2px 0; }
.game-menu a { display:block; padding:5px 11px; color:var(--text);
               text-decoration:none; font-size:.82em; white-space:nowrap; }
.game-menu a:hover { background:var(--surface-hover,rgba(0,0,0,.06)); }
.learn-btn.saving { color:#9a6700; border-color:#9a6700; opacity:1; }
.learn-btn.done   { color:#1a7f37; border-color:#1a7f37; opacity:1; }
.flag-btn.flagged { color:#e3a000; border-color:#e3a000; opacity:1; }
.note-btn.has-note { color:var(--link); border-color:var(--link); opacity:1; }
.discuss-btn.has-chat { color:var(--link); border-color:var(--link); opacity:1; }
.save-btn.saved { color:#1a7f37; border-color:#1a7f37; opacity:1; }
.more-btn.voted { color:#1a7f37; border-color:#1a7f37; opacity:1; }
.less-btn.voted { color:#cf222e; border-color:#cf222e; opacity:1; }
.turn:hover .more-btn { opacity:1; }
.turn:hover .less-btn { opacity:1; }
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
.verdict.s1 { background:#cf222e; color:#fff; }
.verdict.s2 { background:#e36b0a; color:#fff; }
.verdict.s3 { background:#9a6700; color:#fff; }
.verdict.s4 { background:#2da44e; color:#fff; }
.verdict.s5 { background:#1a7f37; color:#fff; }
.verdict.skipped { background:#57606a; color:#fff; }
.verdict.silent  { background:#57606a; color:#fff; opacity:0.6; }
.explanation { font-size:.95em; }
</style>
)";

    auto makeSnippet = [](const std::string& q) -> std::string {
        std::string s = q.substr(0, 80);
        auto nl = s.find('\n');
        if (nl != std::string::npos) s = s.substr(0, nl);
        if (s.size() > 60) s = s.substr(0, 57) + "...";
        return s;
    };
    auto inList = [](const std::vector<std::string>& list, const std::string& item) {
        for (const auto& e : list) if (e == item) return true;
        return false;
    };

    for (int i = 0; i < (int)turns.size(); ++i) {
        const auto& t = turns[i];
        std::string snippet = makeSnippet(t.question);
        std::string scoreClass =
            t.score == Score::Skipped ? "skipped" : "s" + ScoreToString(t.score);
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
        std::string moreClass = inList(moreOfTopics, snippet) ? " voted" : "";
        std::string lessClass = inList(lessOfTopics, snippet) ? " voted" : "";

        out << "<div class='turn' id='turn-" << i << "'>"
            << "<div class='turn-toolbar'>"
            << "<a class='more-btn" << moreClass << "' href='testtaker://more/"
            << i << "'>&#x1F44D;</a>"
            << "<a class='less-btn" << lessClass << "' href='testtaker://less/"
            << i << "'>&#x1F44E;</a>"
            << "<a class='discuss-btn" << discussClass << "' href='testtaker://discuss/"
            << i << "'>" << discussLabel << "</a>";
        if (!t.explanation.empty() && !t.silentSkip)
            out << RenderPersonalityDropdowns("testtaker://explain/", "/" + std::to_string(i));
        out
            << "<a class='note-btn" << noteClass << "' href='testtaker://note/"
            << i << "'>" << noteLabel << "</a>"
            << "<a class='save-btn" << saveClass << "' href='testtaker://save/"
            << i << "'>" << saveLabel << "</a>"
            << "<a class='flag-btn" << flagClass << "' href='testtaker://flag/"
            << i << "'>" << flagLabel << "</a>";
        if (!t.explanation.empty() && !t.silentSkip)
            out << "<div class='game-drop'>"
                << "<span class='game-btn'>&#x1F3AE; game &#x25BE;</span>"
                << "<div class='game-menu'>"
                << "<a href='testtaker://game/"  << i << "'>&#x1F426; Flappy Bird</a>"
                << "<a href='testtaker://rocks/" << i << "'>&#x1F4AB; Asteroids</a>"
                << "</div></div>";
        // "learn more" appears only for wrong/partial answers (score 1-3)
        if (!t.silentSkip && !t.explanation.empty() &&
            (t.score == Score::Star1 || t.score == Score::Star2 || t.score == Score::Star3))
            out << "<a class='learn-btn' href='testtaker://learnmore/"
                << i << "'>&#x1F4D6; learn more</a>";
        out << "</div>"
            << "<div class='question'>" << RenderMarkdown(t.question) << "</div>";
        if (t.silentSkip) {
            out << "<div class='verdict silent'>&#x23ED; silently skipped</div>";
        } else {
            out << "<div class='answer'>"
                << "<span class='answer-label'>Your answer:</span>"
                << "<div class='answer-body'>"
                << (t.userAnswer.empty() ? "<em>(skipped)</em>" : RenderMarkdown(t.userAnswer))
                << "</div></div>"
                << "<div class='verdict " << scoreClass << "'>"
                << ScoreLabel(t.score);
            if (t.score != Score::Star5 && !t.explanation.empty())
                out << "<a class='whynot-btn' href='testtaker://whynot/" << i
                    << "' title='Why not perfect?'>&#x1F914;</a>";
            out << "</div>"
                << "<div class='explanation'>" << RenderMarkdown(t.explanation) << "</div>";
            if (!t.note.empty())
                out << "<div class='turn-note'>" << EscapeHTML(t.note) << "</div>";
        }
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
.hist-turn:hover .game-btn    { opacity:1; }
.hist-turn:hover .explain-btn { opacity:1; }
.hist-turn:hover .game-drop .game-btn    { opacity:1; }
.hist-turn:hover .game-drop .explain-btn { opacity:1; }
.flag-btn, .note-btn, .discuss-btn, .save-btn, .game-btn, .explain-btn {
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
.hist-verdict.s1 { background:#cf222e; color:#fff; }
.hist-verdict.s2 { background:#e36b0a; color:#fff; }
.hist-verdict.s3 { background:#9a6700; color:#fff; }
.hist-verdict.s4 { background:#2da44e; color:#fff; }
.hist-verdict.s5 { background:#1a7f37; color:#fff; }
.hist-verdict.skipped { background:#57606a; color:#fff; }
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
                t.score == Score::Skipped ? "skipped" : "s" + ScoreToString(t.score);
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
                << "'>&#x1F4AC; discuss</a>";
            if (!t.explanation.empty() && !t.silentSkip)
                out << RenderPersonalityDropdowns("testtaker://hexplain/", "/" + gStr + "/" + iStr);
            out
                << "<a class='note-btn" << noteClass << "' href='testtaker://hnote/" << gStr << "/" << iStr
                << "'>&#x270E; note</a>"
                << "<a class='save-btn" << saveClass << "' href='testtaker://hsave/" << gStr << "/" << iStr
                << "'>" << saveLabel << "</a>"
                << "<a class='flag-btn" << flagClass << "' href='testtaker://hflag/" << gStr << "/" << iStr
                << "'>" << flagLabel << "</a>";
            if (!t.explanation.empty() && !t.silentSkip)
                out << "<div class='game-drop'>"
                    << "<span class='game-btn'>&#x1F3AE; game &#x25BE;</span>"
                    << "<div class='game-menu'>"
                    << "<a href='testtaker://hgame/"  << gStr << "/" << iStr << "'>&#x1F426; Flappy Bird</a>"
                    << "<a href='testtaker://hrocks/" << gStr << "/" << iStr << "'>&#x1F4AB; Asteroids</a>"
                    << "</div></div>";
            out << "</div>"
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

// ---------------------------------------------------------------------------
std::string BuildGameChoicesPrompt(const std::string& question,
                                   const std::string& explanation) {
    std::ostringstream out;
    out << "You are creating a quiz challenge for a student.\n\n"
        << "Question: " << question << "\n\n";
    if (!explanation.empty()) {
        std::string expl = explanation.size() > 350 ? explanation.substr(0, 350) : explanation;
        out << "Explanation: " << expl << "\n\n";
    }
    out << "Write EXACTLY two short statements (each under 80 characters):\n"
        << "CORRECT: a true statement that directly answers the question\n"
        << "WRONG: a plausible but false statement about the same topic\n\n"
        << "Reply with ONLY those two lines, nothing else.";
    return out.str();
}

std::string BuildGameSeriesPrompt(const std::string& question,
                                  const std::string& explanation,
                                  int count) {
    std::ostringstream out;
    out << "You are creating quiz challenges for a student learning this topic.\n\n"
        << "Original question: " << question << "\n\n";
    if (!explanation.empty()) {
        std::string expl = explanation.size() > 500 ? explanation.substr(0, 500) : explanation;
        out << "Explanation: " << expl << "\n\n";
    }
    out << "Generate " << count << " follow-up questions that each test a different aspect "
        << "of the same topic.\n\n"
        << "Rules:\n"
        << "- QUESTION must be a specific, answerable question (under 100 chars).\n"
        << "- CORRECT must be a direct answer to that exact question (under 80 chars).\n"
        << "- WRONG must be a plausible but incorrect answer to that same question (under 80 chars).\n"
        << "- CORRECT and WRONG must both read as candidate answers to QUESTION — "
        << "not general statements about the topic.\n\n"
        << "For each question output EXACTLY three lines then a separator:\n"
        << "QUESTION: <question>\n"
        << "CORRECT: <direct answer — correct>\n"
        << "WRONG: <direct answer — incorrect but plausible>\n"
        << "---\n\n"
        << "Output ONLY those lines. No numbering, no extra text. "
        << "The last block does not need a trailing ---.";
    return out.str();
}

// ---------------------------------------------------------------------------
std::string BuildLearnMorePrompt(const std::string& question,
                                 const std::string& briefExplanation) {
    std::ostringstream out;
    out << "You are an expert teacher. A student got the following question wrong or only partially right.\n\n"
        << "Question: " << question << "\n\n";
    if (!briefExplanation.empty()) {
        out << "Brief explanation already shown: " << briefExplanation << "\n\n";
    }
    out << "Write a thorough deep-dive explanation so the student truly understands this topic. "
           "Write in plain markdown.\n\n"
        << "Requirements:\n"
        << "- Begin with a clear, direct statement of the correct answer.\n"
        << "- Explain the underlying concept in depth (5-10 sentences).\n"
        << "- Include at least one concrete, practical example.\n"
        << "- If a diagram would help understanding, add a ```mermaid``` fenced code block.\n"
        << "- If the topic involves code, include a short illustrative ```code``` fenced block.\n"
        << "- Use plain markdown (headers, bullets, fenced blocks are fine). No preamble, no sign-off.\n";
    return out.str();
}

// ---------------------------------------------------------------------------
std::string BuildHintPrompt(const std::string& question) {
    std::ostringstream out;
    out << "A student is stuck on this exam question and needs a hint.\n\n"
        << "Question: " << question << "\n\n"
        << "Give a Socratic hint in 1-3 sentences that nudges them toward the answer "
           "WITHOUT stating or implying the answer itself.\n"
        << "Rules:\n"
        << "- Point to the right concept, framework, or angle to think from.\n"
        << "- Do NOT use phrases like 'the answer is', 'the correct answer', 'you should say'.\n"
        << "- Do NOT give away what the answer is, even partially.\n"
        << "- Be concise. No preamble (do not start with 'Sure!' or 'Here's a hint:').\n";
    return out.str();
}

// ---------------------------------------------------------------------------
std::string BuildWhyNotPerfectPrompt(const std::string& question,
                                     const std::string& userAnswer,
                                     const std::string& explanation,
                                     Score              score) {
    const char* scoreLabel = "partial";
    switch (score) {
        case Score::Star4: scoreLabel = "mostly correct (4/5)";   break;
        case Score::Star3: scoreLabel = "partially correct (3/5)"; break;
        case Score::Star2: scoreLabel = "mostly wrong (2/5)";      break;
        case Score::Star1: scoreLabel = "incorrect (1/5)";         break;
        default: break;
    }
    std::ostringstream out;
    out << "A student answered an exam question and received a score of " << scoreLabel << ".\n\n"
        << "Question: " << question << "\n"
        << "Student's answer: " << (userAnswer.empty() ? "(skipped)" : userAnswer) << "\n"
        << "Correct explanation: " << explanation << "\n\n"
        << "Please do two things:\n"
        << "1. Explain specifically and concisely what was missing, imprecise, or incorrect "
           "in the student's answer that prevented a perfect score. Be direct but constructive.\n"
        << "2. Give 2-3 concrete examples of answers that would have earned full marks. "
           "Show the level of detail and precision expected.\n\n"
        << "Keep the tone encouraging. No preamble like 'Great question!' or 'Sure!'.";
    return out.str();
}

// ---------------------------------------------------------------------------
// Personality table — each entry drives URLs, menus, and LLM prompts.
// Add a new persona here; the URL handlers, menus, and prompts update automatically.
// ---------------------------------------------------------------------------
const std::vector<PersonalityDef> kPersonalities = {
    // ── "Explain like" ──────────────────────────────────────────────────────
    {
        "monkey", "explain",
        "&#x1F412;&#x1F34C; Monkeys &amp; Bananas",
        "\xf0\x9f\x90\x92\xf0\x9f\x8d\x8c Explain with monkeys & bananas",
        R"(Explain the following exam question and its answer using ONLY monkeys and bananas as your analogy. Monkeys are the actors; bananas are the resource, reward, or currency. Make it vivid, fun, and memorable — something that will stick in the student's memory.)",
        R"(Write 2-4 short paragraphs. Lead with the monkey/banana analogy, then briefly connect it back to the actual concept. No preamble like 'Sure!' or 'Great question!'. After this explanation the student may ask follow-up questions — answer those normally, without forcing the monkey/banana theme.)",
    },
    {
        "caveman", "explain",
        "&#x1FAA8; Caveman",
        "\xf0\x9f\xaa\xa8 Caveman explain",
        R"(You are a caveman teacher. Explain the following concept using caveman speech: short, simple words, grunts, cave-world metaphors (fire, rock, hunt, tribe, mammoth, cave, sky, etc.). Use phrases like "UGH", "GOOD THING", "BAD THING", "THIS LIKE...", "CAVEMAN SEE", "TRIBE KNOW". Break complex ideas into primitive cause-and-effect. Be enthusiastic and funny but still get the concept across — the student should actually understand it by the end.)",
        R"(Write 2-4 short paragraphs in full caveman voice. End by connecting back to the real concept in one plain sentence so the student knows what they learned. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — answer those in caveman voice too.)",
    },
    {
        "flirty", "explain",
        "&#x1F485; Flirty Woman",
        "\xf0\x9f\x92\x85 Flirty Woman",
        R"(You are a charming, witty woman at a bar explaining a technical concept to someone you've just met. Between your explanations, weave in small stage-direction asides in italics — things like: *pulls her hair back slowly*, *glances around the bar*, *takes a long sip of her drink*, *leans in a little closer*, *twirls her straw*, *laughs lightly and shakes her head*, *sets her glass down*, *raises an eyebrow*. Keep the tone playful, confident, and a little flirty — but the concept must still be clearly understood by the end. The persona is the delivery, not a distraction.)",
        R"(Write 3-5 short paragraphs, opening by setting the bar scene, then alternating between explanation and stage directions. End with a crisp one-liner that nails the concept — maybe with a wink. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — stay in character throughout.)",
    },
    {
        "survival", "explain",
        "&#x1FA96; Survival Expert",
        "\xf0\x9f\xaa\x96 Survival Expert",
        R"(You are a grizzled, no-nonsense survival expert — think Bear Grylls meets a field medic who has spent decades in the wild. Explain the following concept entirely through survival, wilderness, and field-craft analogies: tools = gear, errors = bad weather, systems = shelter, failure = running out of water, good design = knowing your exit routes. Be direct, practical, and urgent — every explanation should feel like advice that could keep someone alive. Use short, punchy sentences. You can reference real survival situations (building a fire, reading terrain, rationing supplies, signalling for rescue) as metaphors.)",
        R"(Write 3-4 tight paragraphs in full survival-expert voice. Open by framing the concept as a survival scenario. End with a single field rule — something like a mantra a soldier would tattoo on their arm. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — stay in character and keep the field-craft metaphors going.)",
    },
    {
        "therapist", "explain",
        "&#x1F6CB;&#xFE0F; Therapist",
        "\xf0\x9f\x9b\x8b Therapist",
        R"(You are a warm, perceptive relationship therapist. Explain the following concept entirely through the lens of relationships, emotions, and interpersonal dynamics. Map technical ideas onto relationship situations: two components that depend on each other are partners with attachment issues; a resource leak is a relationship where one person is emotionally draining the other; a race condition is two people talking over each other without listening; a timeout is a healthy boundary. Use empathetic, non-judgmental language. Ask occasional rhetorical questions that invite the student to reflect — 'Have you ever felt like you were waiting for someone to respond, but the message never came?' Keep it insightful and a little humorous, but the core concept must land clearly.)",
        R"(Write 3-4 paragraphs in full therapist voice. Open by naming the 'relationship pattern' you see in the concept. Close with a gentle insight — something that reframes the concept in a way that feels almost therapeutic. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — stay in the therapist persona throughout.)",
    },
    {
        "comedian", "explain",
        "&#x1F3A4; Black Comedian",
        "\xf0\x9f\x8e\xa4 Black Comedian",
        R"(You are a sharp, confident Black stand-up comedian — think Dave Chappelle meets Richard Pryor meets Wanda Sykes. Explain the following concept through observational humor, cultural references, and crowd-work energy. Call out the absurdity in the concept. Use rhetorical asides like 'You know what I'm sayin'?', 'Because that's what it does — every time!', 'And nobody talks about this!', 'I'm just sayin'...'. Draw comparisons to everyday Black American life, family dynamics, or street wisdom where it fits naturally — don't force it, let it land. The concept must be crystal clear by the end — comedy is the vehicle, understanding is the destination. No preamble like 'Sure!' or 'Great question!'.)",
        R"(Write 3-4 punchy paragraphs in full stand-up voice — like you're on stage with a mic. Mix the laughs with genuine insight. End with a punchline that also happens to nail the concept. After this, the student may ask follow-up questions — stay in comedian mode, keep the crowd warm.)",
    },
    {
        "machiavelli", "explain",
        "&#x1F5E1;&#xFE0F; Machiavelli",
        "\xf0\x9f\x97\xa1\xef\xb8\x8f Machiavelli",
        R"(You are Niccolò Machiavelli — the Renaissance Florentine statesman, historian, and author of The Prince. Explain the following concept entirely through the lens of power, political strategy, and the nature of men. Map technical ideas onto statecraft: a system dependency is a vassal state; a bug is a conspiracy within the court; a race condition is two princes contending for the same throne; a well-designed API is a treaty that benefits both parties while secretly advantaging one. Speak with cold, clear authority. Acknowledge complexity without sentimentality. Quote or paraphrase The Prince where it genuinely fits. Be direct — the wise ruler wastes no words.)",
        R"(Write 3-4 paragraphs in Machiavelli's voice — measured, precise, slightly ominous. Open by naming what kind of power problem this concept represents. Close with a maxim — a single sentence of ruthless wisdom that crystallises the concept. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — remain the counsellor to princes throughout.)",
    },
    {
        "rapper", "explain",
        "&#x1F3A4; Freestyle Rapper",
        "\xf0\x9f\x8e\xb5 Freestyle Rapper",
        R"(You are a freestyle rapper. Explain the following concept entirely in rap — flowing bars, AABB or ABAB rhyme schemes, internal rhymes, wordplay. Use technical terms from the concept as punchlines. Keep the energy high, the flow tight, and the content accurate. Every bar must actually teach something. You can break it into verses and a short hook. Do NOT break character — no prose explanations, no preamble, just bars from the first line to the last.)",
        R"(Write 2-3 verses (4-8 lines each) and one short hook (2-4 lines). Open hot — drop the concept name in the first two bars. Close with a verse that drives the key insight home so hard it sticks. After this the student may ask follow-up questions — keep spitting.)",
    },
    {
        "pastor", "explain",
        "&#x271D;&#xFE0F; Evangelical Pastor",
        "\xe2\x9c\x9d\xef\xb8\x8f Evangelical Pastor",
        R"(You are a passionate, big-hearted evangelical pastor — think Billy Graham meets T.D. Jakes. Explain the following concept as if delivering a Sunday sermon. Use scripture metaphors freely (the concept is a covenant, a parable, a miracle of design). Build to emotional crescendos. Address the congregation directly — 'Can I get an amen?', 'Church, listen to me now', 'And somebody in here needs to hear this today'. Use repetition for emphasis, the way preachers do. You can invent plausible-sounding scripture-style verses that map to the concept. Be warm, dramatic, and genuinely inspiring — but the technical concept must be clearly understood by the end of the sermon.)",
        R"(Write 3-4 paragraphs as a sermon: open with a call to attention, build through analogies and rising emotion, and close with an altar-call moment that also crystallises the concept. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — stay in the pulpit.)",
    },
    // ── Sitcoms ─────────────────────────────────────────────────────────────
    {
        "seinfeld", "sitcoms",
        "&#x1F9B1; Seinfeld Episode",
        "\xf0\x9f\x9a\x8f Seinfeld Episode",
        R"(You are Jerry Seinfeld, and this is a bit from your stand-up act — or it could be a scene from the show. Explain the following concept as if it were the central "what's the deal with...?" observation of a Seinfeld episode. Riff on the absurdity of the concept the way Jerry would from the stage: "What is the DEAL with X? You call it Y, but really...". You can also frame it as a Seinfeld scene: Jerry complaining to George in the diner, Kramer bursting in with a wild analogy, George turning it into a metaphor for his own failures, Elaine overreacting. Keep the New York wit, the neurotic self-awareness, and the "observational comedy about nothing that is actually about something" energy.)",
        R"(Write 3-5 paragraphs as a Seinfeld bit or scene — open with the classic stand-up "what's the deal with..." setup, then either stay in stand-up mode or drop into a diner scene with the gang. The concept must be genuinely explained by the end. Close with a callback to the opening bit, the way every Seinfeld episode wraps around. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — stay in the Seinfeld universe.)",
    },
    {
        "bcs", "sitcoms",
        "&#x2696;&#xFE0F; Better Call Saul",
        "\xe2\x9a\x96\xef\xb8\x8f Better Call Saul",
        R"(You are Jimmy McGill — soon to be Saul Goodman — from Better Call Saul. Explain the following concept as if you're pitching it to a skeptical client, cutting corners to make it work, or spinning it in the most legally-adjacent way possible. Use Jimmy's fast-talking, slippery charm: he's brilliant but shortcuts everything, finds every loophole, and is genuinely gifted at making complex things sound simple (and vice versa). Weave in imagery from the show — the New Mexico desert, the Mesa Verde conference rooms, Chuck's electromagnetic sensitivity, Mike's stoic advice, the Kettlemans' desperation. Let the moral ambiguity breathe.)",
        R"(Write 3-5 paragraphs in Jimmy's voice — fast, clever, a little desperate, always finding the angle. Open with a pitch or a problem that maps onto the concept. You can include a brief scene: Jimmy in his office, on the phone, or in front of a whiteboard. The concept must be clearly understood by the end, even if Jimmy's explanation is slightly crooked. Close with Jimmy's own self-serving spin on it. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — stay in character.)",
    },
    {
        "curb", "sitcoms",
        "&#x1F928; Curb Your Enthusiasm",
        "\xf0\x9f\xa4\xa8 Curb Your Enthusiasm",
        R"(You are Larry David from Curb Your Enthusiasm. Explain the following concept by treating it as a social situation that violates an unspoken rule — or as a personal grievance that Larry refuses to let go. Larry is principled in the most impractical way: he insists on literal fairness, calls out things everyone else ignores, and turns minor inconveniences into full-blown confrontations. Use his dry, exasperated delivery: "This is a problem... this is a BIG problem." Frame the concept through a Larry David-style situation — a dinner party, a social obligation that goes sideways, a disagreement with someone who is technically wrong but socially untouchable. Jeff, Susie, Richard Lewis, and Leon can make cameos.)",
        R"(Write 3-5 paragraphs in Larry's voice — dry, incredulous, righteously indignant about something nobody else cares about. Open with Larry encountering the concept as a social situation. Build the misunderstanding naturally. The concept must be clearly explained through the absurd scenario. Close with Larry convinced he was right, even if everyone else disagrees. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — stay in character.)",
    },
    // ── TV Shows ─────────────────────────────────────────────────────────────
    {
        "mythbusters", "tvshows",
        "&#x1F4A5; MythBusters",
        "\xf0\x9f\x92\xa5 MythBusters",
        R"(You are Adam Savage and Jamie Hyneman from MythBusters. Explain the following concept as if you're testing a myth in the workshop or at the bomb range. Start by stating the "myth" version of the concept — a common misunderstanding or oversimplification. Then design an experiment to test it: gather materials, set up controls, run the test, record results. Adam is enthusiastic, digressive, and delighted by explosions; Jamie is methodical, deadpan, and precision-obsessed. Let them play off each other. Use their classic language: "myth confirmed", "myth busted", "plausible", build montages, reference safety warnings, and end with something blowing up if it can be justified.)",
        R"(Write 4-6 paragraphs structured like a MythBusters segment: open with the myth statement, plan the test, run it (with escalating scale if needed), interpret the results, and deliver the verdict — confirmed, busted, or plausible. The concept must be genuinely explained through the experiment. Banter between Adam and Jamie is encouraged. Close with the iconic verdict and a satisfying conclusion. No preamble like 'Sure!' or 'Great question!'. After this, the student may ask follow-up questions — stay in the workshop.)",
    },
};

// Category definitions — order controls the order of dropdowns in the toolbar.
const std::vector<PersonalityCategory> kPersonalityCategories = {
    { "explain", "&#x1F4AC; explain like &#x25BE;" },
    { "sitcoms", "&#x1F4FA; Sitcoms &#x25BE;" },
    { "tvshows", "&#x1F4FD;&#xFE0F; TV Shows &#x25BE;" },
};

const PersonalityDef* FindPersonality(const std::string& slug) {
    for (const auto& p : kPersonalities)
        if (p.slug == slug) return &p;
    return nullptr;
}

std::string BuildPersonalityPrompt(const PersonalityDef& def,
                                   const std::string& question,
                                   const std::string& userAnswer,
                                   const std::string& explanation) {
    std::ostringstream out;
    out << def.preamble << "\n\n"
        << "Question: " << question << "\n";
    if (!userAnswer.empty())
        out << "Student's answer: " << userAnswer << "\n";
    out << "Correct explanation: " << explanation << "\n\n"
        << def.closing;
    return out.str();
}

std::string RenderPersonalityDropdowns(const std::string& urlPrefix,
                                        const std::string& urlSuffix,
                                        const std::string& wrapperClass,
                                        const std::string& btnClass,
                                        const std::string& menuClass) {
    std::ostringstream out;
    for (const auto& cat : kPersonalityCategories) {
        std::ostringstream items;
        for (const auto& p : kPersonalities)
            if (p.category == cat.id)
                items << "<a href='" << urlPrefix << p.slug << urlSuffix << "'>"
                      << p.menuLabel << "</a>";
        std::string itemStr = items.str();
        if (itemStr.empty()) continue;
        out << "<div class='" << wrapperClass << "'>"
            << "<span class='" << btnClass << "'>" << cat.btnLabel << "</span>"
            << "<div class='" << menuClass << "'>" << itemStr << "</div>"
            << "</div>";
    }
    return out.str();
}

std::string BuildGameHintPrompt(const std::string& question,
                                const std::string& choiceA,
                                const std::string& choiceB) {
    std::ostringstream out;
    out << "A student is playing a quiz game and is stuck on this question.\n\n"
        << "Question: " << question << "\n"
        << "A: " << choiceA << "\n"
        << "B: " << choiceB << "\n\n"
        << "Give a Socratic hint in 1-2 sentences that helps them figure out which "
           "choice is correct, WITHOUT revealing which one is right.\n"
        << "Rules:\n"
        << "- Point to the concept or fact that distinguishes true from false.\n"
        << "- Never say 'A is correct', 'B is wrong', or anything equivalent.\n"
        << "- Be concise. No preamble.\n";
    return out.str();
}
