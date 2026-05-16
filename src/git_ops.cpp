#include "git_ops.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string shell_quote(const std::string& s) {
    std::string q = "'";
    for (char c : s) {
        if (c == '\'') q += "'\\''";
        else q += c;
    }
    return q + "'";
}

static std::string escape_html(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else                out += c;
    }
    return out;
}

struct CmdResult { std::string output; int rc; };

static CmdResult run_git(const std::string& args, const std::string& dir) {
    std::string cmd = "git -C " + shell_quote(dir) + " " + args + " 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return {"", -1};
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    int status = pclose(p);
    return {out, WEXITSTATUS(status)};
}

static std::vector<GitCommit> parse_git_log(const std::string& output) {
    std::vector<GitCommit> commits;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        GitCommit c;
        auto p1 = line.find('\x1F');
        if (p1 == std::string::npos) continue;
        c.hash = line.substr(0, p1);
        auto p2 = line.find('\x1F', p1 + 1);
        if (p2 == std::string::npos) continue;
        c.shortHash = line.substr(p1 + 1, p2 - p1 - 1);
        auto p3 = line.find('\x1F', p2 + 1);
        if (p3 == std::string::npos) continue;
        c.date    = line.substr(p2 + 1, p3 - p2 - 1);
        c.subject = line.substr(p3 + 1);
        commits.push_back(c);
    }
    return commits;
}

// ── public API ────────────────────────────────────────────────────────────────

bool GitInit(const std::string& dir) {
    return run_git("init", dir).rc == 0;
}

bool GitCommitFile(const std::string& projectDir, const std::string& relPath,
                   const std::string& message) {
    if (run_git("add " + shell_quote(relPath), projectDir).rc != 0) return false;
    return run_git("commit -m " + shell_quote(message), projectDir).rc == 0;
}

std::vector<GitCommit> GitLogFile(const std::string& projectDir,
                                  const std::string& relPath) {
    // Use unit separator (\x1F) as field delimiter to handle | in subjects.
    auto r = run_git("log --format=%H%x1F%h%x1F%as%x1F%s -- " + shell_quote(relPath),
                     projectDir);
    return parse_git_log(r.output);
}

std::vector<GitCommit> GitLogProject(const std::string& projectDir) {
    // Use unit separator (\x1F) as field delimiter to handle | in subjects.
    auto r = run_git("log --all --format=%H%x1F%h%x1F%as%x1F%s", projectDir);
    return parse_git_log(r.output);
}

std::string GitShowFile(const std::string& projectDir, const std::string& hash,
                        const std::string& relPath) {
    auto r = run_git("show " + hash + ":" + shell_quote(relPath), projectDir);
    return r.rc == 0 ? r.output : "";
}

bool GitRestoreFile(const std::string& projectDir, const std::string& hash,
                    const std::string& relPath) {
    return run_git("checkout " + hash + " -- " + shell_quote(relPath), projectDir).rc == 0;
}

bool GitCheckoutCommit(const std::string& projectDir, const std::string& hash) {
    return run_git("checkout " + shell_quote(hash), projectDir).rc == 0;
}

bool GitStashProject(const std::string& projectDir, const std::string& message) {
    auto r = run_git("stash push -u -m " + shell_quote(message), projectDir);
    return r.rc == 0;
}

bool GitUnstashProject(const std::string& projectDir) {
    return run_git("stash pop", projectDir).rc == 0;
}

std::string GitDiffHTML(const std::string& projectDir,
                        const std::string& hash1, const std::string& hash2,
                        const std::string& relPath) {
    std::string args = "diff " + hash1;
    if (!hash2.empty()) args += " " + hash2;
    args += " -- " + shell_quote(relPath);
    auto r = run_git(args, projectDir);

    std::string label1 = hash1.size() >= 7 ? hash1.substr(0, 7) : hash1;
    std::string label2 = hash2.empty() ? "working copy"
                       : (hash2.size() >= 7 ? hash2.substr(0, 7) : hash2);
    std::string title  = label1 + " → " + label2;

    std::ostringstream html;
    html <<
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<title>Diff: " << escape_html(title) << "</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:'SFMono-Regular',Consolas,monospace;font-size:13px;"
             "background:#fff;color:#24292e;padding:0}"
        "h2{font-size:14px;font-weight:600;padding:14px 20px;"
            "border-bottom:1px solid #e1e4e8;background:#f6f8fa}"
        "p.nd{padding:20px;color:#6a737d}"
        "table{width:100%;border-collapse:collapse}"
        "td{padding:1px 10px;white-space:pre-wrap;word-break:break-all;vertical-align:top}"
        ".ln{color:#bbb;text-align:right;user-select:none;min-width:52px;"
             "border-right:1px solid #e1e4e8;padding-right:8px;font-size:11px}"
        ".add{background:#e6ffed}.add .code{color:#22863a}"
        ".del{background:#ffeef0}.del .code{color:#b31d28}"
        ".hunk{background:#f1f8ff}.hunk td{color:#005cc5;font-style:italic}"
        ".hdr td{color:#999;background:#f6f8fa}"
        "tr:hover{filter:brightness(0.96)}"
        "</style></head><body>"
        "<h2>Diff — " << escape_html(relPath) << ": "
            << escape_html(label1) << " → " << escape_html(label2) << "</h2>";

    if (r.output.empty() || r.output.find("@@") == std::string::npos) {
        html << "<p class='nd'>No differences between these versions.</p>";
    } else {
        html << "<table>";
        std::istringstream ss(r.output);
        std::string line;
        int ln_old = 0, ln_new = 0;
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            char c = line[0];
            if (c == 'd' || c == 'i' || c == 'n' ||
                line.rfind("---", 0) == 0 || line.rfind("+++", 0) == 0) {
                html << "<tr class='hdr'><td class='ln'></td>"
                        "<td class='code'>" << escape_html(line) << "</td></tr>";
            } else if (line.rfind("@@", 0) == 0) {
                int os = 0, ns = 0;
                sscanf(line.c_str(), "@@ -%d,%*d +%d", &os, &ns);
                ln_old = os > 0 ? os : 1;
                ln_new = ns > 0 ? ns : 1;
                html << "<tr class='hunk'><td class='ln'></td>"
                        "<td class='code'>" << escape_html(line) << "</td></tr>";
            } else if (c == '-') {
                html << "<tr class='del'><td class='ln'>" << ln_old++
                     << "</td><td class='code'>" << escape_html(line) << "</td></tr>";
            } else if (c == '+') {
                html << "<tr class='add'><td class='ln'>" << ln_new++
                     << "</td><td class='code'>" << escape_html(line) << "</td></tr>";
            } else if (c == ' ') {
                html << "<tr><td class='ln'>" << ln_old++ << "/" << ln_new++
                     << "</td><td class='code'>" << escape_html(line) << "</td></tr>";
            } else {
                html << "<tr class='hdr'><td class='ln'></td>"
                        "<td class='code'>" << escape_html(line) << "</td></tr>";
            }
        }
        html << "</table>";
    }
    html << "</body></html>";
    return html.str();
}
