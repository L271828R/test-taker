# TestTaker

A personal exam and interview practice tool for macOS with RAG (Retrieval-Augmented Generation) support, built with C++ and wxWidgets. TestTaker runs as a single native binary — no server, no browser, no CORS. You practice, it scores you, you review what you got wrong.

Upload PDFs or text files to a per-project **corpus**. When you start a session, TestTaker embeds your topic, retrieves the most relevant passages from your documents, and injects them into the exam prompt — so the LLM asks questions grounded in your actual study material.

## What it does

You create a **project** for a topic (C++ interviews, AWS certification, Spanish vocabulary, whatever), then run timed exam sessions against it. An LLM asks you questions, scores your answers, and explains what you missed. Flagged questions go into a review queue you can drill again later.

### Six tabs

| Tab | Purpose |
|-----|---------|
| **Projects** | Create and activate projects. Each project is a folder on disk. |
| **New Session** | Configure and start an exam — topic, difficulty, question count, LLM backend. Toggle "Use corpus" to enable RAG. |
| **Exam** | The live Q&A loop. Submit answers, skip, flag questions for review. Hover any answered turn to flag it. |
| **Review** | Browse past sessions. Select a session to see all its questions. Right-click any question to flag/unflag. Double-click to re-drill. |
| **Chat** | Free-form study chat for the active project. History is saved in `chat.md`. |
| **Corpus** | Upload PDFs or text files. Documents are copied into the project folder, chunked, and embedded via Ollama (`nomic-embed-text`). Double-click any entry to open it. |

### State that survives restarts

- Last active project is re-activated automatically.
- New Session form fields (topic, backend, credentials) are restored.
- An incomplete exam session is resumed from where you left off — answered turns reload, the next question is fetched.

## LLM backends

| Backend | How |
|---------|-----|
| `claude -p` | Shells out to the [Claude CLI](https://claude.ai/code) — default, no config needed |
| Anthropic API | Direct HTTP — paste your API key in the New Session form |
| Ollama | Local HTTP at `localhost:11434` — pick a model |
| Clipboard | Builds the prompt and copies it; you paste into any LLM manually |

## Session files

Each session is a plain Markdown file in the project folder:

```
# C++ Interview — Session

**Topic:** C++ memory model
**Difficulty:** hard
**Questions:** 10
**Backend:** claude -p

:::session[Session]
Q: What is RAII?
A: Resource Acquisition Is Initialization — tie resource lifetime to object lifetime.
SCORE: correct
FLAG: false
EXPLANATION: Correct. Constructor acquires, destructor releases.

Q: Explain the vtable layout for multiple inheritance.
A: 
SCORE: skipped
FLAG: true
EXPLANATION: Each base class gets its own vtable pointer at the start of the object…
:::
```

Sessions are append-only during a run. Flags toggle in place. Everything is human-readable and editable in vim.

## Project layout

```
~/test-taker/
└── CppInterview/
    ├── context.md                    ← injected into every prompt as background material
    ├── chat.md                       ← chat history
    ├── corpus.db                     ← SQLite: document metadata + float-vector embeddings
    ├── corpus/
    │   ├── cpp_reference.pdf         ← files copied here on upload
    │   ├── cpp_reference.pdf.txt     ← extracted text sidecar
    │   └── notes.txt
    ├── .testtaker.json               ← session index with scores
    ├── .index                        ← internal ID counter
    └── session_20260516_185046.md
```

`context.md` is optional. Add reference material, constraints, or persona instructions there — the app prepends it to every exam prompt and chat message.

### How RAG works

1. **Upload** — each file is split into overlapping ~350-word chunks; each chunk is embedded into a 768-dimensional vector by `nomic-embed-text` and stored in `corpus.db`.
2. **Session start** — the session topic is embedded, and the top-5 closest chunks (by cosine similarity) are retrieved from SQLite using a brute-force dot-product search in C++.
3. **Prompt injection** — the retrieved chunks are prepended to the exam prompt as `Relevant excerpts from the study corpus`, so the LLM generates questions grounded in your material.

No external vector database or ML framework is required.

## Dependencies

| Dependency | Version | Required for |
|------------|---------|--------------|
| [wxWidgets](https://wxwidgets.org) | 3.2+ | UI |
| CMake | 3.16+ | Build |
| `xxd` | any (ships with Vim / macOS) | Asset embedding |
| SQLite | 3+ | Corpus storage |
| [Ollama](https://ollama.com) + `nomic-embed-text` | any | RAG embeddings |
| `pdftotext` (poppler) | any | PDF text extraction |

```bash
brew install wxwidgets sqlite poppler
ollama pull nomic-embed-text
```

RAG and PDF support are optional — the app works without Ollama and pdftotext, it just won't embed or extract PDFs.

## Build

```bash
git clone https://github.com/your-username/test-taker.git
cd test-taker/app
bash build.sh
```

First configure downloads `mermaid.min.js` and `highlight.js` and embeds them into the binary via `xxd`. All subsequent builds are fully offline.

### Run tests

```bash
cmake --build build --target test_test-taker && ./build/test_test-taker
```

All tests are headless (no wxWidgets dependency) and cover config parsing, session serialisation, exam prompt building, and conversation persistence.

## Configuration

| File | Purpose |
|------|---------|
| `~/.config/test-taker/config` | `defaultFolder` — root directory for all projects |
| `~/.config/test-taker/state` | Last active project, form state, last session file |
| `~/Library/Logs/TestTaker/test-taker.log` | Timestamped log — LLM calls, scores, errors |

Set the projects folder on first launch via the **Set Projects Folder…** button in the Projects tab.

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Enter` | Send chat message |
| `Ctrl+Shift+L` | Light mode |
| `Ctrl+Shift+D` | Dark mode |
| `Ctrl++` / `Ctrl+-` | Increase / decrease font size |
| `Ctrl+0` | Reset font size |
| `Ctrl+Q` | Quit |

## Source layout

```
src/
├── app_frame.h/cpp         — main window, six-tab notebook, inter-panel callbacks
├── project_panel.h/cpp     — project browser, folder picker, new project
├── new_session_panel.h/cpp — session config form, LLM backend picker, corpus toggle
├── exam_panel.h/cpp        — live Q&A loop, flag-on-hover, session resume
├── review_panel.h/cpp      — session list, question browser, flag toggle
├── chat_panel.h/cpp        — study chat, persisted to chat.md
├── corpus_panel.h/cpp      — corpus tab: upload, embed, delete, double-click to open
├── corpus.h/cpp            — SQLite corpus: document CRUD, chunk storage, KNN search
├── embeddings.h/cpp        — ChunkText, EmbedText (Ollama), ExtractPDF (pdftotext)
├── session.h/cpp           — session parse/serialise, AppendSessionTurn, LoadSessionHeader
├── exam_meta.h/cpp         — .testtaker.json read/write, session index
├── exam_prompt.h/cpp       — LLM prompt builders, RenderExamTurns
├── llm.h/cpp               — backend dispatch (claude -p, API, Ollama, clipboard)
├── config.h/cpp            — AppConfig / AppState load/save
└── conversation.h/cpp      — chat history parse/persist, BuildQAPrompt
tests/
└── test_*.cpp              — unit tests, no wxWidgets dependency
```

## License

MIT
