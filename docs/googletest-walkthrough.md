# GoogleTest Walkthrough — Porting a test from this project's hand-rolled runner

## Background

This project uses a hand-rolled test runner. Every test file exports one function
(`int test_markdown()`, etc.) that returns a failure count, and `tests/main.cpp`
calls them all. This document walks through converting one of those tests to
GoogleTest (gtest) step by step.

---

## Step 1 — Pick the test to port

We use the fenced code block test from `tests/test_markdown.cpp` (lines 60–80).
It is self-contained: one string in, one HTML string checked. No filesystem, no threads.

**Original (hand-rolled):**

```cpp
{
    std::string md =
        "Here is an example:\n\n"
        "```cpp\n"
        "int x = 42;\n"
        "```\n";
    std::string html = RenderMarkdown(md);
    bool hasPre       = html.find("<pre>")        != std::string::npos;
    bool hasLangClass = html.find("language-cpp") != std::string::npos;
    bool hasCode      = html.find("int x = 42")   != std::string::npos;
    if (!hasPre || !hasLangClass || !hasCode) {
        std::cerr << "FAIL [fenced-code-block]: ...\n";
        ++failures;
    } else {
        std::cout << "PASS [fenced-code-block]\n";
    }
}
```

---

## Step 2 — GoogleTest anatomy

```cpp
TEST(SuiteName, CaseName) {
    // arrange
    // act
    // assert
}
```

| Part | What it is |
|---|---|
| `SuiteName` | Groups related tests — think of it like a class name. Use `Markdown`. |
| `CaseName` | The specific behaviour being tested. Use `FencedCodeBlockWithLanguage`. |
| Body | No return value. No `++failures`. The framework owns all of that. |

---

## Step 3 — The EXPECT macros

| Hand-rolled style | GoogleTest equivalent |
|---|---|
| `if (x != y) { ++failures; }` | `EXPECT_EQ(x, y);` |
| `if (!ok) { ++failures; }` | `EXPECT_TRUE(ok);` |
| `if (s.find("x") == npos) { ++failures; }` | `EXPECT_NE(s.find("x"), std::string::npos);` |

**`EXPECT_*`** — records the failure and continues the rest of the test.  
**`ASSERT_*`** — records the failure and stops the current test immediately.
Use `ASSERT_*` when later assertions depend on an earlier one being true
(e.g. asserting a pointer is non-null before dereferencing it).

The `<< "message"` suffix on any macro prints only when that assertion fails,
replacing the `[fenced-code-block]` label from the hand-rolled style:

```cpp
EXPECT_NE(html.find("<pre>"), std::string::npos) << "missing <pre>";
```

---

## Step 4 — Write the gtest file

```cpp
// tests/gtest_markdown.cpp
#include <gtest/gtest.h>
#include "markdown.h"

TEST(Markdown, FencedCodeBlockWithLanguage) {
    std::string md =
        "Here is an example:\n\n"
        "```cpp\n"
        "int x = 42;\n"
        "```\n";

    std::string html = RenderMarkdown(md);

    EXPECT_NE(html.find("<pre>"),        std::string::npos) << "missing <pre>";
    EXPECT_NE(html.find("language-cpp"), std::string::npos) << "missing language class";
    EXPECT_NE(html.find("int x = 42"),  std::string::npos) << "missing code content";
}
```

Three things to notice:
1. Include `<gtest/gtest.h>` — the only new header.
2. `EXPECT_NE(a, b)` replaces the `find() != npos` boolean dance.
3. The `<< "..."` after each `EXPECT_NE` is the failure label.

---

## Step 5 — The main()

GoogleTest provides its own `main`. You only need this file once per binary:

```cpp
// tests/gtest_main.cpp
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

`InitGoogleTest` parses flags like `--gtest_filter`.  
`RUN_ALL_TESTS` discovers and runs every `TEST()` in the binary automatically —
no registration list like the hand-rolled `main.cpp`.

---

## Step 6 — Wire it into CMake

```cmake
# Option A — use an already-built system install
find_package(GTest REQUIRED)

# Option B — vendored source (what this project has under
#   /Users/rueda/test-taker/GoogleTest/GoogleTest1-TestFramework/corpus/googletest)
add_subdirectory(/path/to/googletest EXCLUDE_FROM_ALL)

# Either way, the rest is the same:
add_executable(gtest_markdown
    tests/gtest_markdown.cpp
    tests/gtest_main.cpp
    src/markdown.cpp            # production code under test
)

target_include_directories(gtest_markdown PRIVATE src)
target_link_libraries(gtest_markdown PRIVATE GTest::gtest)

include(GoogleTest)
gtest_discover_tests(gtest_markdown)  # registers each TEST() with ctest
```

---

## Step 7 — Build and run

```bash
cmake --build build --target gtest_markdown
./build/gtest_markdown
```

**Passing output:**

```
[==========] Running 1 test from 1 test suite.
[----------] 1 test from Markdown
[ RUN      ] Markdown.FencedCodeBlockWithLanguage
[       OK ] Markdown.FencedCodeBlockWithLanguage (0 ms)
[==========] 1 test ran. (0 ms total)
[  PASSED  ] 1 test.
```

**Failing output** (e.g. `<pre>` missing):

```
[ RUN      ] Markdown.FencedCodeBlockWithLanguage
tests/gtest_markdown.cpp:12: Failure
Value of: html.find("<pre>")
  Actual: 18446744073709551615
Expected: not std::string::npos
missing <pre>
[  FAILED  ] Markdown.FencedCodeBlockWithLanguage (0 ms)
```

The failure message gives you: file, line number, actual value, expected value,
and your custom label — all automatically.

**Filter to one test:**

```bash
./build/gtest_markdown --gtest_filter=Markdown.FencedCodeBlockWithLanguage
```

**Filter to a whole suite:**

```bash
./build/gtest_markdown --gtest_filter=Markdown.*
```

---

## Comparison summary

| Hand-rolled runner | GoogleTest |
|---|---|
| `int test_foo()` returns failure count | `TEST(Suite, Case)` void macro |
| Manual `if (!ok) ++failures` | `EXPECT_*` / `ASSERT_*` macros |
| `main.cpp` calls each function by name | `RUN_ALL_TESTS()` discovers automatically |
| `std::cerr << "FAIL [label]"` | `<< "label"` appended to any EXPECT |
| One binary, all suites registered manually | Each `TEST()` auto-registered; filter at runtime |

---

## Quick reference — common EXPECT macros

| Macro | Passes when |
|---|---|
| `EXPECT_TRUE(x)` | `x` is true |
| `EXPECT_FALSE(x)` | `x` is false |
| `EXPECT_EQ(a, b)` | `a == b` |
| `EXPECT_NE(a, b)` | `a != b` |
| `EXPECT_LT(a, b)` | `a < b` |
| `EXPECT_LE(a, b)` | `a <= b` |
| `EXPECT_GT(a, b)` | `a > b` |
| `EXPECT_GE(a, b)` | `a >= b` |
| `EXPECT_STREQ(a, b)` | C-strings are equal (`strcmp == 0`) |
| `EXPECT_THROW(expr, ExcType)` | `expr` throws `ExcType` |
| `EXPECT_NO_THROW(expr)` | `expr` throws nothing |
