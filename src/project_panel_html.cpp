#include "project_panel_html.h"
#include "html_template.h"
#include "markdown.h"
#include <sstream>

// ---------------------------------------------------------------------------
// CSS
// ---------------------------------------------------------------------------

static std::string BuildCSS() {
    return R"CSS(<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
  font-size:13px;line-height:1.4;color:var(--text);background:var(--bg);
  display:flex;flex-direction:column;height:100vh;overflow:hidden}
.pp-toolbar{padding:6px 8px;background:var(--surface);border-bottom:1px solid var(--border);
  display:flex;flex-wrap:wrap;gap:4px;align-items:center;flex-shrink:0}
#pp-search{padding:4px 8px;border:1px solid var(--border);border-radius:4px;
  background:var(--bg);color:var(--text);font-size:.9em;width:160px}
#pp-sort{padding:4px 6px;border:1px solid var(--border);border-radius:4px;
  background:var(--bg);color:var(--text);font-size:.9em}
.pp-sep{margin:0 4px;color:var(--border)}
.pp-btn{padding:3px 10px;border:1px solid var(--border);border-radius:4px;
  background:var(--surface);color:var(--text);cursor:pointer;font-size:.85em;white-space:nowrap}
.pp-btn:hover:not(:disabled){background:var(--link);color:#fff;border-color:var(--link)}
.pp-btn:disabled{opacity:.45;cursor:default}
.pp-tree-wrap{flex:1;overflow-y:auto;padding:4px 0}
ul.pp-tree,ul.pp-children{list-style:none;padding:0;margin:0}
ul.pp-children{margin-left:16px}
li.pp-folder>div.pp-item-row{cursor:pointer;padding:3px 8px;display:flex;align-items:center;gap:4px}
li.pp-folder>div.pp-item-row:hover{background:var(--surface)}
li.pp-folder .pp-arrow{font-size:.7em;color:var(--text-muted)}
li.pp-folder:not(.expanded)>.pp-children{display:none}
li.pp-proj>div.pp-item-row{cursor:pointer;padding:3px 8px 3px 24px;display:flex;align-items:center;gap:4px}
li.pp-proj>div.pp-item-row:hover{background:var(--surface)}
li.pp-proj.selected>div.pp-item-row{background:var(--link);color:#fff}
li.pp-proj.active>div.pp-item-row{font-weight:700}
li.pp-proj.active.selected>div.pp-item-row{background:var(--link);color:#fff;font-weight:700}
.pp-info{padding:6px 10px;font-size:.82em;color:var(--text-muted);border-top:1px solid var(--border);
  min-height:2.4em;flex-shrink:0;white-space:pre-wrap;word-break:break-all}
</style>
)CSS";
}

// ---------------------------------------------------------------------------
// JavaScript
// ---------------------------------------------------------------------------

static std::string BuildJS() {
    return R"JS(<script>
var ppSelectedPath = '';
var ppDragging = '';

function ppAction(payload) {
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ppAction)
        window.webkit.messageHandlers.ppAction.postMessage(payload);
}

function ppSelect(path, stats) {
    document.querySelectorAll('li.pp-proj').forEach(function(li) {
        li.classList.toggle('selected', li.dataset.path === path);
    });
    ppSelectedPath = path;
    var info = document.getElementById('pp-info');
    if (info) info.textContent = path + (stats ? '\n' + stats : '');
    document.querySelectorAll('.pp-act-btn').forEach(function(b) {
        b.disabled = !path;
    });
}

function ppActivate(path) {
    ppAction(JSON.stringify({action:'activate', path:path}));
}

function ppToggle(path) {
    document.querySelectorAll('li.pp-folder').forEach(function(li) {
        if (li.dataset.path === path) li.classList.toggle('expanded');
    });
}

function ppSearch() {
    var q = document.getElementById('pp-search').value.toLowerCase();
    document.querySelectorAll('li.pp-proj').forEach(function(li) {
        var name = (li.dataset.name || '').toLowerCase();
        li.style.display = (!q || name.indexOf(q) >= 0) ? '' : 'none';
    });
    document.querySelectorAll('li.pp-folder').forEach(function(li) {
        var anyVisible = false;
        li.querySelectorAll('li.pp-proj').forEach(function(p) {
            if (p.style.display !== 'none') anyVisible = true;
        });
        li.style.display = (!q || anyVisible) ? '' : 'none';
    });
}

function ppSort() {
    ppAction(JSON.stringify({action:'sort', order: document.getElementById('pp-sort').value}));
}

function ppRename() {
    if (ppSelectedPath) ppAction(JSON.stringify({action:'rename', path:ppSelectedPath}));
}

function ppDelete() {
    if (ppSelectedPath) ppAction(JSON.stringify({action:'delete', path:ppSelectedPath}));
}

function ppNewProject() {
    ppAction(JSON.stringify({action:'newProject', parentPath:ppSelectedPath}));
}

function ppNewSubfolder() {
    ppAction(JSON.stringify({action:'newSubfolder', parentPath:ppSelectedPath}));
}

function ppSetFolder() {
    ppAction(JSON.stringify({action:'setFolder'}));
}

function ppRefresh() {
    ppAction(JSON.stringify({action:'refresh'}));
}

function ppDragStart(e, path) {
    ppDragging = path;
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', path);
}

function ppDragOver(e) {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
}

function ppDrop(e, dst) {
    e.preventDefault();
    var src = ppDragging || e.dataTransfer.getData('text/plain');
    ppDragging = '';
    if (src && dst && src !== dst)
        ppAction(JSON.stringify({action:'move', src:src, dst:dst}));
}
</script>
)JS";
}

// ---------------------------------------------------------------------------
// Tree rendering
// ---------------------------------------------------------------------------

static std::string RenderEntries(const std::vector<ProjectEntry>& entries,
                                  const ProjectPanelState& s);

static std::string RenderFolder(const ProjectEntry& e, const ProjectPanelState& s) {
    bool expanded = s.expandedPaths.count(e.path) > 0;
    std::string cls = "pp-folder" + std::string(expanded ? " expanded" : "");
    std::string children = RenderEntries(e.children, s);
    return "<li class='" + cls + "' data-path='" + EscapeHTML(e.path) + "'"
           " ondragover='ppDragOver(event)'"
           " ondrop='ppDrop(event,\"" + EscapeHTML(e.path) + "\")'>"
           "<div class='pp-item-row'"
           " onclick='ppToggle(\"" + EscapeHTML(e.path) + "\")'>"
           "<span class='pp-arrow'>&#9654;</span>"
           "&#128193; " + EscapeHTML(e.name) +
           (e.dateStr.empty() ? "" : " <span class='pp-date'>" + EscapeHTML(e.dateStr) + "</span>") +
           "</div>"
           "<ul class='pp-children'>" + children + "</ul>"
           "</li>\n";
}

static std::string RenderProject(const ProjectEntry& e, const ProjectPanelState& s) {
    bool isActive   = (e.path == s.activePath);
    bool isSelected = (e.path == s.selectedPath);
    std::string cls = "pp-proj";
    if (isActive)   cls += " active";
    if (isSelected) cls += " selected";

    std::string statsEsc = EscapeHTML(e.stats);
    std::string pathEsc  = EscapeHTML(e.path);
    std::string nameEsc  = EscapeHTML(e.name);

    return "<li class='" + cls + "'"
           " data-path='" + pathEsc + "'"
           " data-name='" + nameEsc + "'"
           " data-stats='" + statsEsc + "'"
           " draggable='true'"
           " ondragstart='ppDragStart(event,\"" + pathEsc + "\")'"
           " ondragover='ppDragOver(event)'"
           " ondrop='ppDrop(event,\"" + pathEsc + "\")'>"
           "<div class='pp-item-row'"
           " onclick='ppSelect(\"" + pathEsc + "\",\"" + statsEsc + "\")'"
           " ondblclick='ppActivate(\"" + pathEsc + "\")'>"
           "&#128196; " + nameEsc +
           (e.dateStr.empty() ? "" : " <span class='pp-date'>" + EscapeHTML(e.dateStr) + "</span>") +
           "</div>"
           "</li>\n";
}

static std::string RenderEntries(const std::vector<ProjectEntry>& entries,
                                  const ProjectPanelState& s) {
    std::string out;
    for (const auto& e : entries) {
        if (e.isFolder)
            out += RenderFolder(e, s);
        else
            out += RenderProject(e, s);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

static std::string BuildToolbar(const ProjectPanelState& s) {
    bool hasSort = (s.sortOrder == "created" || s.sortOrder == "modified");
    std::string sortName     = (s.sortOrder == "name"    ) ? " selected" : "";
    std::string sortCreated  = (s.sortOrder == "created" ) ? " selected" : "";
    std::string sortModified = (s.sortOrder == "modified") ? " selected" : "";

    std::string toolbar =
        "<div class='pp-toolbar'>"
        "<input id='pp-search' type='text' placeholder='Search\xe2\x80\xa6' oninput='ppSearch()'>"
        "<select id='pp-sort' onchange='ppSort()'>"
        "<option value='name'" + sortName + ">Name</option>"
        "<option value='created'" + sortCreated + ">Created</option>"
        "<option value='modified'" + sortModified + ">Modified</option>"
        "</select>"
        "<button class='pp-btn pp-act-btn' disabled onclick='ppActivate(ppSelectedPath)'>Activate</button>"
        "<button class='pp-btn pp-act-btn' disabled onclick='ppRename()'>Rename</button>"
        "<button class='pp-btn pp-act-btn' disabled onclick='ppDelete()'>Delete\xe2\x80\xa6</button>"
        "<button class='pp-btn' onclick='ppNewProject()'>New Project</button>"
        "<button class='pp-btn' onclick='ppNewSubfolder()'>New Subfolder</button>"
        "<span class='pp-sep'>&#x2016;</span>"
        "<button class='pp-btn' onclick='ppSetFolder()'>Set Folder\xe2\x80\xa6</button>"
        "<button class='pp-btn' onclick='ppRefresh()'>Refresh</button>"
        "</div>\n";
    (void)hasSort;
    return toolbar;
}

// ---------------------------------------------------------------------------
// Main builder
// ---------------------------------------------------------------------------

std::string BuildProjectPanelHTML(const ProjectPanelState& s) {
    std::string css = BuildCSS();
    std::string js  = BuildJS();
    std::string toolbar = BuildToolbar(s);

    std::string treeContent;
    if (!s.hasFolder) {
        // No folder configured — show message, no tree items
        treeContent = "<div class='pp-no-folder'>" + EscapeHTML(s.folderMsg) + "</div>\n";
    } else {
        std::string items = RenderEntries(s.tree, s);
        treeContent = "<ul class='pp-tree'>" + items + "</ul>\n";
    }

    // Info bar shows selected stats or folderMsg
    std::string infoText = s.selectedPath.empty()
        ? EscapeHTML(s.folderMsg)
        : EscapeHTML(s.selectedPath);

    std::string body =
        toolbar +
        "<div class='pp-tree-wrap'>" + treeContent + "</div>\n"
        "<div id='pp-info' class='pp-info'>" + infoText + "</div>\n";

    return BuildHTML(css + js + body, "Projects", s.darkMode);
}
