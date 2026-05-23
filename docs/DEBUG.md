# Debugging Core Dumps — test-taker

Reference for debugging crashes in this project. Written for Claude Code sessions.

---

## 1. Build a debug binary

The default `build.sh` produces a `Release` binary. Crash reports from Release builds
have symbolicated frames (macOS does this automatically for known binaries), but local
variables and inlined functions are optimised away. For full debuggability, build with
debug symbols and no optimisation:

```bash
# One-time: configure a parallel debug build directory
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug

# Build (repeat after code changes)
cmake --build build-debug -j$(sysctl -n hw.logicalcpu)

# Run the debug binary
./build-debug/test-taker
```

The debug binary is **much larger** and **noticeably slower** — use it only when
hunting a specific crash.

> **Note:** The existing `build/` directory stays as-is (Release). Keep both.
> Tests always use the Release build: `cmake --build build --target test_test-taker`.

---

## 2. Enable core dumps (macOS)

By default macOS does not write core files. Enable them for the current shell session:

```bash
ulimit -c unlimited          # allow arbitrarily large core files
sudo sysctl kern.coredump=1  # ensure kernel-level coredump is on (usually already is)
```

Core files land in the **current working directory** when the process crashes.
`/cores/` is also checked by some tools.

Run the app from the project root so the core appears there:

```bash
cd /Users/rueda/project/test-taker
ulimit -c unlimited
./build-debug/test-taker
# crash → writes `core` or `core.<pid>` in this directory
```

---

## 3. Inspect the core with lldb

### Post-mortem (core file already exists)

```bash
lldb ./build-debug/test-taker -c core          # or core.<pid>
```

Key commands once inside lldb:

```
(lldb) bt                   # backtrace for the crashed thread
(lldb) bt all               # backtraces for every thread
(lldb) thread list          # see all threads and which one crashed
(lldb) thread select 0      # switch to thread 0 (main thread)
(lldb) frame select 3       # jump to frame 3 in the current backtrace
(lldb) frame info           # file + line of current frame
(lldb) p someVariable       # print a variable
(lldb) po someObject        # print with description (good for wx objects)
(lldb) x/s addr             # read memory as a C string at address
(lldb) disassemble          # assembly listing around the crash
(lldb) quit
```

### Live attach (catch it while running)

```bash
lldb ./build-debug/test-taker
(lldb) run
# reproduce the crash
(lldb) bt all
```

Or attach to a running process by PID (shown in the title bar):

```bash
lldb -p 52762
(lldb) bt all
```

---

## 4. Read macOS crash reports without a core file

macOS writes `.ips` crash reports automatically. Location:

```
~/Library/Logs/DiagnosticReports/test-taker-<date>.ips
```

Open in Console.app for a formatted view, or:

```bash
# List recent crash reports
ls -lt ~/Library/Logs/DiagnosticReports/ | grep test-taker | head -10

# Print the symbolicated crash report
cat "~/Library/Logs/DiagnosticReports/test-taker-2026-05-23-080906.ips"
```

The crash report includes:
- **Exception Type / Subtype** — e.g. `EXC_BAD_ACCESS (SIGSEGV)` with the faulting address
- **Thread N Crashed** — full symbolicated stack trace
- **All thread stacks** — useful when the crash is in a background thread

### Reading the crash address

```
Exception Subtype: KERN_INVALID_ADDRESS at 0x000000000000031f
```

An address near zero (like `0x31f`) almost always means a **null pointer + small offset**:
the `this` pointer was null, and the crash is at field offset `0x31f` (799 bytes).

```
0   libc++.1.dylib    std::string::operator=(...)  + 8
1   test-taker        ReviewPanel::RefreshSessions(...) + 28
```

Frame 1 tells you the method and byte offset into it. Load the binary in lldb and
disassemble to find the exact source line:

```bash
lldb ./build-debug/test-taker
(lldb) disassemble -n "ReviewPanel::RefreshSessions"
# count bytes from the start of the function to offset +28
```

Or just add the offset to the symbol address:

```bash
(lldb) image lookup -a 0x10076b638   # paste crash address from report
```

---

## 5. Diagnosing the specific crashes seen in this project

### Crash pattern: `SIGSEGV` in `ReviewPanel::RefreshSessions` at startup

**Cause:** `AddScriptMessageHandler` on `wxWebView` internally calls `RunScript`, which
calls `wxYieldFor`, which pumps the event loop. If this happens inside `ExamPanel`'s
constructor (called before `ReviewPanel` is constructed), `OnProjectActivated` can fire
with `m_reviewPanel` still null.

**Fix applied:** Deferred `AddScriptMessageHandler` with `wxTheApp->CallAfter(...)`.

**How to spot it next time:** Crash in any panel method called from
`AppFrame::OnProjectActivated`, with the panel's `this` pointer near-zero.
Check whether the crashing panel is constructed *after* `ExamPanel` in `AppFrame`.

---

### Warning pattern: "Error running JavaScript: ReferenceError: Can't find variable: X"

macOS WKWebView's `evaluateJavaScript:completionHandler:` (which wx's `RunScript`
wraps) executes against the *committed* DOM at the moment of the call — which may be
the old page if `SetPage()` is still loading asynchronously.

**Root cause (this project):** The bundled mermaid + highlight JS is ~20 MB. The initial
`Render()` call in `ExamPanel`'s constructor starts loading this large page, but the page
hasn't committed by the time `CallAfter` fires on the next event-loop tick.
`OnProjectActivated` then fires inside `YieldFor`, calling `ResumeSession` →
`RequestNextQuestion()` → `RunScript("setBusy(...)")` — still against `about:blank`.
Stubs in `BuildHTML`'s `<head>` don't help because that big page hasn't loaded yet.

**Fix applied:**
1. In `ExamPanel`'s constructor, immediately call `SetPage` with a tiny (~100 byte) stub
   page that defines `setBusy`/`showHint` as no-ops. This replaces `about:blank` in
   microseconds, well before `CallAfter` fires.
2. Removed the initial `Render()` from the constructor — `OnProjectActivated` always
   calls either `ResumeSession` or `Clear()`, both of which call `Render()`.
3. All three `RunScript("setBusy(...)")` calls guarded with
   `if(typeof setBusy==='function')` as belt-and-suspenders.

**How to spot it next time:**
- Error dialog says `ReferenceError: Can't find variable: X` on startup (not on user action).
- The failing `RunScript` fires during the `YieldFor` inside `AddScriptMessageHandler`'s
  internal `RunScript`, which means any `CallAfter` or event can trigger it.
- Check: is the page that defines `X` still loading (large page)? Is there a committed
  fallback page that defines `X` as a no-op stub?
- Fix order: stub page → guard → then eliminate unnecessary initial `Render()` calls.

---

## 6. Useful lldb one-liners for this codebase

```bash
# Print all std::string members of the current frame
(lldb) frame variable -T | grep string

# Watch a member variable for writes
(lldb) watchpoint set variable m_busy

# Break on any method in ExamPanel
(lldb) breakpoint set -r "ExamPanel::"

# Break on RunScript to see every JS call
(lldb) breakpoint set -n "wxWebView::RunScript"
(lldb) breakpoint command add   # add `bt` as the command so you get a trace each time

# Print the wxString as UTF-8
(lldb) p url.ToStdString()
```

---

## 7. Quick checklist when a new crash arrives

1. Copy the `.ips` file from `~/Library/Logs/DiagnosticReports/`.
2. Find the **Thread N Crashed** section and read the top 5 frames.
3. Note the **Exception Subtype** address — near-zero means null pointer.
4. Identify which frame is in `test-taker` (not a library) — that is the bug site.
5. If the crash address is an offset from a class member, work out which field
   (count struct field sizes or use `offsetof` in a quick test).
6. Build debug, run under lldb, set a breakpoint just before the crash site, and
   inspect the `this` pointer and arguments.
7. Write a **failing unit test** that reproduces the invariant violation before fixing.
   (If the crash is in wx UI code that can't run headlessly, document why in the PR.)
