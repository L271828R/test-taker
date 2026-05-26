#include "html_template.h"
#include "markdown.h"
#include <sstream>
#include <vector>
#include <iomanip>
#include "mermaid_js.h"
#include "hljs_js.h"
#include "hljs_css_light.h"
#include "hljs_css_dark.h"
#include "plotly_js.h"
#include <cstdio>
#include <string>

static const std::string& GetMermaidJS() {
    static std::string cached;
    if (cached.empty()) {
        cached.assign(reinterpret_cast<const char*>(mermaid_js_data), mermaid_js_data_len);
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

static const std::string& GetHighlightJS() {
    static std::string cached;
    if (cached.empty()) {
        cached.assign(reinterpret_cast<const char*>(hljs_js_data), hljs_js_data_len);
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

static const std::string& GetPlotlyJS() {
    static std::string cached;
    if (cached.empty()) {
        cached.assign(reinterpret_cast<const char*>(plotly_js_data), plotly_js_data_len);
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

static const std::string& GetHighlightCSSLight() {
    static std::string cached;
    if (cached.empty())
        cached.assign(reinterpret_cast<const char*>(hljs_css_light_data), hljs_css_light_data_len);
    return cached;
}

static const std::string& GetHighlightCSSDark() {
    static std::string cached;
    if (cached.empty())
        cached.assign(reinterpret_cast<const char*>(hljs_css_dark_data), hljs_css_dark_data_len);
    return cached;
}

std::string BuildHTML(const std::string& body,
                      const std::string& title,
                      bool darkMode,
                      int fontSizePercent) {
    const std::string htmlClass = darkMode ? " class=\"dark\"" : "";
    char fsBuf[32];
    snprintf(fsBuf, sizeof(fsBuf), "%.4g", 16.0 * fontSizePercent / 100.0);
    const std::string fsPx       = std::string(fsBuf) + "px";
    const std::string fontPctStr = std::to_string(fontSizePercent);

    return R"HTML(<!DOCTYPE html>
<html lang="en")HTML" + htmlClass + R"HTML(>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>)HTML" + EscapeHTML(title) + R"HTML(</title>
<script>)HTML" + GetMermaidJS() + R"HTML(</script>
<script>)HTML" + GetHighlightJS() + R"HTML(</script>
<script>)HTML" + GetPlotlyJS() + R"HTML(</script>
<style>)HTML" + (darkMode ? GetHighlightCSSDark() : GetHighlightCSSLight()) + R"HTML(</style>
<style>
/* ── Theme tokens ───────────────────────────────────────────────────────── */
:root {
  --bg:            #ffffff;
  --surface:       #f6f8fa;
  --surface2:      #fafbfc;
  --text:          #24292f;
  --text-muted:    #57606a;
  --border:        #d0d7de;
  --link:          #0969da;
  --link-hover:    #0550ae;
  --code-inline:   rgba(175,184,193,0.2);
  --del:           #656d76;
  --zm-svg-bg:     #ffffff;
  --mermaid-hover: rgba(9,105,218,0.16);
  --mermaid-ring:  rgba(9,105,218,0.53);
}
.dark {
  --bg:            #0d1117;
  --surface:       #161b22;
  --surface2:      #1c2128;
  --text:          #e6edf3;
  --text-muted:    #8b949e;
  --border:        #30363d;
  --link:          #58a6ff;
  --link-hover:    #79c0ff;
  --code-inline:   rgba(110,118,129,0.4);
  --del:           #8b949e;
  --zm-svg-bg:     #1e2430;
  --mermaid-hover: rgba(88,166,255,0.12);
  --mermaid-ring:  rgba(88,166,255,0.45);
}

/* ── Reset & base ───────────────────────────────────────────────────────── */
*{box-sizing:border-box;margin:0;padding:0}
body{
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
  font-size:)HTML" + fsPx + R"HTML(;line-height:1.65;
  color:var(--text);background:var(--bg);
  padding:32px 24px;max-width:960px;margin:0 auto;
  overflow-wrap:anywhere;word-break:break-word;
}

/* ── Typography ─────────────────────────────────────────────────────────── */
h1,h2,h3,h4,h5,h6{margin:24px 0 16px;font-weight:600;line-height:1.25;color:var(--text)}
h1{font-size:2em;  border-bottom:1px solid var(--border);padding-bottom:.3em}
h2{font-size:1.5em;border-bottom:1px solid var(--border);padding-bottom:.3em}
h3{font-size:1.25em}
h4{font-size:1em}
p{margin-bottom:16px;overflow-wrap:anywhere;word-break:break-word}
a{color:var(--link);text-decoration:none}
a:hover{color:var(--link-hover);text-decoration:underline}
strong{font-weight:600}
del{color:var(--del)}

/* ── Code ───────────────────────────────────────────────────────────────── */
code{
  font-family:'SFMono-Regular',Consolas,'Liberation Mono',Menlo,monospace;
  font-size:85%;background:var(--code-inline);
  padding:.2em .4em;border-radius:3px;color:var(--text);
}
pre{
  background:var(--surface);border:1px solid var(--border);
  border-radius:6px;padding:16px;overflow-x:auto;margin-bottom:16px;
}
pre code{background:none;padding:0;font-size:87.5%;border:none}
.hljs{background:transparent}
.code-wrapper{position:relative;display:block;margin-bottom:16px}
.code-wrapper pre{margin-bottom:0}
.copy-btn{
  position:absolute;top:8px;right:8px;
  padding:3px 9px;font-size:11px;line-height:1.5;
  background:var(--surface2);border:1px solid var(--border);
  border-radius:4px;cursor:pointer;color:var(--text-muted);
  opacity:0;transition:opacity .15s,background .15s;
}
.code-wrapper:hover .copy-btn{opacity:1}
.copy-btn:hover{background:var(--border)}
.copy-btn.copied{color:var(--link)}

/* ── Blockquote ─────────────────────────────────────────────────────────── */
blockquote{
  padding:0 1em;color:var(--text-muted);
  border-left:.25em solid var(--border);margin-bottom:16px;
}

/* ── Tidbit ─────────────────────────────────────────────────────────────── */
details.tidbit{
  border:1px solid var(--border);border-radius:6px;
  margin-bottom:16px;background:var(--surface);
}
details.tidbit summary{
  padding:10px 14px;cursor:pointer;font-style:italic;
  color:var(--text-muted);user-select:none;list-style:none;
}
details.tidbit summary::before{content:"💬 "}
details.tidbit summary::-webkit-details-marker{display:none}
.tb-body-row{display:flex;gap:14px;align-items:flex-start}
.tb-body-text{flex:1;min-width:0}
.tb-avatar-lg{flex-shrink:0;width:80px;height:80px;border-radius:50%;object-fit:cover;
  border:2px solid var(--border);box-shadow:0 2px 8px rgba(0,0,0,.18);display:block}
details.tidbit[open] summary{border-bottom:1px solid var(--border)}
.tidbit-body{padding:12px 16px}
.tidbit-body p:last-child{margin-bottom:0}
/* carousel replaces stacked tidbits when count > 1 */
.tidbit-carousel{
  border:1px solid var(--border);border-radius:6px;
  margin-bottom:16px;background:var(--surface);
}
.tidbit-carousel-header{
  display:flex;justify-content:space-between;align-items:center;
  padding:10px 14px;font-style:italic;color:var(--text-muted);user-select:none;
}
.tidbit-carousel-nav{display:flex;align-items:center;gap:6px}
.tidbit-carousel-counter{font-size:.8em;opacity:.75}
.tidbit-carousel-arrow{
  background:none;border:1px solid var(--border);border-radius:4px;
  color:var(--text-muted);cursor:pointer;font-size:1.1em;
  padding:0 7px;line-height:1.5;transition:color .12s,border-color .12s;
}
.tidbit-carousel-arrow:hover{color:var(--text);border-color:var(--text-muted)}

/* ── Conversation ───────────────────────────────────────────────────────── */
details.conversation{
  border:1px solid var(--border);border-radius:6px;
  margin-bottom:16px;background:var(--surface);
}
details.conversation summary{
  padding:10px 14px;cursor:pointer;font-style:italic;
  color:var(--text-muted);user-select:none;list-style:none;
}
details.conversation summary::-webkit-details-marker{display:none}
details.conversation[open] summary{border-bottom:1px solid var(--border)}
.conversation-body{padding:12px 16px}
.qa-turn{margin-bottom:16px}
.qa-turn:last-child{margin-bottom:0}
.qa-q{background:var(--surface2);border-radius:6px 6px 6px 2px;
  padding:8px 12px;margin-bottom:6px;font-weight:500;color:var(--text)}
.qa-a{background:var(--bg);border:1px solid var(--border);
  border-radius:2px 6px 6px 6px;padding:8px 12px;color:var(--text)}
.chat-btn{
  margin-left:10px;background:none;border:none;cursor:pointer;
  font-size:0.85em;opacity:0.4;transition:opacity .15s;vertical-align:middle;
  padding:0 4px;color:inherit;
}
h2:hover .chat-btn{opacity:1}

/* ── Notes ─────────────────────────────────────────────────────────────── */
.note-marker{
  display:inline-block;font-size:0.7em;vertical-align:super;
  cursor:pointer;user-select:none;margin-left:1px;
  color:var(--link);line-height:1;
}
.note-popover{
  position:fixed;z-index:5000;
  background:var(--surface);border:1px solid var(--border);
  border-radius:8px;padding:12px 14px;max-width:320px;
  box-shadow:0 4px 16px rgba(0,0,0,.18);font-size:0.88em;
  color:var(--text);line-height:1.5;
}
.note-popover-text{margin-bottom:10px;white-space:pre-wrap}
.note-popover-actions{display:flex;gap:8px;justify-content:flex-end}
.note-popover-actions button{
  background:none;border:1px solid var(--border);border-radius:4px;
  padding:3px 10px;cursor:pointer;font-size:0.9em;color:var(--text-muted);
}
.note-popover-actions button:hover{background:var(--border)}
/* Selection toolbar */
#note-toolbar{
  position:fixed;z-index:4999;display:none;
  background:var(--surface);border:1px solid var(--border);
  border-radius:6px;padding:4px 8px;
  box-shadow:0 2px 8px rgba(0,0,0,.15);
  cursor:pointer;font-size:0.85em;color:var(--text);
  white-space:nowrap;user-select:none;
}
#note-toolbar:hover{background:var(--border)}

/* ── Lists ──────────────────────────────────────────────────────────────── */
ul,ol{padding-left:2em;margin-bottom:16px}
li{margin:4px 0;color:var(--text)}
li,summary,.tidbit-body{overflow-wrap:anywhere;word-break:break-word}

/* ── Media ──────────────────────────────────────────────────────────────── */
img{max-width:100%;height:auto;border-radius:4px}

/* ── Rule ───────────────────────────────────────────────────────────────── */
hr{height:.25em;padding:0;margin:24px 0;background:var(--border);border:0}

/* ── Table ──────────────────────────────────────────────────────────────── */
table{border-collapse:collapse;margin-bottom:16px;width:100%}
th,td{border:1px solid var(--border);padding:6px 13px;text-align:left;color:var(--text)}
th{background:var(--surface);font-weight:600}
tr:nth-child(even) td{background:var(--surface)}

/* ── Mermaid wrapper ────────────────────────────────────────────────────── */
.mermaid-wrapper{
  display:inline-block;cursor:zoom-in;
  border:1px solid var(--border);border-radius:6px;
  padding:20px;margin-bottom:16px;
  background:var(--surface2);
  transition:box-shadow .18s,border-color .18s;
  width:100%;text-align:center;
}
.mermaid-wrapper:hover{
  box-shadow:0 0 0 3px var(--mermaid-hover);
  border-color:var(--mermaid-ring);
}

/* ── Zoom modal ─────────────────────────────────────────────────────────── */
#zm-overlay{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,.88);
  z-index:9999;align-items:center;justify-content:center;
}
#zm-overlay.open{display:flex}
#zm-stage{
  position:relative;width:90vw;height:90vh;
  overflow:hidden;cursor:grab;user-select:none;
}
#zm-stage.dragging{cursor:grabbing}
#zm-inner{
  display:flex;align-items:center;justify-content:center;
  width:100%;height:100%;transform-origin:center center;
}
#zm-inner svg{
  max-width:85vw;max-height:85vh;
  background:var(--zm-svg-bg);border-radius:8px;padding:24px;
  box-shadow:0 8px 32px rgba(0,0,0,.5);
}
#zm-close{
  position:fixed;top:14px;right:18px;background:rgba(255,255,255,.12);
  border:none;color:#fff;font-size:20px;width:36px;height:36px;
  border-radius:50%;cursor:pointer;z-index:10001;
  display:flex;align-items:center;justify-content:center;
  transition:background .15s;
}
#zm-close:hover{background:rgba(255,255,255,.28)}
#zm-hint{
  position:fixed;bottom:14px;left:50%;transform:translateX(-50%);
  color:rgba(255,255,255,.55);font-size:12px;pointer-events:none;white-space:nowrap;
}
#zm-scale{
  position:fixed;top:16px;left:50%;transform:translateX(-50%);
  color:rgba(255,255,255,.7);font-size:12px;background:rgba(0,0,0,.4);
  padding:3px 10px;border-radius:20px;pointer-events:none;
}
</style>
<script>
// Stubs so RunScript calls never throw "Can't find variable" if the exam
// input section isn't present (e.g. the page is still loading or not active).
// BuildExamInputSection overrides these with the real implementations.
function setBusy(msg){}
function showHint(text){}
</script>
</head>
<body>
)HTML" + body + R"HTML(

<!-- ── Zoom modal ───────────────────────────────────────────────────────── -->
<div id="zm-overlay">
  <button id="zm-close" title="Close (Esc)">&#x2715;</button>
  <div id="zm-stage">
    <div id="zm-inner"></div>
  </div>
  <div id="zm-hint">Scroll to zoom &nbsp;·&nbsp; Drag to pan &nbsp;·&nbsp; ESC to close</div>
  <div id="zm-scale">100%</div>
</div>

<script>
// ── Highlight.js ─────────────────────────────────────────────────────────
hljs.highlightAll();

// ── Copy buttons on fenced code blocks ───────────────────────────────────
document.querySelectorAll('pre code').forEach(function(block) {
  var pre = block.parentElement;
  var wrapper = document.createElement('div');
  wrapper.className = 'code-wrapper';
  pre.parentNode.insertBefore(wrapper, pre);
  wrapper.appendChild(pre);
  var btn = document.createElement('button');
  btn.className = 'copy-btn';
  btn.textContent = 'Copy';
  btn.addEventListener('click', function() {
    var text = block.innerText;
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.clipboardCopy) {
      window.webkit.messageHandlers.clipboardCopy.postMessage(text);
    }
    btn.textContent = 'Copied!';
    btn.classList.add('copied');
    setTimeout(function() { btn.textContent = 'Copy'; btn.classList.remove('copied'); }, 1500);
  });
  wrapper.appendChild(btn);
});

// ── Mermaid init ─────────────────────────────────────────────────────────
mermaid.initialize({startOnLoad:true,
  theme: document.documentElement.classList.contains('dark') ? 'dark' : 'default',
  securityLevel:'loose'});

// ── Chapter chat buttons ─────────────────────────────────────────────────
document.querySelectorAll('h2[data-ch-id]').forEach(function(h2) {
  var btn = document.createElement('button');
  btn.className = 'chat-btn';
  btn.textContent = '💬';
  btn.title = 'Ask about this chapter';
  btn.addEventListener('click', function(e) {
    e.stopPropagation();
    var id = h2.getAttribute('data-ch-id');
    var title = h2.textContent.replace('💬','').trim();
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.chat)
      window.webkit.messageHandlers.chat.postMessage(id + '|' + title);
  });
  h2.appendChild(btn);
});

// ── Tidbit carousel: group consecutive tidbits, show one at a time with › ──
(function() {
  var all = Array.from(document.querySelectorAll('details.tidbit'));
  var visited = new Set();
  all.forEach(function(el) {
    if (visited.has(el)) return;
    var group = [el];
    var sib = el.nextElementSibling;
    while (sib && sib.classList && sib.classList.contains('tidbit')) {
      group.push(sib); sib = sib.nextElementSibling;
    }
    group.forEach(function(e) { visited.add(e); });
    if (group.length < 2) return;

    var speakers = group.map(function(d) {
      var s = d.querySelector('summary'); return s ? s.textContent.trim() : '';
    });
    var bodies = group.map(function(d) {
      var b = d.querySelector('.tidbit-body'); return b ? b.innerHTML : '';
    });

    var carousel = document.createElement('div');
    carousel.className = 'tidbit-carousel';
    carousel.dataset.cur = '0';

    var hdr = document.createElement('div');
    hdr.className = 'tidbit-carousel-header';

    var spk = document.createElement('span');
    spk.textContent = '💬 ' + speakers[0];

    var nav = document.createElement('span');
    nav.className = 'tidbit-carousel-nav';

    var ctr = document.createElement('span');
    ctr.className = 'tidbit-carousel-counter';
    ctr.textContent = '1 / ' + group.length;

    var arr = document.createElement('button');
    arr.className = 'tidbit-carousel-arrow';
    arr.textContent = '›';

    nav.appendChild(ctr); nav.appendChild(arr);
    hdr.appendChild(spk); hdr.appendChild(nav);
    carousel.appendChild(hdr);

    var panels = bodies.map(function(html, idx) {
      var p = document.createElement('div');
      p.className = 'tidbit-body';
      p.innerHTML = html;
      if (idx > 0) p.hidden = true;
      carousel.appendChild(p);
      return p;
    });

    arr.addEventListener('click', function() {
      var cur = parseInt(carousel.dataset.cur);
      panels[cur].hidden = true;
      cur = (cur + 1) % group.length;
      panels[cur].hidden = false;
      carousel.dataset.cur = cur;
      spk.textContent = '💬 ' + speakers[cur];
      ctr.textContent = (cur + 1) + ' / ' + group.length;
    });

    el.parentNode.insertBefore(carousel, el);
    group.forEach(function(e) { e.parentNode.removeChild(e); });
  });
})();

// ── Zoom state ───────────────────────────────────────────────────────────
let zmScale = 1, zmTX = 0, zmTY = 0;
let zmDrag = false, zmDX = 0, zmDY = 0;
const zmInner = document.getElementById('zm-inner');
const zmStage = document.getElementById('zm-stage');
const zmScaleLabel = document.getElementById('zm-scale');

function zmApply() {
  zmInner.style.transform = `translate(${zmTX}px,${zmTY}px) scale(${zmScale})`;
  zmScaleLabel.textContent = Math.round(zmScale * 100) + '%';
}

function zmOpen(svgHTML) {
  zmScale=1; zmTX=0; zmTY=0;
  zmInner.innerHTML = svgHTML;
  const svg = zmInner.querySelector('svg');
  if (svg) {
    svg.removeAttribute('width');
    svg.removeAttribute('height');
    svg.style.maxWidth  = '85vw';
    svg.style.maxHeight = '85vh';
  }
  zmApply();
  document.getElementById('zm-overlay').classList.add('open');
  document.body.style.overflow = 'hidden';
}

function zmClose() {
  document.getElementById('zm-overlay').classList.remove('open');
  document.body.style.overflow = '';
}

// Click on any mermaid wrapper → zoom (event delegation, works after async render)
document.addEventListener('click', e => {
  if (e.target.closest('#zm-overlay')) return;
  const w = e.target.closest('.mermaid-wrapper');
  if (w) { const svg = w.querySelector('svg'); if (svg) zmOpen(svg.outerHTML); }
});

document.getElementById('zm-close').addEventListener('click', zmClose);
document.getElementById('zm-overlay').addEventListener('click', e => {
  if (e.target === document.getElementById('zm-overlay')) zmClose();
});

document.addEventListener('keydown', e => { if (e.key === 'Escape') zmClose(); });

zmStage.addEventListener('wheel', e => {
  e.preventDefault();
  zmScale = Math.min(Math.max(zmScale * (e.deltaY < 0 ? 1.12 : 0.9), 0.08), 20);
  zmApply();
}, {passive:false});

zmStage.addEventListener('mousedown', e => {
  if (e.button !== 0) return;
  zmDrag = true; zmDX = e.clientX - zmTX; zmDY = e.clientY - zmTY;
  zmStage.classList.add('dragging'); e.preventDefault();
});
document.addEventListener('mousemove', e => {
  if (!zmDrag) return;
  zmTX = e.clientX - zmDX; zmTY = e.clientY - zmDY; zmApply();
});
document.addEventListener('mouseup', () => {
  zmDrag = false; zmStage.classList.remove('dragging');
});

let lastDist = 0;
zmStage.addEventListener('touchstart', e => {
  if (e.touches.length === 1) {
    zmDrag = true; zmDX = e.touches[0].clientX - zmTX; zmDY = e.touches[0].clientY - zmTY;
  } else if (e.touches.length === 2) {
    zmDrag = false;
    lastDist = Math.hypot(e.touches[0].clientX - e.touches[1].clientX,
                          e.touches[0].clientY - e.touches[1].clientY);
  }
  e.preventDefault();
}, {passive:false});
zmStage.addEventListener('touchmove', e => {
  if (e.touches.length === 1 && zmDrag) {
    zmTX = e.touches[0].clientX - zmDX; zmTY = e.touches[0].clientY - zmDY; zmApply();
  } else if (e.touches.length === 2) {
    const d = Math.hypot(e.touches[0].clientX - e.touches[1].clientX,
                         e.touches[0].clientY - e.touches[1].clientY);
    if (lastDist > 0) {
      zmScale = Math.min(Math.max(zmScale * (d / lastDist), 0.08), 20); zmApply();
    }
    lastDist = d;
  }
  e.preventDefault();
}, {passive:false});
zmStage.addEventListener('touchend', () => { zmDrag = false; lastDist = 0; });

// Ctrl/Meta+wheel — adjust document font size
(function(){
  var pct = )HTML" + fontPctStr + R"HTML(;
  document.addEventListener('wheel', function(e){
    if (!e.ctrlKey && !e.metaKey) return;
    e.preventDefault();
    pct = Math.max(50, Math.min(200, pct + (e.deltaY < 0 ? 10 : -10)));
    document.body.style.fontSize = (16 * pct / 100) + 'px';
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.fontSizeChange)
      window.webkit.messageHandlers.fontSizeChange.postMessage(String(pct));
  }, {passive:false});
})();

// ── Math graphs (Plotly) ─────────────────────────────────────────────────
(function() {
  if (typeof Plotly === 'undefined') return;
  var COLORS = ['#1f77b4','#ff7f0e','#2ca02c','#d62728','#9467bd','#8c564b'];
  function toJS(expr) {
    return expr.trim()
      .replace(/\^{([^}]*)}/g, '**($1)')
      .replace(/\^(-?[0-9.]+)/g, '**($1)')
      .replace(/\\frac{([^}]*)}{([^}]*)}/g, '(($1)/($2))')
      .replace(/\\sqrt{([^}]*)}/g, 'sqrt($1)')
      .replace(/\\ln/g, 'log').replace(/\\log/g, 'log10')
      .replace(/\\sin/g, 'sin').replace(/\\cos/g, 'cos').replace(/\\tan/g, 'tan')
      .replace(/\\pi/g, 'PI').replace(/\\e\b/g, 'E');
  }
  document.querySelectorAll('.math-graph').forEach(function(el) {
    el.style.height = el.style.height || '360px';
    var exprs = (el.dataset.exprs || '').split('|').map(function(e){return e.trim();}).filter(Boolean);
    var isDark = document.documentElement.classList.contains('dark');
    var bg   = isDark ? '#1e1e2e' : '#ffffff';
    var grid = isDark ? '#313244' : '#e8e8e8';
    var zero = isDark ? '#585b70' : '#aaaaaa';
    var fg   = isDark ? '#cdd6f4' : '#24292f';
    var traces = exprs.map(function(expr, i) {
      var fn;
      try { fn = new Function('x', 'with(Math){return ' + toJS(expr) + '}'); } catch(e) { return null; }
      var xs = [], ys = [];
      for (var v = -10; v <= 10; v += 0.04) {
        var y; try { y = fn(v); } catch(e) { y = null; }
        xs.push(v);
        ys.push((typeof y === 'number' && isFinite(y) && Math.abs(y) < 1e6) ? y : null);
      }
      return {x:xs, y:ys, mode:'lines', name:expr, connectgaps:false,
              line:{color:COLORS[i % COLORS.length], width:2.5}};
    }).filter(Boolean);
    var layout = {
      paper_bgcolor:bg, plot_bgcolor:bg,
      font:{color:fg, size:12},
      xaxis:{zeroline:true, zerolinecolor:zero, gridcolor:grid, zerolinewidth:1.5},
      yaxis:{zeroline:true, zerolinecolor:zero, gridcolor:grid, zerolinewidth:1.5},
      margin:{l:50,r:20,t:16,b:40},
      showlegend:exprs.length > 1,
      legend:{bgcolor:'transparent', font:{color:fg}}
    };
    Plotly.newPlot(el, traces, layout, {responsive:true, displayModeBar:false});
  });
})();

// ── Notes ────────────────────────────────────────────────────────────────
(function() {
  // Selection toolbar
  var toolbar = document.createElement('div');
  toolbar.id = 'note-toolbar';
  toolbar.textContent = '📝 Add note';
  document.body.appendChild(toolbar);

  var selTimeout = null;
  document.addEventListener('selectionchange', function() {
    clearTimeout(selTimeout);
    selTimeout = setTimeout(function() {
      var sel = window.getSelection();
      var text = sel ? sel.toString().trim() : '';
      if (!text || text.length < 2) { toolbar.style.display = 'none'; return; }
      var range = sel.getRangeAt(0);
      var rect = range.getBoundingClientRect();
      toolbar.style.display = 'block';
      toolbar.style.left = Math.min(rect.left + window.scrollX, window.innerWidth - 200) + 'px';
      toolbar.style.top  = (rect.top + window.scrollY - 36) + 'px';
    }, 200);
  });

  toolbar.addEventListener('mousedown', function(e) {
    e.preventDefault();
    var sel = window.getSelection();
    if (!sel || !sel.toString().trim()) return;
    var selectedText = sel.toString().trim();
    var range = sel.getRangeAt(0);
    var container = range.startContainer;
    var fullText = container.textContent || '';
    var start = Math.max(0, range.startOffset - 60);
    var end   = Math.min(fullText.length, range.endOffset + 60);
    var context = fullText.substring(start, end);
    toolbar.style.display = 'none';
    sel.removeAllRanges();
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.note)
      window.webkit.messageHandlers.note.postMessage(
        JSON.stringify({action:'add', selectedText:selectedText, context:context}));
  });

  var activePopover = null;
  document.addEventListener('mousedown', function(e) {
    if (activePopover && !activePopover.contains(e.target)) {
      activePopover.remove(); activePopover = null;
    }
    if (!e.target.classList.contains('note-marker'))
      toolbar.style.display = 'none';
  });

  document.addEventListener('click', function(e) {
    var marker = e.target.closest('.note-marker');
    if (!marker) return;
    e.stopPropagation();
    if (activePopover) { activePopover.remove(); activePopover = null; }
    var noteId  = marker.getAttribute('data-note-id');
    var noteText= marker.getAttribute('data-note-text') || '(no text)';
    var pop = document.createElement('div');
    pop.className = 'note-popover';
    pop.innerHTML =
      '<div class="note-popover-text"></div>' +
      '<div class="note-popover-actions">' +
        '<button class="note-edit">✏️ Edit</button>' +
        '<button class="note-delete">🗑️ Delete</button>' +
      '</div>';
    pop.querySelector('.note-popover-text').textContent = noteText;
    var rect = marker.getBoundingClientRect();
    pop.style.left = Math.min(rect.left + window.scrollX, window.innerWidth - 340) + 'px';
    pop.style.top  = (rect.bottom + window.scrollY + 6) + 'px';
    document.body.appendChild(pop);
    activePopover = pop;
    pop.querySelector('.note-edit').addEventListener('click', function() {
      pop.remove(); activePopover = null;
      if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.note)
        window.webkit.messageHandlers.note.postMessage(
          JSON.stringify({action:'edit', id: parseInt(noteId)}));
    });
    pop.querySelector('.note-delete').addEventListener('click', function() {
      pop.remove(); activePopover = null;
      if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.note)
        window.webkit.messageHandlers.note.postMessage(
          JSON.stringify({action:'delete', id: parseInt(noteId)}));
    });
  });
})();
</script>
</body>
</html>
)HTML";
}

// Replace invalid UTF-8 bytes with '?' so wxString::FromUTF8 never sees bad input.
static std::string SanitizeUTF8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const auto* e = p + s.size();
    while (p < e) {
        unsigned char c = *p;
        int extra = 0;
        if      (c < 0x80)              extra = 0;
        else if ((c & 0xE0) == 0xC0)    extra = 1;
        else if ((c & 0xF0) == 0xE0)    extra = 2;
        else if ((c & 0xF8) == 0xF0)    extra = 3;
        else { out += '?'; ++p; continue; }
        bool ok = true;
        for (int j = 0; j < extra; ++j)
            if (p + 1 + j >= e || (p[1 + j] & 0xC0) != 0x80) { ok = false; break; }
        if (!ok) { out += '?'; ++p; continue; }
        for (int j = 0; j <= extra; ++j) out += (char)p[j];
        p += extra + 1;
    }
    return out;
}

std::string BuildLogsHTML(const std::string& rawLog,
                           const std::string& logPath,
                           bool darkMode) {
    const std::string bg      = darkMode ? "#0d1117" : "#ffffff";
    const std::string surface = darkMode ? "#161b22" : "#f6f8fa";
    const std::string border  = darkMode ? "#30363d" : "#d0d7de";
    const std::string text    = darkMode ? "#e6edf3" : "#24292f";
    const std::string muted   = darkMode ? "#8b949e" : "#57606a";
    const std::string green   = darkMode ? "#3fb950" : "#1a7f37";
    const std::string red     = darkMode ? "#f85149" : "#cf222e";

    std::string rows;
    std::istringstream ss(SanitizeUTF8(rawLog));
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::string ts, msg;
        if (line.size() > 21 && line[10] == ' ' && line[19] == ' ') {
            ts  = line.substr(0, 19);
            msg = line.substr(21);
        } else {
            msg = line;
        }
        std::string msgColor = text;
        if (msg.find("FAILED") != std::string::npos || msg.find("error") != std::string::npos)
            msgColor = red;
        else if (msg.find("=== startup") != std::string::npos)
            msgColor = green;

        rows += "<tr>"
                "<td class='ts'>" + EscapeHTML(ts) + "</td>"
                "<td style='color:" + msgColor + "'>" + EscapeHTML(msg) + "</td>"
                "</tr>\n";
    }

    return R"HTML(<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<title>TestTaker — Logs</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'SFMono-Regular',Consolas,monospace;font-size:13px;
     background:)HTML" + bg + R"HTML(;color:)HTML" + text + R"HTML(;padding:24px}
h2{font-size:15px;font-weight:600;margin-bottom:16px;color:)HTML" + text + R"HTML(}
table{width:100%;border-collapse:collapse}
tr{border-bottom:1px solid )HTML" + border + R"HTML(}
tr:last-child{border-bottom:none}
td{padding:5px 10px;vertical-align:top;white-space:pre-wrap;word-break:break-all}
.ts{color:)HTML" + muted + R"HTML(;white-space:nowrap;padding-right:20px;user-select:none}
tr:hover{background:)HTML" + surface + R"HTML(}
</style></head><body>
<h2>TestTaker — Application Log</h2>
<p style="font-size:12px;color:)HTML" + muted + R"HTML(;margin:8px 0 16px">)HTML"
+ EscapeHTML(logPath) + R"HTML(</p>
<table>)HTML" + rows + R"HTML(</table>
</body></html>)HTML";
}

// ---------------------------------------------------------------------------
std::string BuildRagLogsHTML(const std::string& rawLog,
                              const std::string& logPath,
                              bool darkMode) {
    const std::string bg      = darkMode ? "#0d1117" : "#ffffff";
    const std::string surface = darkMode ? "#161b22" : "#f6f8fa";
    const std::string border  = darkMode ? "#30363d" : "#d0d7de";
    const std::string text    = darkMode ? "#e6edf3" : "#24292f";
    const std::string muted   = darkMode ? "#8b949e" : "#57606a";
    const std::string accent  = darkMode ? "#388bfd" : "#0969da";
    const std::string green   = darkMode ? "#3fb950" : "#1a7f37";
    const std::string amber   = darkMode ? "#d29922" : "#9a6700";

    // Parse events from the structured log.
    struct Chunk { std::string score, doc, text; };
    struct Event {
        std::string time, context, query;
        std::vector<Chunk> chunks;
    };

    std::vector<Event> events;
    {
        std::istringstream ss(rawLog);
        std::string line;
        Event cur;
        bool inEvent = false;
        Chunk curChunk;
        bool inChunk = false;

        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line == "RAG_EVENT") {
                inEvent = true; cur = {}; inChunk = false;
            } else if (line == "END_EVENT") {
                if (inChunk && !curChunk.doc.empty()) cur.chunks.push_back(curChunk);
                events.push_back(cur);
                inEvent = false; inChunk = false;
            } else if (!inEvent) {
                // skip lines outside events
            } else if (line.rfind("time=", 0) == 0) {
                cur.time = line.substr(5);
            } else if (line.rfind("context=", 0) == 0) {
                cur.context = line.substr(8);
            } else if (line.rfind("query=", 0) == 0) {
                cur.query = line.substr(6);
            } else if (line.rfind("CHUNK score=", 0) == 0) {
                if (inChunk && !curChunk.doc.empty()) cur.chunks.push_back(curChunk);
                curChunk = {};
                // parse "CHUNK score=0.847 doc=qa_guide.pdf"
                std::string rest = line.substr(12);
                auto docPos = rest.find(" doc=");
                if (docPos != std::string::npos) {
                    curChunk.score = rest.substr(0, docPos);
                    curChunk.doc   = rest.substr(docPos + 5);
                }
                inChunk = true;
            } else if (inChunk) {
                if (!curChunk.text.empty()) curChunk.text += "\n";
                curChunk.text += line;
            }
        }
    }

    // Render events newest-first.
    std::string cards;
    for (int i = static_cast<int>(events.size()) - 1; i >= 0; --i) {
        const auto& ev = events[static_cast<size_t>(i)];

        std::string chunkRows;
        for (size_t j = 0; j < ev.chunks.size(); ++j) {
            const auto& ch = ev.chunks[j];
            // Score badge colour: green ≥0.7, amber ≥0.4, muted below.
            float s = 0.0f;
            try { s = std::stof(ch.score); } catch (...) {}
            std::string badgeColor = s >= 0.7f ? green : (s >= 0.4f ? amber : muted);

            chunkRows +=
                "<div class='chunk'>"
                "<div class='chunk-hdr'>"
                "<span class='score-badge' style='background:" + badgeColor + "'>"
                + EscapeHTML(ch.score) + "</span>"
                "<span class='doc-name'>" + EscapeHTML(ch.doc) + "</span>"
                "<span class='chunk-num'>chunk " + std::to_string(j + 1)
                + " of " + std::to_string(ev.chunks.size()) + "</span>"
                "</div>"
                "<div class='chunk-text'>" + EscapeHTML(ch.text) + "</div>"
                "</div>\n";
        }
        if (chunkRows.empty())
            chunkRows = "<p class='no-chunks'>No chunks retrieved (Ollama unavailable or empty corpus).</p>";

        cards +=
            "<div class='event-card'>"
            "<div class='event-hdr'>"
            "<span class='ctx-badge'>" + EscapeHTML(ev.context) + "</span>"
            "<span class='event-time'>" + EscapeHTML(ev.time) + "</span>"
            "</div>"
            "<div class='query-box'>&#x1F50D;&nbsp;" + EscapeHTML(ev.query) + "</div>"
            + chunkRows +
            "</div>\n";
    }

    if (cards.empty())
        cards = "<p class='empty'>No RAG events logged yet. "
                "Upload documents to the Corpus tab and start a session with &ldquo;Use corpus&rdquo; enabled.</p>";

    return R"HTML(<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<title>TestTaker — RAG Logs</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;font-size:14px;
     background:)HTML" + bg + R"HTML(;color:)HTML" + text + R"HTML(;padding:24px;max-width:960px;margin:0 auto}
h2{font-size:16px;font-weight:600;margin-bottom:4px}
.path{font-size:12px;color:)HTML" + muted + R"HTML(;margin-bottom:20px;font-family:monospace}
.event-card{border:1px solid )HTML" + border + R"HTML(;border-radius:8px;margin-bottom:20px;overflow:hidden}
.event-hdr{background:)HTML" + surface + R"HTML(;padding:8px 12px;display:flex;align-items:center;gap:10px;
            border-bottom:1px solid )HTML" + border + R"HTML(}
.ctx-badge{font-size:11px;font-weight:600;padding:2px 8px;border-radius:12px;
           background:)HTML" + accent + R"HTML(;color:#fff}
.event-time{font-size:12px;color:)HTML" + muted + R"HTML(;font-family:monospace}
.query-box{padding:10px 14px;font-weight:500;border-bottom:1px solid )HTML" + border + R"HTML(;
           background:)HTML" + bg + R"HTML(}
.chunk{padding:12px 14px;border-bottom:1px solid )HTML" + border + R"HTML(}
.chunk:last-child{border-bottom:none}
.chunk-hdr{display:flex;align-items:center;gap:8px;margin-bottom:8px}
.score-badge{font-size:11px;font-weight:700;padding:2px 7px;border-radius:10px;color:#fff;font-family:monospace}
.doc-name{font-size:12px;font-weight:600;color:)HTML" + accent + R"HTML(}
.chunk-num{font-size:11px;color:)HTML" + muted + R"HTML(;margin-left:auto}
.chunk-text{font-size:13px;line-height:1.6;white-space:pre-wrap;word-break:break-word;
            background:)HTML" + surface + R"HTML(;border-radius:6px;padding:10px 12px;
            border-left:3px solid )HTML" + border + R"HTML(}
.no-chunks{padding:10px 14px;color:)HTML" + muted + R"HTML(;font-style:italic}
.empty{color:)HTML" + muted + R"HTML(;font-style:italic;margin-top:40px;text-align:center}
</style></head><body>
<h2>TestTaker — RAG Retrieval Log</h2>
<p class='path'>)HTML" + EscapeHTML(logPath) + R"HTML(</p>
)HTML" + cards + R"HTML(
</body></html>)HTML";
}
