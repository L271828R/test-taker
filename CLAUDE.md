# TestTaker — Claude guidance

## Tests

Always run the test suite **before and after** making any code changes.

```bash
cmake --build build --target test_test-taker && ./build/test_test-taker
```

All tests must pass before committing.

## TDD cycle — applies to ALL code changes, including bug fixes

Every change to production code — new feature, bug fix, or refactor — follows this exact order. No exceptions.

1. **Run the suite green.** Confirm all existing tests pass before touching anything.
2. **Write a failing test.** Add the test that exercises the new behaviour or reproduces the bug. Run the suite — it must fail on the new case and pass on all others.
3. **Implement.** Write the minimum code to make the new test pass.
4. **Run the suite green again.** All tests — old and new — must pass.
5. **Build clean.** `bash build.sh` — zero warnings expected.

Never write implementation code before a failing test exists for it. For bug fixes: the test must fail against the buggy code before the fix is applied. Never skip to step 5 to "just check it compiles."

## Code organisation and separation of concerns

Split by concern — a new concern gets a new file, not more lines in an existing one.

| File | Owns |
|---|---|
| `html_template.h/cpp` | `WrapWithTemplate`, CSS/JS generation, `BuildLogsHTML` |
| `markdown.h/cpp` | `RenderMarkdown`, `ProcessInline`, `EscapeHTML` |
| `app_frame.h/cpp` | Main window, five-tab notebook, inter-panel callbacks |
| `project_panel.h/cpp` | Project browser, folder picker, config persistence |
| `new_session_panel.h/cpp` | Session config form, LLM backend picker, Start button |
| `exam_panel.h/cpp` | Active Q&A loop, scoring display, Submit/Skip/Flag |
| `review_panel.h/cpp` | Session list, flagged question browser, drill launch |
| `chat_panel.h/cpp` | Embedded chat tab, history via `chat.md` |
| `session.h/cpp` | `QuestionTurn`, session parse/serialise, `AppendSessionTurn` |
| `exam_meta.h/cpp` | `.testtaker.json` read/write, `RecordSession`, `EnsureExamMeta` |
| `exam_prompt.h/cpp` | `BuildFirstQuestionPrompt`, scoring prompt, response parsing |
| `llm.h/cpp` | LLM backend dispatch (`claude -p`, API, Ollama, Clipboard) |
| `config.h/cpp` | `AppConfig`/`AppState` load/save at `~/.config/test-taker/` |
| `project.h/cpp` | Project folder layout, per-project config |

When a function doesn't clearly belong to an existing module, make a new one rather than stretching an existing file. If you are unsure, ask.

Headers are the public interface. A reader should understand a module by reading its `.h` file alone — keep them free of implementation detail.

## C++ style

- Prefer free functions over member functions when a function doesn't need `this`.
- No raw `new`/`delete` — use `wxWeakRef`, stack allocation, or smart pointers.
- Raw string literals (`R"HTML(...)HTML"`) must not be broken mid-tag. If you need to splice a runtime value in, end the literal at a clean boundary (end of attribute value, end of line).
- Keep functions under ~50 lines. If a function is growing, extract a named helper.

## Content creation features (in progress)

The app is being extended with a **Create** tab for LLM-assisted markdown generation. Key design decisions:

- **Project folders** — each project lives in its own directory containing `claude.md` (project-level prompt context), the generated chapter `.md` files, and an index file tracking numeric IDs.
- **Numeric IDs** — every chapter and every tidbit block carries a short numeric identifier (e.g. `ch:3`, `tb:7`) embedded as an HTML comment in the rendered source. These IDs are stable across edits.
- **Edit tab** — the user pastes an ID and a plain-English instruction ("make this friendlier", "swap character for Einstein"). The app looks up the block, builds a patch prompt, calls the LLM, and splices the result back into the source file.
- **LLM backends** — `claude -p` (shell out), Anthropic API (key in config), Ollama (local HTTP), or clipboard (build prompt and copy, user pastes manually).
- **Tidbit characters** — the Create form lets the user pick characters; personas are passed in the prompt alongside the `--llm` syntax reference so the LLM produces valid tidbit blocks.

New tests for these features live in `tests/test_creator.cpp`, `tests/test_project.cpp`, and `tests/test_editor.cpp`.
