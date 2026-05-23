#include "saved_panel.h"
#include "saved_convos.h"
#include "exam_prompt.h"
#include "html_template.h"
#include "logger.h"
#include "game_data.h"
#include "llm_response.h"
#include "turn_chat.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <wx/app.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace fs = std::filesystem;

enum { ID_SP_EXPORT = wxID_HIGHEST + 700 };

wxBEGIN_EVENT_TABLE(SavedPanel, wxPanel)
    EVT_BUTTON(ID_SP_EXPORT, SavedPanel::OnExport)
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, SavedPanel::OnWebViewNav)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
SavedPanel::SavedPanel(wxWindow* parent, bool darkMode)
    : wxPanel(parent), m_darkMode(darkMode)
{
    auto* outerSizer = new wxBoxSizer(wxVERTICAL);

    m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxSP_3DSASH | wxSP_LIVE_UPDATE);
    m_splitter->SetMinimumPaneSize(220);

    m_leftPanel = new wxPanel(m_splitter, wxID_ANY);
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    toolbar->AddStretchSpacer();
    m_exportBtn = new wxButton(m_leftPanel, ID_SP_EXPORT, "Export as Markdown\xe2\x80\xa6");
    toolbar->Add(m_exportBtn, 0, wxALL, 4);
    leftSizer->Add(toolbar, 0, wxEXPAND);

    m_webView = wxWebView::New(m_leftPanel, wxID_ANY, "about:blank");
    leftSizer->Add(m_webView, 1, wxEXPAND);
    m_leftPanel->SetSizer(leftSizer);

    m_chatPanel = new TurnChatPanel(m_splitter, darkMode,
        [this]() {
            if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
            m_chatOpen   = false;
            m_currentIdx = -1;
            m_webView->RunScript(
                "document.querySelectorAll('.saved-entry.active')"
                ".forEach(function(e){e.classList.remove('active');});");
        },
        [this]() { Render(); });

    m_splitter->Initialize(m_leftPanel);
    outerSizer->Add(m_splitter, 1, wxEXPAND);
    SetSizer(outerSizer);
}

// ---------------------------------------------------------------------------
void SavedPanel::SyncProject(const std::string& projectDir,
                              const LLMConfig&   llmCfg,
                              bool               darkMode) {
    m_projectDir = projectDir;
    m_llmCfg     = llmCfg;
    m_darkMode   = darkMode;
    m_chatOpen   = false;
    m_currentIdx = -1;
    if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
    m_chatPanel->Reset();
    m_chatPanel->SetDarkMode(darkMode);
    Render();
}

void SavedPanel::SetDarkMode(bool dark) {
    m_darkMode = dark;
    m_chatPanel->SetDarkMode(dark);
    Render();
}

void SavedPanel::Refresh() { Render(); }

// ---------------------------------------------------------------------------
void SavedPanel::Render() {
    auto convos = LoadSavedConvos(m_projectDir);
    std::string body = BuildSavedConvosHTML(convos);
    std::string html = BuildHTML(body, "Saved Convos", m_darkMode);
    m_webView->SetPage(wxString::FromUTF8(html), "");
    m_exportBtn->Enable(!convos.empty());

    // Re-apply active class if side panel is open
    if (m_chatOpen && m_currentIdx >= 0) {
        std::string si = std::to_string(m_currentIdx);
        m_webView->RunScript(
            "var e=document.getElementById('saved-entry-" + si + "');"
            "if(e)e.classList.add('active');");
    }
}

// ---------------------------------------------------------------------------
void SavedPanel::OpenChatFor(int fileIdx,
                              const std::string& starterMsg,
                              const std::string& starterDisplayQ) {
    if (m_projectDir.empty()) return;

    // Toggle closed if the user clicks back on the entry that is already open.
    if (m_chatOpen && m_currentIdx == fileIdx) {
        if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
        m_chatOpen   = false;
        m_currentIdx = -1;
        m_webView->RunScript(
            "document.querySelectorAll('.saved-entry.active')"
            ".forEach(function(e){e.classList.remove('active');});");
        return;
    }

    auto convos = LoadSavedConvos(m_projectDir);
    if (fileIdx < 0 || fileIdx >= (int)convos.size()) return;
    const auto& c = convos[fileIdx];

    QuestionTurn synth;
    synth.question    = c.question;
    synth.explanation = c.explanation;
    synth.score       = Score::Skipped;  // neutral — shows "—" in side panel

    std::string discussFile = m_projectDir + "/saved_discuss.md";
    m_chatPanel->OpenTurn(synth, fileIdx, discussFile, m_llmCfg,
                          starterMsg, starterDisplayQ);

    if (!m_splitter->IsSplit()) {
        int w = m_splitter->GetClientSize().GetWidth();
        m_splitter->SplitVertically(m_leftPanel, m_chatPanel, w * 6 / 10);
    }
    m_chatOpen   = true;
    m_currentIdx = fileIdx;

    std::string si = std::to_string(fileIdx);
    m_webView->RunScript(
        "document.querySelectorAll('.saved-entry.active')"
        ".forEach(function(e){e.classList.remove('active');});"
        "var e=document.getElementById('saved-entry-" + si + "');"
        "if(e)e.classList.add('active');");
}

// ---------------------------------------------------------------------------
void SavedPanel::LaunchGame(int fileIdx,
                             const std::string& question,
                             const std::string& explanation) {
    std::string si = std::to_string(fileIdx);
    m_webView->RunScript(
        "var g=document.querySelector(\"a[href='testtaker://saved-game/" + si + "']\");"
        "if(g){g.textContent='\xe2\x8f\xb3 generating\xe2\x80\xa6';"
        "g.style.pointerEvents='none';}");

    std::string projectDir  = m_projectDir;
    LLMConfig   llmCfg     = m_llmCfg;
    auto onRefresh = [this]() { Render(); };

    std::thread([=]() {
        LLMResult r1, r2;
        {
            auto t1 = std::thread([&]() {
                r1 = InvokeLLM(BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
            });
            auto t2 = std::thread([&]() {
                r2 = InvokeLLM(BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
            });
            t1.join(); t2.join();
        }

        wxTheApp->CallAfter([=]() {
            m_webView->RunScript(
                "var g=document.querySelector(\"a[href='testtaker://saved-game/" + si + "']\");"
                "if(g){g.textContent='\xf0\x9f\x8e\xae game';g.style.pointerEvents='';}");

            auto b1 = ParseMultipleGameChoices(r1.text);
            auto b2 = ParseMultipleGameChoices(r2.text);
            if (b1.empty() && b2.empty()) {
                Logger::get().log("SavedPanel: game series parse failed");
                return;
            }

            auto toGameData = [&](const std::vector<GameQuestionBlock>& bl) {
                std::vector<GameData> out;
                for (auto& b : bl) {
                    bool cia = (std::rand() % 2 == 0);
                    GameData gd;
                    gd.question   = b.question.empty() ? question : b.question;
                    gd.choiceA    = cia ? b.correct : b.wrong;
                    gd.choiceB    = cia ? b.wrong   : b.correct;
                    gd.correctIsA = cia;
                    out.push_back(gd);
                }
                return out;
            };

            auto all = toGameData(b1);
            auto m2  = toGameData(b2);
            all.insert(all.end(), m2.begin(), m2.end());

            wxString tmpPath = wxFileName::CreateTempFileName("tt-game") + ".dat";
            if (!WriteGameFiles(tmpPath.ToStdString(), all)) {
                Logger::get().log("SavedPanel: could not write game data");
                return;
            }

            wxFileName exeDir(wxStandardPaths::Get().GetExecutablePath());
            wxString gameBin = exeDir.GetPath() + "/test-taker-game";
            if (!wxFileExists(gameBin))
                gameBin = exeDir.GetPath() + "/../test-taker-game";
            if (!wxFileExists(gameBin)) {
                Logger::get().log("SavedPanel: test-taker-game not found");
                return;
            }

            wxString cmd = "\"" + gameBin + "\" \"" + tmpPath + "\"";
            wxExecute(cmd, wxEXEC_ASYNC);
            Logger::get().log("SavedPanel: launched game with "
                              + std::to_string(all.size()) + " questions");

            std::string tmpStr   = tmpPath.ToStdString();
            std::string wantFile = tmpStr + ".want";
            std::string saveFile = tmpStr + ".save";
            std::string hintReq  = tmpStr + ".hintreq";
            std::string hintResp = tmpStr + ".hintresp";

            std::thread([=]() {
                for (;;) {
                    if (fs::exists(saveFile)) {
                        std::string q, a;
                        { std::ifstream sf(saveFile);
                          std::string ln;
                          while (std::getline(sf, ln)) {
                              if (ln.rfind("Q: ", 0) == 0) q = ln.substr(3);
                              if (ln.rfind("A: ", 0) == 0) a = ln.substr(3);
                          } }
                        try { fs::remove(saveFile); } catch (...) {}
                        if (!q.empty() && !a.empty() && !projectDir.empty()) {
                            char buf[32]; time_t now = time(nullptr);
                            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M",
                                          std::localtime(&now));
                            AppendSavedConvo(projectDir, q,
                                             "Correct answer: " + a, buf, true);
                            wxTheApp->CallAfter(onRefresh);
                        }
                    }
                    if (fs::exists(hintReq)) {
                        std::string hq, ha, hb;
                        { std::ifstream hf(hintReq);
                          std::string ln;
                          while (std::getline(hf, ln)) {
                              if (ln.rfind("Q: ", 0) == 0) hq = ln.substr(3);
                              if (ln.rfind("A: ", 0) == 0) ha = ln.substr(3);
                              if (ln.rfind("B: ", 0) == 0) hb = ln.substr(3);
                          } }
                        try { fs::remove(hintReq); } catch (...) {}
                        if (!hq.empty()) {
                            LLMResult hr = InvokeLLM(
                                BuildGameHintPrompt(hq, ha, hb), llmCfg);
                            if (hr.ok && !hr.text.empty()) {
                                std::ofstream hrf(hintResp);
                                hrf << hr.text;
                            }
                        }
                    }
                    if (fs::exists(wantFile)) {
                        try { fs::remove(wantFile); } catch (...) {}
                        LLMResult r = InvokeLLM(
                            BuildGameSeriesPrompt(question, explanation, 4), llmCfg);
                        if (r.ok) {
                            auto bl = ParseMultipleGameChoices(r.text);
                            std::vector<GameData> batch;
                            for (auto& b : bl) {
                                bool cia = (std::rand() % 2 == 0);
                                GameData gd;
                                gd.question   = b.question.empty() ? question : b.question;
                                gd.choiceA    = cia ? b.correct : b.wrong;
                                gd.choiceB    = cia ? b.wrong   : b.correct;
                                gd.correctIsA = cia;
                                batch.push_back(gd);
                            }
                            if (!batch.empty()) AppendGameFiles(tmpStr, batch);
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            }).detach();
        });
    }).detach();
}

// ---------------------------------------------------------------------------
void SavedPanel::OnExport(wxCommandEvent&) {
    if (m_projectDir.empty()) return;

    wxFileDialog dlg(this, "Export Saved Convos", "", "saved_convos.md",
                     "Markdown files (*.md)|*.md",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;

    std::string src  = m_projectDir + "/saved_convos.md";
    std::string dest = dlg.GetPath().ToStdString();

    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dest, std::ios::binary);
    if (!in || !out) {
        wxMessageBox("Could not export saved convos.", "Export Failed", wxOK | wxICON_ERROR);
        return;
    }
    out << in.rdbuf();
}

// ---------------------------------------------------------------------------
void SavedPanel::OnWebViewNav(wxWebViewEvent& evt) {
    wxString url = evt.GetURL();

    if (url.StartsWith("testtaker://delete-saved/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(25).ToLong(&idx);
        DeleteSavedConvo(m_projectDir, (int)idx);
        if (m_chatOpen && m_currentIdx == (int)idx) {
            if (m_splitter->IsSplit()) m_splitter->Unsplit(m_chatPanel);
            m_chatOpen   = false;
            m_currentIdx = -1;
            m_chatPanel->Reset();
        }
        Render();
        return;
    }

    if (url.StartsWith("testtaker://saved-discuss/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(26).ToLong(&idx);
        OpenChatFor((int)idx);
        return;
    }

    if (url.StartsWith("testtaker://saved-explain/")) {
        evt.Veto();
        wxString rest = url.Mid(26);
        int slash = rest.Find('/');
        if (slash < 0) return;
        std::string slug = rest.Left(slash).ToStdString();
        long idx = -1;
        rest.Mid(slash + 1).ToLong(&idx);
        const PersonalityDef* def = FindPersonality(slug);
        if (!def) return;
        auto convos = LoadSavedConvos(m_projectDir);
        if (idx < 0 || idx >= (long)convos.size()) return;
        const auto& c = convos[(int)idx];
        std::string starter = BuildPersonalityPrompt(*def, c.question, "", c.explanation);
        OpenChatFor((int)idx, starter, def->displayQ);
        return;
    }

    if (url.StartsWith("testtaker://saved-learnmore/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(28).ToLong(&idx);
        auto convos = LoadSavedConvos(m_projectDir);
        if (idx < 0 || idx >= (long)convos.size()) return;
        const auto& c = convos[(int)idx];
        std::string starter = BuildLearnMorePrompt(c.question, c.explanation);
        OpenChatFor((int)idx, starter, "\xf0\x9f\x93\x96 Deep dive: learn more");
        return;
    }

    if (url.StartsWith("testtaker://saved-game/")) {
        evt.Veto();
        long idx = -1;
        url.Mid(23).ToLong(&idx);
        auto convos = LoadSavedConvos(m_projectDir);
        if (idx < 0 || idx >= (long)convos.size()) return;
        const auto& c = convos[(int)idx];
        if (c.explanation.empty()) return;
        LaunchGame((int)idx, c.question, c.explanation);
        return;
    }

    evt.Skip();
}
