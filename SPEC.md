# MDViewer — Implementation Specification

A self-contained offline Markdown and HTML viewer written in **C++17** using
**wxWidgets 3.x** as the GUI framework. The primary goal is to render Markdown
files (including Mermaid diagrams) to a styled HTML view inside a native
desktop window, with no internet connection required at runtime.

---

## Target Platform & Toolchain

- **OS**: macOS (primary), Linux and Windows should work with minor adjustments
- **Compiler**: AppleClang 17 / GCC 12+ / MSVC 2022
- **C++ standard**: C++17 (structured bindings, `std::string_view` etc.)
- **Build system**: CMake 3.16+
- **GUI**: wxWidgets 3.3.x — components `core`, `base`, `webview`
- **Web engine**: WKWebView (macOS), WebKitGTK (Linux), WebView2 (Windows)

---

## Repository Layout

```
mdviewer/
├── CMakeLists.txt      — build, download, and xxd-embed mermaid.min.js
├── main.cpp            — wxApp subclass + entry point
├── mdviewer.h          — MDViewerFrame declaration
├── mdviewer.cpp        — full implementation
├── mermaid.min.js      — downloaded once at cmake configure time (gitignore-able)
├── build.sh            — convenience: configure if needed, then compile
└── rebuild.sh          — wipe build/, reconfigure, recompile from scratch
```

The `build/` directory is generated; `mermaid_js.h` is generated inside it by
`xxd` during the build and is never committed.

---

## Build System — CMakeLists.txt

### Key steps in order

1. `find_package(wxWidgets REQUIRED COMPONENTS core base webview)`
2. **Download** `mermaid.min.js` at *configure* time using CMake's
   `file(DOWNLOAD …)` — only if the file does not already exist in the source
   tree. On failure, print a `FATAL_ERROR` with the manual `curl` command.
   URL: `https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js`
3. **Embed**: `add_custom_command` runs
   `xxd -i -n mermaid_js_data mermaid.min.js > build/mermaid_js.h`
   This produces a C array `unsigned char mermaid_js_data[]` and
   `unsigned int mermaid_js_data_len`.
4. `add_custom_target(gen_mermaid_header ALL DEPENDS …)` +
   `add_dependencies(mdviewer gen_mermaid_header)`
5. `target_include_directories(mdviewer PRIVATE ${CMAKE_CURRENT_BINARY_DIR})`
   so `#include "mermaid_js.h"` resolves.
6. On macOS: `find_library(WEBKIT_FW WebKit)` and link if found.

---

## Source Files

### main.cpp

- Subclass `wxApp` as `MDViewerApp`.
- `OnInit()`: validate `argc >= 2`, check the file exists with `wxFileExists`,
  then construct `MDViewerFrame(argv[1])` and call `Show(true)`.
- `wxIMPLEMENT_APP(MDViewerApp)` macro.

### mdviewer.h

```cpp
enum {
    ID_RELOAD       = wxID_HIGHEST + 1,
    ID_THEME_LIGHT,
    ID_THEME_DARK,
};

class MDViewerFrame : public wxFrame {
    wxWebView* m_webView;
    wxString   m_filePath;
    bool       m_darkMode;

    void        LoadAndRender();
    std::string ReadFile(const std::string& path);
    std::string RenderMarkdown(const std::string& md);
    std::string ProcessInline(const std::string& text);
    static std::string EscapeHTML(const std::string& text);
    std::string WrapWithTemplate(const std::string& body,
                                 const std::string& title,
                                 bool darkMode);

    // handlers: OnOpen, OnReload, OnThemeLight, OnThemeDark, OnExit, OnClose
};
```

### mdviewer.cpp

#### Free function: `GetMermaidJS()`

```cpp
static const std::string& GetMermaidJS() {
    static std::string cached;
    if (cached.empty()) {
        cached.assign(reinterpret_cast<const char*>(mermaid_js_data),
                      mermaid_js_data_len);
        // Sanitise: HTML parser closes <script> at the first "</script>"
        // even inside a string literal. Replace with <\/script> (JS-safe).
        const std::string bad  = "</script>";
        const std::string safe = "<\\/script>";
        size_t pos = 0;
        while ((pos = cached.find(bad, pos)) != std::string::npos) {
            cached.replace(pos, bad.size(), safe);
            pos += safe.size();
        }
    }
    return cached;
}
```

#### Constructor

1. Load `bool m_darkMode` from `wxConfig("MDViewer")` key `"darkMode"`,
   default `false`.
2. Build **File** menu: Open (`Ctrl+O`), Reload (`Ctrl+R`), separator, Exit (`Ctrl+Q`).
3. Build **View** menu: two `AppendRadioItem` entries —
   `ID_THEME_LIGHT` "Light Mode" (`Ctrl+Shift+L`) and
   `ID_THEME_DARK` "Dark Mode" (`Ctrl+Shift+D`).
   Call `view->Check(m_darkMode ? ID_THEME_DARK : ID_THEME_LIGHT, true)`.
4. `CreateStatusBar()`.
5. `m_webView = wxWebView::New(this, wxID_ANY, "about:blank")`.
6. Call `LoadAndRender()`.

#### LoadAndRender()

```
ext = lower-case extension of m_filePath
if ext == "html" or "htm":
    m_webView->LoadURL("file://" + m_filePath)
else:
    raw  = ReadFile(m_filePath)
    body = RenderMarkdown(raw)
    html = WrapWithTemplate(body, filename, m_darkMode)
    baseURL = "file://" + directory of m_filePath + path separator
    m_webView->SetPage(wxString::FromUTF8(html), baseURL)
```

The `baseURL` is critical — it lets WebKit resolve relative image paths
(e.g. `![](./diagram.png)`) relative to the Markdown file's directory.

#### Theme handlers

```cpp
void OnThemeLight(wxCommandEvent&) {
    if (m_darkMode) {
        m_darkMode = false;
        wxConfig("MDViewer").Write("darkMode", false);
        LoadAndRender();
    }
}
// OnThemeDark is the mirror image
```

The preference is written to `wxConfig` on every change so it persists
across restarts. On macOS this lands in `~/Library/Preferences/MDViewer.plist`.

---

## Markdown Parser — RenderMarkdown()

A **single-pass, line-by-line state machine**. No external parsing library.

### Block-level state tracked

| Variable    | Purpose |
|-------------|---------|
| `inCode`    | Inside a fenced non-mermaid code block |
| `inMermaid` | Inside a ` ```mermaid ` block |
| `fenceChar` | `'`'` or `'~'` — set when opening fence is seen |
| `mermaidBuf`| Accumulates Mermaid diagram source lines |
| `inUL`      | Inside `<ul>` |
| `inOL`      | Inside `<ol>` |
| `inBQ`      | Inside `<blockquote>` |
| `inTable`   | Inside GFM `<table>` |
| `paraLines` | Accumulated lines of the current paragraph |

### Fence detection helpers (lambdas)

- **`getFenceInfo(line)`** — returns `{true, lang}` if line starts with 3+
  backticks or tildes; `lang` is the trimmed info string after the fence chars.
- **`isClosingFence(line, fenceChar)`** — true if line contains only
  `fenceChar` chars (and optional spaces/tabs), at least 3 of them.

### Block rules matched in this priority order

1. **Inside code/mermaid block**: accumulate or close on matching fence.
2. **Opening fence**: flush paragraph, close lists/BQ/table, set `fenceChar`,
   branch on `lang == "mermaid"`.
3. **Empty line**: flush paragraph, close lists/BQ/table.
4. **Setext header** (look-ahead): next line is all `=` → `<h1>`;
   all `-` (≥3) → `<h2>`. Consumes the underline line.
5. **ATX header**: line starts with 1–6 `#` followed by a space.
   Strip trailing `#` and spaces from the heading text.
6. **Horizontal rule**: stripped line is all `-`, `*`, or `_` (≥3 chars),
   only when `paraLines` is empty.
7. **Blockquote**: line starts with `> `.
8. **Unordered list**: line starts with `- `, `* `, or `+ `.
9. **Ordered list**: line starts with one or more digits followed by `. `.
10. **GFM table header**: line contains `|` and the *next* line matches
    `/^[-|: \t]+$/` with at least one `|`. Emit `<table><thead>`, skip the
    separator line, set `inTable = true`.
11. **GFM table body row**: `inTable == true` and line contains `|`.
12. **Plain paragraph**: append to `paraLines`.

### Mermaid block output

```html
<div class="mermaid-wrapper"><div class="mermaid">
{diagram source}
</div></div>
```

Mermaid.js finds `.mermaid` divs and replaces them with SVG in-place.

### flushParagraph()

Join `paraLines` with spaces (or `<br>` if a line ends with two spaces),
wrap in `<p>ProcessInline(…)</p>`.

---

## Inline Formatter — ProcessInline()

Character-by-character scan with greedy matching in this order:

| Pattern | Output |
|---------|--------|
| `` `…` `` | `<code>EscapeHTML(…)</code>` |
| `***…***` | `<strong><em>…</em></strong>` |
| `**…**` | `<strong>…</strong>` |
| `__…__` | `<strong>…</strong>` |
| `*…*` | `<em>…</em>` |
| `_…_` | `<em>…</em>` |
| `~~…~~` | `<del>…</del>` |
| `![alt](url)` | `<img src="url" alt="alt" loading="lazy">` |
| `[text](url)` | `<a href="url">ProcessInline(text)</a>` |
| `<http…>` or `<mailto:…>` | auto-link `<a href="…">` |
| `&` `<` `>` | `&amp;` `&lt;` `&gt;` |

Inner content of bold/italic/links is recursively passed through
`ProcessInline`. Inline code content is passed through `EscapeHTML` only
(no markdown inside code spans).

---

## HTML Template — WrapWithTemplate()

Signature: `WrapWithTemplate(body, title, darkMode)`

The function constructs a complete HTML document as a C++ string using raw
string literals (`R"HTML(…)HTML"`). Key insertion points:

- `<html lang="en"` + (`class="dark"` if `darkMode`) + `>`
- `<title>` + `EscapeHTML(title)` + `</title>`
- `<script>` + `GetMermaidJS()` + `</script>` — the 3.3 MB bundle inline
- CSS block (see below)
- `body` string (rendered HTML)
- Zoom modal HTML
- Init script block where `mermaid.initialize` theme is injected:
  `theme:'dark'` when `darkMode`, else `theme:'default'`

### CSS — Custom Properties (theming)

Two sets of tokens; the `class="dark"` on `<html>` activates the dark set:

| Token | Light | Dark |
|-------|-------|------|
| `--bg` | `#ffffff` | `#0d1117` |
| `--surface` | `#f6f8fa` | `#161b22` |
| `--surface2` | `#fafbfc` | `#1c2128` |
| `--text` | `#24292f` | `#e6edf3` |
| `--text-muted` | `#57606a` | `#8b949e` |
| `--border` | `#d0d7de` | `#30363d` |
| `--link` | `#0969da` | `#58a6ff` |
| `--link-hover` | `#0550ae` | `#79c0ff` |
| `--code-inline` | `rgba(175,184,193,0.2)` | `rgba(110,118,129,0.4)` |
| `--del` | `#656d76` | `#8b949e` |
| `--zm-svg-bg` | `#ffffff` | `#1e2430` |
| `--mermaid-hover` | `rgba(9,105,218,0.16)` | `rgba(88,166,255,0.12)` |
| `--mermaid-ring` | `rgba(9,105,218,0.53)` | `rgba(88,166,255,0.45)` |

Every colour in the CSS uses `var(--token)` — no hardcoded colours outside
the `:root`/`.dark` blocks.

### Mermaid wrapper CSS

```css
.mermaid-wrapper {
  cursor: zoom-in;
  border: 1px solid var(--border);
  background: var(--surface2);
  /* hover: box-shadow with --mermaid-hover, border-color with --mermaid-ring */
}
```

### Zoom modal

HTML structure:
```html
<div id="zm-overlay">          <!-- full-screen dark backdrop -->
  <button id="zm-close">✕</button>
  <div id="zm-stage">          <!-- 90vw × 90vh, overflow:hidden, cursor:grab -->
    <div id="zm-inner"></div>  <!-- transform target -->
  </div>
  <div id="zm-hint">…</div>   <!-- "Scroll · Drag · ESC" -->
  <div id="zm-scale">100%</div>
</div>
```

`#zm-inner svg` background uses `var(--zm-svg-bg)` so it matches the theme.

### Zoom JavaScript

State variables: `zmScale`, `zmTX`, `zmTY`, `zmDrag`, `zmDX`, `zmDY`,
`lastDist` (for pinch).

`zmApply()` sets `zmInner.style.transform = translate(…) scale(…)` and
updates the scale label.

**Opening the zoom**: event delegation on `document` — any click on
`.mermaid-wrapper` (but not inside `#zm-overlay`) extracts the SVG's
`outerHTML`, strips `width`/`height` attributes, and calls `zmOpen()`.

**Controls**:
- Mouse wheel on `#zm-stage` → scale ±12%/±10%, clamped 0.08–20
- `mousedown/mousemove/mouseup` on `#zm-stage` → pan via translate
- Two-finger pinch (`touchstart/touchmove`) → scale via `Math.hypot` distance ratio
- One-finger drag (`touchmove`) → pan
- ESC key or click on backdrop → `zmClose()`
- `#zm-close` button → `zmClose()`

---

## Convenience Shell Scripts

### build.sh

```bash
#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
if [ ! -d build ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build -j"$(sysctl -n hw.logicalcpu)"
echo "Run: ./build/mdviewer <file.md|file.html>"
```

### rebuild.sh

Same as `build.sh` but begins with `rm -rf build`.

---

## Behaviour Notes

- **`.md` / anything not `.html`/`.htm`** → rendered through the Markdown
  parser → `SetPage(html, baseURL)`.
- **`.html` / `.htm`** → loaded directly with `LoadURL("file://…")` so
  existing CSS/JS/assets in the file work unchanged.
- **Relative images** in Markdown resolve correctly because `SetPage` is called
  with `baseURL = "file://" + directory_of_file + "/"`.
- **`</script>` sanitisation** in `GetMermaidJS()` is a one-time O(n) pass
  cached in a `static std::string`. `<\/script>` is identical at JS runtime
  but invisible to the HTML tokeniser.
- **`wxConfig`** key `"darkMode"` (bool) under app name `"MDViewer"` persists
  the theme. Written on every theme change; read in the constructor.
- The View menu radio items are initialised with `Check(…, true)` in the
  constructor to reflect the loaded preference.
