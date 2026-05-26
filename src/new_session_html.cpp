#include "new_session_html.h"
#include "html_template.h"
#include "markdown.h"
#include <algorithm>
#include <sstream>

// ---------------------------------------------------------------------------
// Focus-area serialisation helpers
// ---------------------------------------------------------------------------

std::string SerializeFocusAreas(const std::vector<FocusArea>& areas) {
    std::string out;
    for (const auto& a : areas) {
        if (!out.empty()) out += "|";
        out += std::to_string(a.stars) + "@@" + a.text;
    }
    return out;
}

std::vector<FocusArea> DeserializeFocusAreas(const std::string& s) {
    std::vector<FocusArea> result;
    if (s.empty()) return result;
    std::string item;
    std::istringstream ss(s);
    while (std::getline(ss, item, '|')) {
        auto sep = item.find("@@");
        if (sep == std::string::npos) continue;
        int stars = std::stoi(item.substr(0, sep));
        std::string text = item.substr(sep + 2);
        if (!text.empty())
            result.push_back({text, std::max(1, std::min(5, stars))});
    }
    return result;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string BuildCSS() {
    return R"CSS(<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
  font-size:14px;line-height:1.5;color:var(--text);background:var(--bg);
  padding:20px 24px;max-width:860px;margin:0 auto}
label{font-weight:600;display:block;margin-bottom:4px;color:var(--text)}
.ns-label{font-weight:600;margin-bottom:4px;color:var(--text)}
input[type=text],input[type=number],input[type=password],textarea,select{
  width:100%;padding:6px 10px;border:1px solid var(--border);border-radius:4px;
  background:var(--surface);color:var(--text);font-family:inherit;font-size:1em}
input[type=text]:focus,input[type=number]:focus,input[type=password]:focus,
textarea:focus,select:focus{outline:2px solid var(--link);outline-offset:-1px}
.ns-row{margin-bottom:14px}
.ns-hrow{display:flex;align-items:center;gap:8px;margin-bottom:6px}
.ns-hrow label{margin-bottom:0}
.ns-btn{padding:5px 12px;border:1px solid var(--border);border-radius:4px;
  background:var(--surface);color:var(--text);cursor:pointer;font-size:.85em}
.ns-btn:hover{background:var(--link);color:#fff;border-color:var(--link)}
.ns-start-btn{padding:10px 28px;background:var(--link);color:#fff;
  border:none;border-radius:6px;font-size:1.1em;font-weight:700;cursor:pointer;
  margin-top:6px}
.ns-start-btn:hover{background:var(--link-hover)}
.ns-status{margin-top:10px;font-size:.9em;color:var(--text-muted);min-height:1.4em}
.ns-status:not(:empty){color:var(--text)}
.ns-fa-row{display:flex;align-items:center;gap:6px;margin-bottom:6px}
.ns-fa-text{flex:1}
.ns-fa-stars{width:80px}
.pers-body{display:flex;flex-wrap:wrap;gap:6px}
.pers-pill{background:var(--surface);border:1px solid var(--border);border-radius:12px;
  padding:2px 10px;font-size:.85em;color:var(--text)}
.ns-project-row{display:flex;align-items:center;gap:10px;
  margin-bottom:14px;padding:10px;background:var(--surface);
  border:1px solid var(--border);border-radius:6px}
.ns-project-name{font-weight:600;flex:1}
hr.ns-sep{border:none;border-top:1px solid var(--border);margin:16px 0}
</style>
)CSS";
}

static std::string BuildJS() {
    return R"JS(<script>
function nsAction(payload) {
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.nsAction)
        window.webkit.messageHandlers.nsAction.postMessage(payload);
}
function nsCollectForm() {
    var faStr = '';
    document.querySelectorAll('.ns-fa-row').forEach(function(row) {
        var txt = row.querySelector('.ns-fa-text').value.trim();
        var stars = row.querySelector('.ns-fa-stars').value;
        if (txt) { if (faStr) faStr += '|'; faStr += stars + '@@' + txt; }
    });
    var corpus = document.getElementById('ns-corpus');
    return JSON.stringify({
        action: 'start',
        topic: document.getElementById('ns-topic').value,
        instructions: document.getElementById('ns-instr').value,
        difficulty: document.getElementById('ns-difficulty').value,
        questions: document.getElementById('ns-questions').value,
        backend: document.getElementById('ns-backend').value,
        apiKey: (document.getElementById('ns-apikey')||{}).value||'',
        ollamaModel: (document.getElementById('ns-ollama-model')||{}).value||'',
        useCorpus: corpus ? corpus.checked.toString() : 'false',
        focusAreas: faStr,
        tidbitCount: document.getElementById('ns-tidbit-count').value
    });
}
function nsStart() { nsAction(nsCollectForm()); }
function nsUpdateBackend() {
    var b = document.getElementById('ns-backend').value;
    document.getElementById('ns-apikey-row').style.display = (b==='Anthropic API') ? '' : 'none';
    document.getElementById('ns-ollama-row').style.display = (b==='Ollama (local)') ? '' : 'none';
}
function nsAddFocusRow(text, stars) {
    var list = document.getElementById('ns-focus-list');
    var row = document.createElement('div');
    row.className = 'ns-fa-row';
    row.innerHTML =
        '<input type="text" class="ns-fa-text" value="' + (text||'') + '">' +
        '<select class="ns-fa-stars">' +
        '<option value="1">1 &#9733;</option>' +
        '<option value="2">2 &#9733;</option>' +
        '<option value="3"' + ((stars||3)==3?' selected':'') + '>3 &#9733;</option>' +
        '<option value="4"' + ((stars||3)==4?' selected':'') + '>4 &#9733;</option>' +
        '<option value="5"' + ((stars||3)==5?' selected':'') + '>5 &#9733;</option>' +
        '</select>' +
        '<button class="ns-btn" onclick="this.parentElement.remove()">&#x2715;</button>';
    var sel = row.querySelector('.ns-fa-stars');
    if (sel && (stars||3) >= 1 && (stars||3) <= 5) sel.value = String(stars||3);
    list.appendChild(row);
}
function nsSetTopic(v) { document.getElementById('ns-topic').value = v; }
function nsSetInstructions(v) { document.getElementById('ns-instr').value = v; }
function nsSetDifficulty(v) { document.getElementById('ns-difficulty').value = v; }
function nsSetQuestions(v) { document.getElementById('ns-questions').value = v; }
function nsSetFocusAreas(serialized) {
    var list = document.getElementById('ns-focus-list');
    list.innerHTML = '';
    if (!serialized) return;
    serialized.split('|').forEach(function(item) {
        var sep = item.indexOf('@@');
        if (sep < 0) return;
        var stars = parseInt(item.substring(0, sep));
        var txt = item.substring(sep + 2);
        if (txt) nsAddFocusRow(txt, stars);
    });
}
function nsSetStatus(msg) {
    var el = document.getElementById('ns-status');
    if (el) el.textContent = msg;
}
function nsSetOllamaModels(models) {
    var sel = document.getElementById('ns-ollama-model');
    if (!sel) return;
    var cur = sel.value;
    sel.innerHTML = '';
    models.forEach(function(m) {
        var opt = document.createElement('option');
        opt.value = m; opt.textContent = m;
        sel.appendChild(opt);
    });
    if (cur) sel.value = cur;
}
</script>
)JS";
}

// Build the project row section
static std::string BuildProjectRow(const NewSessionFormState& s) {
    std::string name = s.projectName.empty()
        ? "(none &mdash; activate a project first)"
        : EscapeHTML(s.projectName);

    return "<div class='ns-project-row'>"
           "<span class='ns-label'>Project:</span>"
           "<span class='ns-project-name'>" + name + "</span>"
           "<button class='ns-btn' onclick=\"nsAction(JSON.stringify({action:'open-context'}))\""
           ">Edit context.md</button>"
           "</div>\n";
}

// Build a star <select> with the given value pre-selected
static std::string BuildStarsSelect(int stars) {
    std::string out = "<select class='ns-fa-stars'>";
    for (int i = 1; i <= 5; ++i) {
        out += "<option value='" + std::to_string(i) + "'";
        if (i == stars) out += " selected";
        out += ">" + std::to_string(i) + " &#9733;</option>";
    }
    out += "</select>";
    return out;
}

// Build focus area rows
static std::string BuildFocusAreas(const std::vector<FocusArea>& areas) {
    std::string out;
    for (const auto& a : areas) {
        out += "<div class='ns-fa-row'>"
               "<input type='text' class='ns-fa-text' value='" + EscapeHTML(a.text) + "'>"
               + BuildStarsSelect(a.stars) +
               "<button class='ns-btn' onclick='this.parentElement.remove()'>&#x2715;</button>"
               "</div>\n";
    }
    return out;
}

// Read-only pill list of active personas (managed in Personas tab).
static std::string BuildPersonalitySection(const std::vector<std::string>& selected) {
    if (selected.empty())
        return "<div style='color:var(--text-muted);font-size:.85em'>"
               "None active &mdash; check some in the <b>Personas</b> tab."
               "</div>";
    std::string out = "<div class='pers-body' style='padding:4px 0'>";
    for (const auto& name : selected)
        out += "<span class='pers-pill'>" + EscapeHTML(name) + "</span>";
    out += "</div>";
    return out;
}

// Build difficulty <select>
static std::string BuildDifficultySelect(const std::string& current) {
    const char* opts[] = {"mixed", "easy", "medium", "hard"};
    std::string out = "<select id='ns-difficulty'>";
    for (auto* o : opts) {
        out += std::string("<option value='") + o + "'";
        if (current == o) out += " selected";
        out += ">";
        out += o;
        out += "</option>";
    }
    out += "</select>";
    return out;
}

// Build backend <select>
static std::string BuildBackendSelect(const std::string& current) {
    const char* opts[] = {"claude -p", "Anthropic API", "Ollama (local)", "Clipboard (manual)"};
    std::string out = "<select id='ns-backend' onchange='nsUpdateBackend()'>";
    for (auto* o : opts) {
        out += std::string("<option value='") + EscapeHTML(o) + "'";
        if (current == o) out += " selected";
        out += ">" + EscapeHTML(o) + "</option>";
    }
    out += "</select>";
    return out;
}

// Build ollama model <select>/<input>
static std::string BuildOllamaModelInput(const std::string& current,
                                          const std::vector<std::string>& models)
{
    if (!models.empty()) {
        std::string out = "<select id='ns-ollama-model'>";
        for (const auto& m : models) {
            out += "<option value='" + EscapeHTML(m) + "'";
            if (m == current) out += " selected";
            out += ">" + EscapeHTML(m) + "</option>";
        }
        out += "</select>";
        return out;
    }
    return "<input type='text' id='ns-ollama-model' value='" + EscapeHTML(current) + "'>";
}

// ---------------------------------------------------------------------------
// Main builder
// ---------------------------------------------------------------------------

std::string BuildNewSessionHTML(const NewSessionFormState& s) {
    bool apikeyVisible = (s.backend == "Anthropic API");
    bool ollamaVisible = (s.backend == "Ollama (local)");

    std::string apikeyStyle  = apikeyVisible  ? "" : "display:none";
    std::string ollamaStyle  = ollamaVisible  ? "" : "display:none";
    std::string corpusStyle  = s.hasCorpus    ? "" : "display:none";

    std::string focusRows = BuildFocusAreas(s.focusAreas);
    std::string persSection = BuildPersonalitySection(s.selectedPersonalities);

    std::string form =
        BuildProjectRow(s) +

        // Topic
        "<div class='ns-row'>"
        "<label>Topic (short name for Review tab):</label>"
        "<input type='text' id='ns-topic'"
        " placeholder='e.g. \"C++ memory model\" or \"AWS IAM\" or \"Spanish verbs\"'"
        " value='" + EscapeHTML(s.topic) + "'>"
        "</div>\n"

        // Instructions
        "<div class='ns-row'>"
        "<label>What to focus on (injected into every prompt):</label>"
        "<textarea id='ns-instr' style='height:80px;resize:vertical'"
        " placeholder='e.g. \"Focus on move semantics, RAII, and smart pointers.\"'>"
        + EscapeHTML(s.instructions) +
        "</textarea>"
        "</div>\n"

        // Focus areas
        "<div class='ns-row'>"
        "<div class='ns-hrow'>"
        "<label style='margin-bottom:0'>Focus areas &mdash; sub-topics with star priority:</label>"
        "<button class='ns-btn'"
        " onclick=\"nsAction(JSON.stringify({action:'reset-weights'}))\">&#x21BA; Reset weights</button>"
        "</div>"
        "<div id='ns-focus-list'>" + focusRows + "</div>"
        "<button class='ns-btn' style='margin-top:4px'"
        " onclick='nsAddFocusRow(\"\",3)'>+ Add area</button>"
        "</div>\n"

        // Guest commentators
        "<div class='ns-row'>"
        "<div class='ns-hrow'>"
        "<label style='margin-bottom:0'>Guest commentators:</label>"
        "<label style='margin-bottom:0;font-weight:normal'>Tidbits per turn:</label>"
        "<input type='number' id='ns-tidbit-count' min='1' max='10'"
        " value='" + std::to_string(s.tidbitCount) + "' style='width:60px'>"
        "</div>"
        + persSection +
        "</div>\n"

        "<hr class='ns-sep'>\n"

        // Difficulty + Questions
        "<div class='ns-row'>"
        "<div class='ns-hrow' style='flex-wrap:wrap;gap:16px'>"
        "<div style='display:flex;align-items:center;gap:6px'>"
        "<label style='margin-bottom:0'>Difficulty:</label>"
        + BuildDifficultySelect(s.difficulty) +
        "</div>"
        "<div style='display:flex;align-items:center;gap:6px'>"
        "<label style='margin-bottom:0'>Questions:</label>"
        "<input type='number' id='ns-questions' min='1' max='50' style='width:70px'"
        " value='" + std::to_string(s.questions) + "'>"
        "</div>"
        "</div>"
        "</div>\n"

        "<hr class='ns-sep'>\n"

        // LLM backend
        "<div class='ns-row'>"
        "<div class='ns-hrow'>"
        "<label style='margin-bottom:0'>LLM backend:</label>"
        + BuildBackendSelect(s.backend) +
        "</div>"
        // API key row
        "<div id='ns-apikey-row' style='" + apikeyStyle + "'>"
        "<div class='ns-hrow' style='margin-top:6px'>"
        "<label style='margin-bottom:0'>API key:</label>"
        "<input type='password' id='ns-apikey' style='max-width:320px'"
        " value='" + EscapeHTML(s.apiKey) + "'>"
        "</div>"
        "</div>\n"
        // Ollama row
        "<div id='ns-ollama-row' style='" + ollamaStyle + "'>"
        "<div class='ns-hrow' style='margin-top:6px'>"
        "<label style='margin-bottom:0'>Ollama model:</label>"
        + BuildOllamaModelInput(s.ollamaModel, s.ollamaModels) +
        "<button class='ns-btn'"
        " onclick=\"nsAction(JSON.stringify({action:'refresh-ollama'}))\">Refresh</button>"
        "</div>"
        "</div>\n"
        "</div>\n"

        // Corpus toggle
        "<div id='ns-corpus-row' style='" + corpusStyle + "'>"
        "<label>"
        "<input type='checkbox' id='ns-corpus'" + (s.useCorpus ? " checked" : "") + ">"
        " Use corpus for context (RAG)</label>"
        "</div>\n"

        // Start button
        "<div class='ns-row'>"
        "<button id='ns-start-btn' class='ns-start-btn' onclick='nsStart()'"
        ">&#x25BA; Start Session</button>"
        "</div>\n"

        // Status
        "<div id='ns-status' class='ns-status'>" + EscapeHTML(s.statusMsg) + "</div>\n";

    std::string css = BuildCSS();
    std::string js  = BuildJS();

    return BuildHTML(css + js + form, "New Session", s.darkMode);
}
