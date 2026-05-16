# StoryTeller - Codex Guidance

## Tests

Run the test suite before and after code changes:

```bash
cmake --build build --target test_story-teller
./build/test_story-teller
```

Before handing off GUI or backend changes, also build the app:

```bash
cmake --build build --target story-teller
```

All tests must pass before committing or declaring the work complete.

## Workflow

1. Run the suite green before changing behavior.
2. Add or update a focused failing test for new pure logic when practical.
3. Implement the smallest scoped change that satisfies the behavior.
4. Run the suite green again.
5. Build `story-teller` cleanly.

Do not revert unrelated worktree changes. This repo is often dirty during active app development.

## Module Ownership

| File | Owns |
|---|---|
| `html_template.h/cpp` | HTML shell, renderer CSS, embedded Mermaid/highlight.js, log HTML |
| `markdown.h/cpp` | Markdown rendering, inline processing, `:::tidbit` syntax, `--llm` reference |
| `mdviewer.h/cpp` | Main wx frame, menus, notebook, file loading, View tab behavior |
| `create_panel.h/cpp` | Create tab UI, project selection, character library, generation flow |
| `edit_panel.h/cpp` | Edit tab UI, rewrite/translate actions, file list, git history controls |
| `creator.h/cpp` | Prompt building, generated content helpers, translation cleanup, saving |
| `project.h/cpp` | Project layout, `.index`, `.config`, stable chapter/tidbit IDs |
| `editor.h/cpp` | Extracting and patching chapters/tidbits in Markdown files |
| `llm.h/cpp` | LLM backend dispatch: Clipboard, Claude CLI, Codex CLI, Ollama, Anthropic API |
| `llm_response.h/cpp` | JSON escaping/parsing and Ollama response helpers |
| `git_ops.h/cpp` | Git init/commit/log/show/restore/diff helpers |

Headers are the public interface. Keep implementation detail in `.cpp` files.

## C++ Style

- Use C++17.
- Prefer free functions for logic that does not need object state.
- Keep functions small and extract helpers when behavior branches.
- Avoid raw `new`/`delete` outside wxWidgets object ownership patterns already used in the UI.
- Use raw string literals only at clean HTML/CSS/JS boundaries.
- Use structured helpers/parsers over ad hoc string slicing when a local helper already exists.

## Product Notes

StoryTeller is a wxWidgets desktop app for:

- rendering Markdown/HTML offline,
- generating story Markdown through LLM backends,
- editing chapters and tidbits by stable `<!-- ch:N -->` / `<!-- tb:N -->` markers,
- translating selected files or selected git versions into new files,
- viewing, diffing, restoring, and committing project file history.

Project folders live under the configured `defaultFolder` and contain `claude.md`, generated `.md` files, `.index`, optional `.config`, and optionally `.git`.

## LLM Backend Notes

- Claude CLI uses `claude -p`.
- Codex CLI uses `codex --ask-for-approval never exec ... --output-last-message`.
- Ollama uses `http://localhost:11434/api/generate` with JSON mode where applicable.
- Clipboard mode copies prompts and does not write model output automatically.
- Translation writes a new suffixed file such as `_spanish.md`; it should not overwrite the source file except when regenerating the same language filename.

## Frontend/UI Notes

- Keep the app utilitarian and dense; this is a work tool, not a landing page.
- Use existing wxWidgets patterns in `create_panel.cpp` and `edit_panel.cpp`.
- After UI changes, build `story-teller`; pure tests do not compile all wx UI paths.
