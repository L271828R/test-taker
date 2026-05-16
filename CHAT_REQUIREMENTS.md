# Chapter Chat — Requirements

## Overview
Each chapter heading in a rendered MDViewer document has a 💬 button.
Clicking it opens a dedicated `ChatFrame` for multi-turn Q&A with an LLM.
Conversations are persisted in the source markdown file and render as
collapsed `<details>` blocks in the main document.

## User flow
1. Reader opens a document and sees 💬 buttons on chapter headings.
2. Reader clicks 💬 on Chapter 3.
3. A `ChatFrame` opens titled "Chat — Chapter 3: The Stack Frame".
4. Reader types a question; Send fires the LLM in a background thread.
5. Answer appears in the chat window when the LLM responds.
6. Reader continues the conversation (multi-turn).
7. On each LLM response the markdown file is patched with the new Q/A pair.
8. When the chat window closes, the main document re-renders to show the
   `:::conversation` block under that chapter.

## Markdown storage

```
:::conversation[Chapter 3: The Stack Frame]
Q: What is a stack frame?
A: A stack frame is a contiguous region of stack memory…

Q: Does Valgrind see stack frames?
A: No — Valgrind only intercepts heap allocations.

:::
```

One `:::conversation` block per chapter, placed just before the horizontal
rule (or next chapter marker) that ends the chapter's section.
Subsequent questions append new Q/A pairs inside the same block.

## HTML rendering
`:::conversation[Title]` → `<details class="conversation">` (collapsed).
Summary line: "💬 N questions — Title".
Body: Q and A divs with distinct styling.

## ChatFrame
- `wxFrame`, non-modal, opens beside the document.
- Title: "Chat — {chapterTitle}".
- Layout: wxWebView (conversation history) + wxTextCtrl input + Send button.
- Send disabled while LLM is processing.
- Pre-populated with existing conversation on open.
- On close: main document re-renders (callback).

## LLM prompt
- Full document markdown as background context.
- Focus instruction: chapter number and title.
- Full conversation history included.
- LLM runs on a background thread via existing `InvokeLLM`.

## Out of scope (POC)
- Streaming responses
- Multiple simultaneous chat windows
- Conversation deletion/editing
- Backend selector in the chat window (uses saved AppState backend)
