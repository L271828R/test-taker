#include "mdviewer.h"
#include "create_panel.h"
#include "edit_panel.h"
#include "project_panel.h"
#include "markdown.h"
#include "html_template.h"
#include "inspector.h"
#include "chat_frame.h"
#include "creator.h"
#include "config.h"
#include "notes.h"
#include <wx/notebook.h>
#include <wx/webview.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/config.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/textdlg.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

// ---------------------------------------------------------------------------
// Event table
// ---------------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(MDViewerFrame, wxFrame)
    EVT_MENU(wxID_OPEN,      MDViewerFrame::OnOpen)
    EVT_MENU(ID_RELOAD,      MDViewerFrame::OnReload)
    EVT_MENU(wxID_COPY,      MDViewerFrame::OnCopy)
    EVT_MENU(wxID_SELECTALL, MDViewerFrame::OnSelectAll)
    EVT_MENU(wxID_PASTE,     MDViewerFrame::OnPasteView)
    EVT_MENU(wxID_FIND,      MDViewerFrame::OnFindOpen)
    EVT_MENU(ID_FIND_NEXT,   MDViewerFrame::OnFindNext)
    EVT_MENU(ID_FIND_PREV,   MDViewerFrame::OnFindPrev)
    EVT_MENU(ID_FIND_CLOSE,  MDViewerFrame::OnFindClose)
    EVT_MENU(ID_THEME_LIGHT, MDViewerFrame::OnThemeLight)
    EVT_MENU(ID_THEME_DARK,  MDViewerFrame::OnThemeDark)
    EVT_MENU(ID_VIEW_LOGS,   MDViewerFrame::OnViewLogs)
    EVT_MENU(ID_VIEW_DOC,    MDViewerFrame::OnViewDoc)
    EVT_MENU(ID_FONT_INCREASE, MDViewerFrame::OnFontIncrease)
    EVT_MENU(ID_FONT_DECREASE, MDViewerFrame::OnFontDecrease)
    EVT_MENU(ID_FONT_RESET,    MDViewerFrame::OnFontReset)
    EVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED(wxID_ANY, MDViewerFrame::OnScriptMessage)
    EVT_MENU(ID_SAVE_HTML,   MDViewerFrame::OnSaveHTML)
    EVT_MENU(wxID_CLOSE,     MDViewerFrame::OnExit)
    EVT_MENU(wxID_EXIT,      MDViewerFrame::OnExit)
    EVT_CLOSE(               MDViewerFrame::OnClose)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MDViewerFrame::MDViewerFrame(const wxString& filePath)
    : wxFrame(nullptr, wxID_ANY,
              filePath.empty() ? wxString("StoryTeller")
                               : wxString("StoryTeller — " + wxFileName(filePath).GetFullName()),
              wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE)
    , m_darkMode(false)
    , m_fontSizePercent(100)
{
    if (!filePath.empty()) {
        wxFileName fn(filePath);
        fn.MakeAbsolute();
        m_filePath = fn.GetFullPath();
    }

    wxConfig cfg("MDViewer");
    m_darkMode        = cfg.ReadBool("darkMode", false);
    m_fontSizePercent = (int)cfg.ReadLong("fontSizePercent", 100);

    // ── File menu ────────────────────────────────────────────────────────
    wxMenuBar* bar  = new wxMenuBar();
    wxMenu*    file = new wxMenu();
    file->Append(wxID_OPEN,  "&Open…\tCtrl+O");
    file->Append(ID_RELOAD,  "&Reload\tCtrl+R");
    file->Append(ID_SAVE_HTML, "Save &HTML…\tCtrl+Shift+S");
    file->AppendSeparator();
    file->Append(wxID_CLOSE, "&Close Window\tCtrl+W");
    file->Append(wxID_EXIT,  "E&xit\tCtrl+Q");
    bar->Append(file, "&File");

    // ── Edit menu ────────────────────────────────────────────────────────
    wxMenu* edit = new wxMenu();
    edit->Append(wxID_COPY,      "&Copy\tCtrl+C");
    edit->Append(wxID_SELECTALL, "Select &All\tCtrl+A");
    edit->AppendSeparator();
    edit->Append(wxID_PASTE,     "&Paste && Render\tCtrl+V");
    edit->AppendSeparator();
    edit->Append(wxID_FIND,      "&Find…\tCtrl+F");
    edit->Append(ID_FIND_NEXT,   "Find &Next\tCtrl+G");
    edit->Append(ID_FIND_PREV,   "Find &Previous\tCtrl+Shift+G");
    bar->Append(edit, "&Edit");

    // ── View menu ────────────────────────────────────────────────────────
    wxMenu* view = new wxMenu();
    view->AppendRadioItem(ID_THEME_LIGHT, "&Light Mode\tCtrl+Shift+L");
    view->AppendRadioItem(ID_THEME_DARK,  "&Dark Mode\tCtrl+Shift+D");
    view->Check(m_darkMode ? ID_THEME_DARK : ID_THEME_LIGHT, true);
    view->AppendSeparator();
    view->Append(ID_VIEW_LOGS, "View &Logs");
    view->Append(ID_VIEW_DOC,  "View &Document\tCtrl+Shift+V");
    view->AppendSeparator();
    view->Append(ID_FONT_INCREASE, "Increase Font Size\tCtrl++");
    view->Append(ID_FONT_DECREASE, "Decrease Font Size\tCtrl+-");
    view->Append(ID_FONT_RESET,    "Reset Font Size\tCtrl+0");
    bar->Append(view, "&View");

    SetMenuBar(bar);

    CreateStatusBar();
    SetStatusText("Loading…");

    // ── Notebook ─────────────────────────────────────────────────────────
    m_notebook = new wxNotebook(this, wxID_ANY);

    // ── View page: webview + floating find bar ────────────────────────────
    m_viewPage = new wxPanel(m_notebook, wxID_ANY);
    m_webView  = wxWebView::New(m_viewPage, wxID_ANY, "about:blank");
    m_webView->AddScriptMessageHandler("fontSizeChange");
    m_webView->AddScriptMessageHandler("clipboardCopy");
    m_webView->AddScriptMessageHandler("chat");
    m_webView->AddScriptMessageHandler("note");
    EnableWebInspector(m_webView);

    auto* viewSizer = new wxBoxSizer(wxVERTICAL);
    viewSizer->Add(m_webView, 1, wxEXPAND);
    m_viewPage->SetSizer(viewSizer);

    // Find bar lives inside the view page so it floats over the webview.
    m_findPanel = new wxPanel(m_viewPage, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxTAB_TRAVERSAL | wxBORDER_SIMPLE);
    auto* fs = new wxBoxSizer(wxHORIZONTAL);
    m_findCtrl = new wxTextCtrl(m_findPanel, wxID_ANY, wxEmptyString,
                                wxDefaultPosition, wxSize(180, -1), wxTE_PROCESS_ENTER);
    auto* prevBtn  = new wxButton(m_findPanel, ID_FIND_PREV, "‹",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* nextBtn  = new wxButton(m_findPanel, ID_FIND_NEXT, "›",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_findStatus   = new wxStaticText(m_findPanel, wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxSize(72, -1));
    auto* closeBtn = new wxButton(m_findPanel, ID_FIND_CLOSE, "×",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    fs->AddSpacer(6);
    fs->Add(m_findCtrl,   0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    fs->AddSpacer(4);
    fs->Add(prevBtn,      0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    fs->Add(nextBtn,      0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    fs->AddSpacer(6);
    fs->Add(m_findStatus, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    fs->AddSpacer(4);
    fs->Add(closeBtn,     0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    fs->AddSpacer(6);
    m_findPanel->SetSizer(fs);
    m_findPanel->Hide();

    closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowFindBar(false); });
    nextBtn->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&) { DoFind(true); });
    prevBtn->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&) { DoFind(false); });
    m_findCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        m_findTerm = wxEmptyString;
        m_webView->Find(wxEmptyString);
        DoFind(true);
    });
    m_findCtrl->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { DoFind(true); });
    m_findCtrl->Bind(wxEVT_KEY_DOWN,   [this](wxKeyEvent& evt) {
        if (evt.GetKeyCode() == WXK_ESCAPE) ShowFindBar(false);
        else evt.Skip();
    });

    // ── Projects page ─────────────────────────────────────────────────────
    m_projectPage = new ProjectPanel(m_notebook,
        [this](const std::string& path) { LoadFile(path); });
    m_notebook->AddPage(m_projectPage, "Projects");

    // ── Create page ───────────────────────────────────────────────────────
    m_createPage = new CreatePanel(m_notebook,
        [this](const std::string& path) {
            LoadFile(path);
            if (m_editPage) m_editPage->RefreshChapters();
        });
    m_notebook->AddPage(m_createPage, "Create");

    // ── Edit page ─────────────────────────────────────────────────────────
    m_editPage = new EditPanel(m_notebook,
        [this](const std::string& path) { LoadFile(path); });
    m_notebook->AddPage(m_editPage, "Edit");

    // ── View page (last) ──────────────────────────────────────────────────
    m_notebook->AddPage(m_viewPage, "View");
    m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& evt) {
        evt.Skip();
        wxWindow* page = m_notebook->GetPage(evt.GetSelection());
        if (m_editPage && page == m_editPage)
            m_editPage->RefreshChapters();
        else if (m_projectPage && page == m_projectPage)
            m_projectPage->RefreshProjects();
    });

    // ── Frame layout ─────────────────────────────────────────────────────
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_notebook, 1, wxEXPAND);
    SetSizer(sizer);

    // Size to fit the display, with a comfortable margin so the title bar
    // is never pushed off the top of the screen.
    wxSize display = wxGetDisplaySize();
    int w = std::min(1280, display.x - 40);
    int h = std::min(860,  display.y - 80);
    SetSize(w, h);
    Centre();

    m_viewPage->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        evt.Skip();
        CallAfter([this]() { PositionFindBar(); });
    });

    CallAfter([this]() { LoadAndRender(); });
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------
std::string MDViewerFrame::ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Logger::get().log("ReadFile FAILED: " + path);
        return "";
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    Logger::get().log("ReadFile OK: " + path + "  (" + std::to_string(content.size()) + " bytes)");
    return content;
}

// ---------------------------------------------------------------------------
// Dispatch: .html → load URL directly; .md → render markdown
// ---------------------------------------------------------------------------
void MDViewerFrame::LoadAndRender() {
    if (m_filePath.empty()) {
        std::string body = RenderMarkdown(
            "# StoryTeller\n\n"
            "Open a file with **File → Open** (Ctrl+O) or paste markdown with **Ctrl+V**.\n");
        std::string html = BuildHTML(body, "StoryTeller", m_darkMode, m_fontSizePercent);
        m_webView->SetPage(wxString::FromUTF8(html), "");
        SetStatusText("Ready");
        return;
    }

    wxString ext = wxFileName(m_filePath).GetExt().Lower();
    Logger::get().log("LoadAndRender: " + m_filePath.ToStdString() + "  ext=" + ext.ToStdString());

    if (ext == "html" || ext == "htm") {
        wxString url = "file://" + m_filePath;
#ifdef __WXMSW__
        url.Replace("/", "\\");
        url = "file:///" + m_filePath;
#endif
        m_webView->LoadURL(url);
        SetStatusText("Loaded HTML: " + m_filePath);
        return;
    }

    std::string raw = ReadFile(m_filePath.ToStdString());

    // Auto-stamp chapter markers so the chat buttons work on legacy documents.
    // Only runs once: if headings exist but no <!-- ch: --> markers are present.
    if (raw.find("<!-- ch:") == std::string::npos &&
        (raw.find("\n## ") != std::string::npos || raw.rfind("## ", 0) == 0)) {
        auto stamped = StampChapters(raw, 0);
        if (stamped.count > 0) {
            std::ofstream f(m_filePath.ToStdString());
            if (f) {
                f << stamped.text;
                raw = stamped.text;
                Logger::get().log("Auto-stamped " + std::to_string(stamped.count)
                                  + " chapters in: " + m_filePath.ToStdString());
            }
        }
    }

    std::string body = RenderMarkdown(raw);

    // Inject note spans into the rendered HTML body (after rendering, so that
    // ProcessInline has already escaped < > — we match the escaped form).
    {
        std::string projDir = CurrentProjectDir();
        if (!projDir.empty()) {
            auto notes = LoadNotes(projDir);
            if (!notes.empty()) {
                std::map<int,std::string> noteTexts;
                for (const auto& n : notes)
                    noteTexts[n.id] = n.text;
                body = InjectNoteSpans(body, noteTexts);
            }
        }
    }

    std::string title = wxFileName(m_filePath).GetFullName().ToStdString();
    std::string html  = BuildHTML(body, title, m_darkMode, m_fontSizePercent);

    wxString baseURL = "file://" + wxFileName(m_filePath).GetPath(wxPATH_GET_SEPARATOR);
    m_webView->SetPage(wxString::FromUTF8(html), baseURL);
    SetStatusText("Rendered: " + m_filePath);
}

// ---------------------------------------------------------------------------
// Menu handlers
// ---------------------------------------------------------------------------
void MDViewerFrame::OnViewLogs(wxCommandEvent&) {
    const std::string logPath = std::string(getenv("HOME") ?: "") + "/Library/Logs/StoryTeller/story-teller.log";
    std::string html = BuildLogsHTML(ReadFile(logPath), logPath, m_darkMode);
    m_webView->SetPage(wxString::FromUTF8(html), "");
    SetStatusText("Viewing logs — use View > View Document to return");
}

void MDViewerFrame::LoadFile(const std::string& path) {
    m_filePath = wxString::FromUTF8(path);
    SetTitle("StoryTeller — " + wxFileName(m_filePath).GetFullName());
    if (m_editPage) m_editPage->RefreshChapters();
    if (m_createPage) m_createPage->SyncProject();
    // View is the last tab (index 3: Projects=0, Create=1, Edit=2, View=3).
    m_notebook->SetSelection(3);
    LoadAndRender();
}

void MDViewerFrame::OnViewDoc(wxCommandEvent&)  { LoadAndRender(); }

void MDViewerFrame::OnThemeLight(wxCommandEvent&) {
    if (m_darkMode) {
        m_darkMode = false;
        wxConfig cfg("MDViewer");
        cfg.Write("darkMode", false);
        LoadAndRender();
    }
}

void MDViewerFrame::OnThemeDark(wxCommandEvent&) {
    if (!m_darkMode) {
        m_darkMode = true;
        wxConfig cfg("MDViewer");
        cfg.Write("darkMode", true);
        LoadAndRender();
    }
}

void MDViewerFrame::OnOpen(wxCommandEvent&) {
    wxFileDialog dlg(this, "Open file", "", "",
                     "Markdown and HTML files (*.md;*.html;*.htm)|*.md;*.html;*.htm"
                     "|Markdown files (*.md)|*.md"
                     "|HTML files (*.html;*.htm)|*.html;*.htm"
                     "|All files (*)|*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_CANCEL) return;
    m_filePath = dlg.GetPath();
    SetTitle("MDViewer — " + wxFileName(m_filePath).GetFullName());
    LoadAndRender();
}

void MDViewerFrame::OnReload(wxCommandEvent&) { LoadAndRender(); }

void MDViewerFrame::OnSaveHTML(wxCommandEvent&) {
    if (m_filePath.empty()) {
        SetStatusText("No file loaded — open a .md file first.");
        return;
    }

    wxString ext = wxFileName(m_filePath).GetExt().Lower();
    std::string html;
    if (ext == "html" || ext == "htm") {
        html = ReadFile(m_filePath.ToStdString());
    } else {
        std::string raw   = ReadFile(m_filePath.ToStdString());
        std::string body  = RenderMarkdown(raw);
        std::string title = wxFileName(m_filePath).GetFullName().ToStdString();
        html = BuildHTML(body, title, m_darkMode, m_fontSizePercent);
    }

    wxString defaultName = wxFileName(m_filePath).GetName() + ".html";
    wxFileDialog dlg(this, "Save HTML", wxFileName(m_filePath).GetPath(),
                     defaultName,
                     "HTML files (*.html)|*.html|All files (*)|*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_CANCEL) return;

    std::ofstream f(dlg.GetPath().ToStdString());
    f << html;
    SetStatusText("Saved HTML: " + dlg.GetPath());
}

void MDViewerFrame::OpenChat(int chId, const std::string& chTitle) {
    if (m_filePath.empty()) return;

    // Build LLM config from saved app state
    AppState st = LoadAppState();
    LLMConfig cfg;
    cfg.backend     = BackendFromLabel(st.backend);
    cfg.apiKey      = st.apiKey;
    cfg.ollamaModel = st.ollamaModel;

    // Close existing chat window if open
    if (m_chatFrame) {
        m_chatFrame->Close(true);
        m_chatFrame = nullptr;
    }

    m_chatFrame = new ChatFrame(
        this,
        m_filePath.ToStdString(),
        chId,
        chTitle,
        cfg,
        m_darkMode,
        [this]() {
            // Called after each turn is saved — re-render the document
            CallAfter([this]() { LoadAndRender(); });
        }
    );
    // ChatFrame calls Show() in its constructor.
    m_chatFrame->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        m_chatFrame = nullptr;
        e.Skip();
    });
}

void MDViewerFrame::OnExit(wxCommandEvent&)   { Close(true); }

void MDViewerFrame::OnClose(wxCloseEvent& evt) {
    Destroy();
    evt.Skip();
}

void MDViewerFrame::OnFontIncrease(wxCommandEvent&) {
    m_fontSizePercent = std::min(200, m_fontSizePercent + 10);
    wxConfig cfg("MDViewer");
    cfg.Write("fontSizePercent", (long)m_fontSizePercent);
    LoadAndRender();
}

void MDViewerFrame::OnFontDecrease(wxCommandEvent&) {
    m_fontSizePercent = std::max(50, m_fontSizePercent - 10);
    wxConfig cfg("MDViewer");
    cfg.Write("fontSizePercent", (long)m_fontSizePercent);
    LoadAndRender();
}

void MDViewerFrame::OnFontReset(wxCommandEvent&) {
    m_fontSizePercent = 100;
    wxConfig cfg("MDViewer");
    cfg.Write("fontSizePercent", 100L);
    LoadAndRender();
}

// ---------------------------------------------------------------------------
// Edit: copy, select-all, paste-to-render
// ---------------------------------------------------------------------------
void MDViewerFrame::OnCopy(wxCommandEvent&)      { m_webView->Copy(); }
void MDViewerFrame::OnSelectAll(wxCommandEvent&) { m_webView->SelectAll(); }

void MDViewerFrame::OnPasteView(wxCommandEvent&) {
    if (!wxTheClipboard->Open()) return;
    wxTextDataObject data;
    bool ok = wxTheClipboard->IsSupported(wxDF_TEXT) && wxTheClipboard->GetData(data);
    wxTheClipboard->Close();
    if (!ok) return;

    std::string md   = data.GetText().ToStdString();
    std::string body = RenderMarkdown(md);
    std::string html = BuildHTML(body, "Clipboard", m_darkMode, m_fontSizePercent);
    m_webView->SetPage(wxString::FromUTF8(html), wxEmptyString);
    wxString status = "Rendering clipboard markdown";
    if (!m_filePath.empty()) status += "  (Ctrl+R to return to file)";
    SetStatusText(status);
}

// ---------------------------------------------------------------------------
// Find bar
// ---------------------------------------------------------------------------
void MDViewerFrame::OnFindOpen(wxCommandEvent&) {
    if (m_findPanel->IsShown()) {
        m_findCtrl->SetFocus();
        m_findCtrl->SelectAll();
    } else {
        ShowFindBar(true);
    }
}

void MDViewerFrame::OnFindNext(wxCommandEvent&)  { DoFind(true); }
void MDViewerFrame::OnFindPrev(wxCommandEvent&)  { DoFind(false); }
void MDViewerFrame::OnFindClose(wxCommandEvent&) { ShowFindBar(false); }

void MDViewerFrame::ShowFindBar(bool show) {
    if (show) {
        m_findPanel->Show();
        PositionFindBar();
        m_findCtrl->SetFocus();
        m_findCtrl->SelectAll();
    } else {
        m_findPanel->Hide();
        m_webView->Find(wxEmptyString);
        m_findStatus->SetLabel(wxEmptyString);
        m_findTotal = 0; m_findCurrent = 0; m_findTerm.clear();
        m_webView->SetFocus();
    }
}

void MDViewerFrame::PositionFindBar() {
    if (!m_findPanel || !m_findPanel->IsShown()) return;
    wxSize panel  = m_findPanel->GetBestSize();
    wxSize client = m_viewPage ? m_viewPage->GetClientSize() : GetClientSize();
    m_findPanel->SetSize(client.x - panel.x - 12, 8, panel.x, panel.y);
    m_findPanel->Raise();
}

void MDViewerFrame::DoFind(bool forward) {
    wxString term = m_findCtrl->GetValue();
    if (term.empty()) {
        m_webView->Find(wxEmptyString);
        m_findStatus->SetLabel(wxEmptyString);
        m_findTotal = 0; m_findCurrent = 0; m_findTerm.clear();
        return;
    }

    bool newSearch = (term != m_findTerm);
    m_findTerm = term;

    // wxWebView::Find() returns -1 on macOS WKWebView (async under the hood),
    // so count via JS instead — only when the term changes.
    if (newSearch) {
        wxString escaped = term.Lower();
        escaped.Replace("\\", "\\\\");
        escaped.Replace("\"", "\\\"");
        wxString js = wxString::Format(
            "(document.body.innerText.toLowerCase().split(\"%s\").length - 1)",
            escaped);
        wxString result;
        long n = 0;
        m_findTotal = (m_webView->RunScript(js, &result) && result.ToLong(&n))
                      ? (int)std::max(0L, n) : 0;
    }

    int flags = wxWEBVIEW_FIND_HIGHLIGHT_RESULT | wxWEBVIEW_FIND_WRAP;
    if (!forward) flags |= wxWEBVIEW_FIND_BACKWARDS;
    m_webView->Find(term, static_cast<wxWebViewFindFlags>(flags));

    if (m_findTotal == 0) {
        m_findCurrent = 0;
        m_findStatus->SetLabel("No results");
    } else {
        if (newSearch) {
            m_findCurrent = 1;
        } else if (forward) {
            m_findCurrent = m_findCurrent % m_findTotal + 1;
        } else {
            m_findCurrent = (m_findCurrent - 2 + m_findTotal) % m_findTotal + 1;
        }
        m_findStatus->SetLabel(wxString::Format("%d of %d", m_findCurrent, m_findTotal));
    }
}

// ---------------------------------------------------------------------------
// Simple JSON field extractor: finds "key":"value" or "key":number in json.
// For string values, returns content between the first pair of quotes after ':'.
// For number values (when expectString=false), returns digits.
static std::string ExtractJsonField(const std::string& json,
                                    const std::string& key,
                                    bool expectString = true) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) ++pos;
    if (pos >= json.size()) return "";
    if (expectString) {
        if (json[pos] != '"') return "";
        ++pos;
        std::string val;
        while (pos < json.size()) {
            char c = json[pos++];
            if (c == '"') break;
            if (c == '\\' && pos < json.size()) {
                char esc = json[pos++];
                switch (esc) {
                    case '"':  val += '"';  break;
                    case '\\': val += '\\'; break;
                    case 'n':  val += '\n'; break;
                    case 'r':  val += '\r'; break;
                    case 't':  val += '\t'; break;
                    default:   val += esc;  break;
                }
            } else {
                val += c;
            }
        }
        return val;
    } else {
        size_t numStart = pos;
        while (pos < json.size() && (std::isdigit((unsigned char)json[pos]) || json[pos] == '-'))
            ++pos;
        return json.substr(numStart, pos - numStart);
    }
}

void MDViewerFrame::OnScriptMessage(wxWebViewEvent& evt) {
    if (evt.GetMessageHandler() == "chat") {
        wxString payload = evt.GetString();
        int sep = payload.Find('|');
        if (sep != wxNOT_FOUND) {
            long chId = 0;
            payload.Left(sep).ToLong(&chId);
            std::string chTitle = payload.Mid(sep + 1).ToStdString();
            OpenChat((int)chId, chTitle);
        }
        return;
    }
    if (evt.GetMessageHandler() == "clipboardCopy") {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(evt.GetString()));
            wxTheClipboard->Close();
        }
        return;
    }
    if (evt.GetMessageHandler() == "note") {
        std::string payload = evt.GetString().ToStdString();
        std::string action  = ExtractJsonField(payload, "action");
        if (action == "add") {
            std::string selText = ExtractJsonField(payload, "selectedText");
            std::string context = ExtractJsonField(payload, "context");
            OnNoteAdd(selText, context);
        } else if (action == "edit") {
            std::string idStr = ExtractJsonField(payload, "id", false);
            if (!idStr.empty()) {
                try { OnNoteEdit(std::stoi(idStr)); } catch (...) {}
            }
        } else if (action == "delete") {
            std::string idStr = ExtractJsonField(payload, "id", false);
            if (!idStr.empty()) {
                try { OnNoteDelete(std::stoi(idStr)); } catch (...) {}
            }
        }
        return;
    }
    long val;
    if (evt.GetString().ToLong(&val)) {
        m_fontSizePercent = (int)std::max(50L, std::min(200L, val));
        wxConfig cfg("MDViewer");
        cfg.Write("fontSizePercent", (long)m_fontSizePercent);
    }
}

// ---------------------------------------------------------------------------
// Note helpers
// ---------------------------------------------------------------------------
std::string MDViewerFrame::CurrentProjectDir() const {
    if (m_filePath.empty()) return "";
    return wxFileName(m_filePath).GetPath().ToStdString();
}

void MDViewerFrame::OnNoteAdd(const std::string& selectedText,
                              const std::string& context) {
    if (m_filePath.empty() || selectedText.empty()) return;
    std::string projDir = CurrentProjectDir();
    auto notes = LoadNotes(projDir);
    wxTextEntryDialog dlg(this, "Enter your note:", "Add Note");
    if (dlg.ShowModal() != wxID_OK) return;
    std::string noteText = dlg.GetValue().ToStdString();
    if (noteText.empty()) return;

    int id = NextNoteId(notes);
    Note n;
    n.id           = id;
    n.anchor       = "note:" + std::to_string(id);
    n.selectedText = selectedText;
    n.text         = noteText;
    n.file         = wxFileName(m_filePath).GetFullName().ToStdString();

    // Read, patch, write the .md file.
    std::string content;
    {
        std::ifstream f(m_filePath.ToStdString());
        content.assign(std::istreambuf_iterator<char>(f), {});
    }
    content = InsertNoteAnchor(content, selectedText, context, id);
    {
        std::ofstream f(m_filePath.ToStdString(), std::ios::trunc);
        f << content;
    }
    notes.push_back(n);
    SaveNotes(projDir, notes);
    LoadAndRender();
}

void MDViewerFrame::OnNoteEdit(int noteId) {
    if (m_filePath.empty()) return;
    std::string projDir = CurrentProjectDir();
    auto notes = LoadNotes(projDir);
    auto it = std::find_if(notes.begin(), notes.end(),
                           [noteId](const Note& n){ return n.id == noteId; });
    if (it == notes.end()) return;
    wxTextEntryDialog dlg(this, "Edit your note:", "Edit Note",
                          wxString::FromUTF8(it->text));
    if (dlg.ShowModal() != wxID_OK) return;
    it->text = dlg.GetValue().ToStdString();
    SaveNotes(projDir, notes);
    LoadAndRender();
}

void MDViewerFrame::OnNoteDelete(int noteId) {
    if (m_filePath.empty()) return;
    std::string projDir = CurrentProjectDir();
    auto notes = LoadNotes(projDir);
    // Remove anchor from file.
    std::string content;
    {
        std::ifstream f(m_filePath.ToStdString());
        content.assign(std::istreambuf_iterator<char>(f), {});
    }
    content = RemoveNoteAnchor(content, noteId);
    {
        std::ofstream f(m_filePath.ToStdString(), std::ios::trunc);
        f << content;
    }
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [noteId](const Note& n){ return n.id == noteId; }),
                notes.end());
    SaveNotes(projDir, notes);
    LoadAndRender();
}
