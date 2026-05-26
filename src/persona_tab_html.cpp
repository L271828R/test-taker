#include "persona_tab_html.h"
#include <string>

std::string BuildPersonaTabHTML(bool darkMode) {
    std::string bodyClass = darkMode ? " class=\"dark\"" : "";
    std::string html = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<style>
:root {
  --bg:#f5f5f5; --surface:#fff; --border:#ddd;
  --text:#222; --muted:#888; --accent:#0066cc;
  --pill-active:#0066cc; --pill-active-text:#fff;
  --card-bg:#fff; --card-hover:#f0f4ff;
  --card-checked:#e8f0fe; --card-checked-border:#0066cc;
  --danger:#cc0000; --btn-bg:#fff;
}
.dark {
  --bg:#1e1e1e; --surface:#2a2a2a; --border:#444;
  --text:#e0e0e0; --muted:#999; --accent:#4d9fff;
  --pill-active:#4d9fff; --pill-active-text:#111;
  --card-bg:#2a2a2a; --card-hover:#2a2a3a;
  --card-checked:#1a2a3a; --card-checked-border:#4d9fff;
  --danger:#ff5555; --btn-bg:#2a2a2a;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: -apple-system, BlinkMacSystemFont, sans-serif;
  font-size: 13px; background: var(--bg); color: var(--text);
  padding: 16px;
}
h2 { font-size: 15px; font-weight: 600; margin-bottom: 14px; }
.top-bar { display: flex; gap: 8px; margin-bottom: 12px; align-items: center; }
.search {
  flex: 1; padding: 7px 10px; border-radius: 7px;
  border: 1px solid var(--border); background: var(--surface);
  color: var(--text); font-size: 13px; outline: none;
}
.search:focus { border-color: var(--accent); }
.filter-bar {
  display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 16px; align-items: center;
}
.pill {
  padding: 4px 12px; border-radius: 20px; border: 1px solid var(--border);
  background: var(--surface); color: var(--text);
  cursor: pointer; font-size: 12px; user-select: none;
  display: inline-flex; align-items: center; gap: 5px;
  transition: background .15s, border-color .15s;
}
.pill:hover { background: var(--card-hover); }
.pill.active { background: var(--pill-active); color: var(--pill-active-text); border-color: var(--pill-active); font-weight: 600; }
.pill .pdel { font-size: 10px; opacity: .6; line-height: 1; cursor: pointer; }
.pill .pdel:hover { opacity: 1; color: var(--danger); }
.btn-add-cat {
  padding: 4px 10px; border-radius: 20px; border: 1px dashed var(--border);
  background: transparent; color: var(--muted); cursor: pointer; font-size: 12px;
}
.btn-add-cat:hover { border-color: var(--accent); color: var(--accent); }
.cat-form { display: inline-flex; gap: 4px; align-items: center; }
.cat-inp {
  padding: 3px 8px; border-radius: 5px; border: 1px solid var(--accent);
  background: var(--surface); color: var(--text); font-size: 12px; width: 130px; outline: none;
}
.btn-xs { padding: 2px 8px; border-radius: 4px; font-size: 11px; cursor: pointer; border: none; }
.btn-ok  { background: var(--accent); color: #fff; }
.btn-ok:hover { filter: brightness(1.1); }
.btn-no  { background: var(--border); color: var(--text); }
.btn-no:hover { filter: brightness(.9); }
.grid { display: flex; flex-wrap: wrap; gap: 14px; }
.card {
  width: 148px; background: var(--card-bg); border: 1.5px solid var(--border);
  border-radius: 10px; padding: 12px 10px 10px;
  display: flex; flex-direction: column; align-items: center; gap: 6px;
  position: relative; transition: background .15s, border-color .15s;
}
.card:hover { background: var(--card-hover); }
.card.checked { background: var(--card-checked); border-color: var(--card-checked-border); }
.card-check {
  position: absolute; top: 8px; right: 8px;
  width: 16px; height: 16px; cursor: pointer; accent-color: var(--accent);
}
.card-del {
  position: absolute; top: 6px; left: 8px;
  background: none; border: none; cursor: pointer;
  color: var(--muted); font-size: 13px; line-height: 1; padding: 0; opacity: .35;
}
.card-del:hover { color: var(--danger); opacity: 1; }
.card-ren {
  position: absolute; top: 6px; left: 26px;
  background: none; border: none; cursor: pointer;
  color: var(--muted); font-size: 13px; line-height: 1; padding: 0; opacity: .35;
}
.card-ren:hover { color: var(--accent); opacity: 1; }
.rename-active { border-color: var(--accent); }
.rename-inp {
  width: 100%; border: 1px solid var(--accent); border-radius: 5px;
  background: var(--surface); color: var(--text); font-size: 12px;
  padding: 4px 6px; outline: none; font-family: inherit;
}
.avatar {
  width: 72px; height: 72px; border-radius: 50%;
  overflow: hidden; border: 2px solid var(--border);
  display: flex; align-items: center; justify-content: center;
  flex-shrink: 0; font-size: 24px; font-weight: 700; color: #fff;
  cursor: pointer; transition: border-color .15s;
}
.card.checked .avatar { border-color: var(--card-checked-border); }
.avatar:hover { border-color: var(--accent); }
.avatar img { width: 100%; height: 100%; object-fit: cover; display: block; }
.pname {
  font-size: 11px; font-weight: 500; text-align: center;
  color: var(--text); line-height: 1.3; word-break: break-word;
}
.cat-badge { font-size: 10px; color: var(--muted); text-transform: uppercase; letter-spacing: .04em; }
.desc-ta {
  width: 100%; border: 1px solid var(--border); border-radius: 5px;
  background: var(--surface); color: var(--text); font-size: 11px;
  font-family: inherit; padding: 3px 6px; resize: none; outline: none; line-height: 1.4;
}
.desc-ta:focus { border-color: var(--accent); }
.add-card {
  width: 148px; border: 2px dashed var(--border); border-radius: 10px;
  padding: 12px 10px; display: flex; flex-direction: column;
  align-items: center; justify-content: center; gap: 6px;
  cursor: pointer; transition: border-color .15s; min-height: 140px;
  background: transparent;
}
.add-card:hover { border-color: var(--accent); }
.add-card .plus { font-size: 26px; color: var(--muted); }
.add-card .add-lbl { font-size: 11px; color: var(--muted); }
.add-form-card {
  width: 148px; border: 2px solid var(--accent); border-radius: 10px;
  padding: 12px 10px; display: flex; flex-direction: column; gap: 6px;
  background: var(--card-bg);
}
.add-form-card input, .add-form-card select {
  width: 100%; border: 1px solid var(--border); border-radius: 5px;
  background: var(--surface); color: var(--text); font-size: 12px;
  padding: 4px 6px; outline: none; font-family: inherit;
}
.add-form-card input:focus { border-color: var(--accent); }
.add-form-btns { display: flex; gap: 4px; }
.add-form-btns .btn-xs { flex: 1; padding: 4px 0; font-size: 11px; }
.empty { color: var(--muted); padding: 32px; text-align: center; width: 100%; }
</style>
</head>
<body)HTML";
    html += bodyClass;
    html += R"HTML(>
<h2>Guest Commentators</h2>
<div class="top-bar">
  <input class="search" id="search" type="text" placeholder="Search…" oninput="setSearch(this.value)">
</div>
<div class="filter-bar" id="filters"></div>
<div class="grid" id="grid"></div>

<script>
var _cats    = {};
var _imgs    = {};
var _checked = {};
var _descs   = {};
var _cur      = '__all__';
var _search   = '';
var _addCat   = false;
var _addChar  = false;
var _renaming = null;

function send(obj) {
  window.webkit.messageHandlers.personas.postMessage(JSON.stringify(obj));
}

function esc(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}
function eattr(s) {
  return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;');
}
function ej(s) {
  return String(s).replace(/\\/g,'\\\\').replace(/'/g,"\\'").replace(/\n/g,'\\n').replace(/\r/g,'');
}
function norm(s) {
  return s.trim().toLowerCase().replace(/\s+/g,'_').replace(/[^a-z0-9_--￿]/g,'');
}
function nameColor(name) {
  var h = 0;
  for (var i = 0; i < name.length; i++)
    h = (h * 31 + name.charCodeAt(i)) & 0xfffffff;
  return 'hsl(' + (h % 360) + ',52%,42%)';
}
function fuzzy(name, q) {
  if (!q) return true;
  var n = name.toLowerCase(), qi = 0;
  for (var i = 0; i < n.length && qi < q.length; i++)
    if (n[i] === q[qi]) qi++;
  return qi === q.length;
}

function allNames() {
  var seen = {}, out = [];
  Object.keys(_cats).sort().forEach(function(c) {
    _cats[c].forEach(function(n) { if (!seen[n]) { seen[n] = 1; out.push(n); } });
  });
  return out.sort();
}
function getNames() {
  var names = _cur === '__all__' ? allNames() : (_cats[_cur] || []).slice().sort();
  if (_search) {
    var q = _search.toLowerCase();
    names = names.filter(function(n) { return fuzzy(n, q); });
  }
  return names;
}
function getCat(name) {
  var found = [];
  Object.keys(_cats).forEach(function(c) {
    if (_cats[c].indexOf(name) !== -1) found.push(c);
  });
  return found[0] || '';
}

function renderFilters() {
  var cats = Object.keys(_cats).sort();
  var h = '<div class="pill' + (_cur === '__all__' ? ' active' : '') +
          '" onclick="setFilter(\'__all__\')">All</div>';
  cats.forEach(function(c) {
    h += '<div class="pill' + (_cur === c ? ' active' : '') +
         '" onclick="setFilter(\'' + ej(c) + '\')">' + esc(c) +
         '<span class="pdel" onclick="event.stopPropagation();delCat(\'' + ej(c) + '\')">&#x2715;</span>' +
         '</div>';
  });
  if (_addCat) {
    h += '<div class="cat-form">' +
         '<input class="cat-inp" id="catInp" placeholder="Category…" ' +
         'onkeydown="catKey(event)">' +
         '<button class="btn-xs btn-ok" onclick="okCat()">&#x2713;</button>' +
         '<button class="btn-xs btn-no" onclick="cancelCat()">&#x2715;</button>' +
         '</div>';
  } else {
    h += '<button class="btn-add-cat" onclick="startCat()">+ Category</button>';
  }
  document.getElementById('filters').innerHTML = h;
  if (_addCat) { var el = document.getElementById('catInp'); if (el) el.focus(); }
}

function renderGrid() {
  var names = getNames();
  var h = names.map(function(name) {
    var key = norm(name);
    var url = _imgs[key];
    var chk = !!_checked[name];
    var catLabel = (_cur === '__all__' && !_search)
      ? '<div class="cat-badge">' + esc(getCat(name)) + '</div>' : '';
    var avatar = url
      ? '<img src="' + esc(url) + '" alt="' + eattr(name) + '">'
      : '<span style="background:' + nameColor(name) + '">' +
        esc(name.trim().charAt(0).toUpperCase()) + '</span>';
    var desc = esc(_descs[name] || '');
    if (_renaming === name) {
      return '<div class="card rename-active">' +
        '<div class="pname" style="margin-top:8px">' + esc(name) + '</div>' +
        '<input class="rename-inp" id="renameInp" value="' + eattr(name) + '" placeholder="New name…"' +
          ' onkeydown="renameKey(event,\'' + ej(name) + '\')">' +
        '<div class="add-form-btns">' +
          '<button class="btn-xs btn-ok" onclick="confirmRename(\'' + ej(name) + '\')">Rename</button>' +
          '<button class="btn-xs btn-no" onclick="cancelRename()">Cancel</button>' +
        '</div></div>';
    }
    return '<div class="card' + (chk ? ' checked' : '') + '">' +
      '<button class="card-del" onclick="delChar(\'' + ej(name) + '\')" title="Remove">&#x2715;</button>' +
      '<button class="card-ren" onclick="startRename(\'' + ej(name) + '\')" title="Rename">&#x270e;</button>' +
      '<input type="checkbox" class="card-check"' + (chk ? ' checked' : '') +
        ' onchange="toggleChar(\'' + ej(name) + '\',this.checked)">' +
      '<div class="avatar" onclick="uploadImg(\'' + ej(name) + '\')" title="Upload image">' +
        avatar + '</div>' +
      '<div class="pname">' + esc(name) + '</div>' +
      catLabel +
      '<textarea class="desc-ta" rows="2" placeholder="Notes…" data-name="' + eattr(name) + '"' +
        ' oninput="scheduleDesc(\'' + ej(name) + '\',this.value)"' +
        ' onblur="saveDesc(\'' + ej(name) + '\',this.value)">' + desc + '</textarea>' +
      '</div>';
  }).join('');

  if (_addChar) {
    var opts = Object.keys(_cats).sort().map(function(c) {
      return '<option value="' + eattr(c) + '"' +
             (c === _cur && _cur !== '__all__' ? ' selected' : '') + '>' + esc(c) + '</option>';
    }).join('');
    h += '<div class="add-form-card">' +
         '<input id="charInp" placeholder="Name…" onkeydown="charKey(event)">' +
         (opts ? '<select id="charCat">' + opts + '</select>' : '') +
         '<div class="add-form-btns">' +
         '<button class="btn-xs btn-ok" onclick="okChar()">Add</button>' +
         '<button class="btn-xs btn-no" onclick="cancelChar()">Cancel</button>' +
         '</div></div>';
  } else {
    h += '<div class="add-card" onclick="startChar()">' +
         '<span class="plus">+</span>' +
         '<span class="add-lbl">Add commentator</span></div>';
  }

  var el = document.getElementById('grid');
  el.innerHTML = h || '<div class="empty">No commentators found.</div>';
  if (_addChar) { var ci = document.getElementById('charInp'); if (ci) ci.focus(); }
}

function render() { renderFilters(); renderGrid(); }

function setFilter(cat) { _cur = cat; render(); }
function setSearch(val) { _search = val.trim(); renderGrid(); }

function startCat() { _addCat = true; renderFilters(); }
function cancelCat() { _addCat = false; renderFilters(); }
function okCat() {
  var el = document.getElementById('catInp');
  var name = el ? el.value.trim() : '';
  _addCat = false;
  if (name) send({action:'addCategory', name:name});
  else renderFilters();
}
function catKey(e) { if (e.key==='Enter') okCat(); else if (e.key==='Escape') cancelCat(); }
function delCat(name) {
  if (!confirm('Delete category "' + name + '" and all its commentators?')) return;
  send({action:'deleteCategory', name:name});
}

function startChar() { _addChar = true; renderGrid(); }
function cancelChar() { _addChar = false; renderGrid(); }
function okChar() {
  var ni = document.getElementById('charInp');
  var cs = document.getElementById('charCat');
  var name = ni ? ni.value.trim() : '';
  var cat  = cs ? cs.value : (Object.keys(_cats).sort()[0] || '');
  _addChar = false;
  if (name && cat) send({action:'addCharacter', category:cat, name:name});
  else renderGrid();
}
function charKey(e) { if (e.key==='Enter') okChar(); else if (e.key==='Escape') cancelChar(); }
function delChar(name) {
  send({action:'deleteCharacter', category:getCat(name), name:name});
}

function toggleChar(name, checked) {
  _checked[name] = checked;
  renderGrid();
  send({action:'toggle', name:name, checked:checked});
}
var _descTimers = {};
function scheduleDesc(name, desc) {
  _descs[name] = desc;
  clearTimeout(_descTimers[name]);
  _descTimers[name] = setTimeout(function() { saveDesc(name, desc); }, 600);
}
function saveDesc(name, desc) {
  _descs[name] = desc;
  send({action:'setDesc', name:name, description:desc});
}
function uploadImg(name) {
  send({action:'uploadImage', name:name});
}

function startRename(name) {
  _renaming = name;
  renderGrid();
  setTimeout(function() {
    var el = document.getElementById('renameInp');
    if (el) { el.focus(); el.select(); }
  }, 0);
}
function cancelRename() { _renaming = null; renderGrid(); }
function confirmRename(oldName) {
  var el = document.getElementById('renameInp');
  var newName = el ? el.value.trim() : '';
  if (!newName || newName === oldName) { cancelRename(); return; }
  _renaming = null;
  send({action:'renameCharacter', oldName:oldName, newName:newName});
}
function renameKey(e, oldName) {
  if (e.key === 'Enter') confirmRename(oldName);
  else if (e.key === 'Escape') cancelRename();
}

function setCharacters(data) {
  _cats    = data.cats    || {};
  _imgs    = data.imgs    || {};
  _descs   = data.descs   || {};
  _checked = {};
  (data.checked || []).forEach(function(n) { _checked[n] = true; });
  if (_cur !== '__all__' && !_cats[_cur]) _cur = '__all__';
  render();
}

function setDarkMode(dark) {
  document.body.classList.toggle('dark', dark);
}

window.updatePersonaImage = function(key, url) {
  _imgs[key] = url;
  renderGrid();
};

document.addEventListener('DOMContentLoaded', function() {
  send({action:'ready'});
  document.getElementById('search').focus();
});
</script>
</body>
</html>)HTML";
    return html;
}
