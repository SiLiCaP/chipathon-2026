#!/usr/bin/env python3
"""Static results site for local-ci runs.

  report.py build <out_dir> [<run_dir>]
                                (re)generate <out_dir>/index.html, and the page of
                                <run_dir> only (or of every run when it is omitted)
  report.py junit <results.xml> print an HTML test summary (like test-summary/action)
"""

import datetime
import hashlib
import html
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

import mistune

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b\][^\x07]*\x07|\r(?!\n)")
EMOJI = {
    "green_circle": "🟢", "orange_circle": "🟠", "red_circle": "🔴", "yellow_circle": "🟡",
    "white_check_mark": "✅", "heavy_check_mark": "✔️", "x": "❌", "warning": "⚠️",
    "information_source": "ℹ️", "no_entry": "⛔", "construction": "🚧", "rocket": "🚀",
    "tada": "🎉", "memo": "📝", "mag": "🔍", "wrench": "🔧", "stopwatch": "⏱️",
}
WORKFLOW_ORDER = ["test", "docs", "gds", "fpga"]
JOB_ORDER = ["test", "docs", "gds", "viewer", "precheck", "gl_test"]
LOG_TAIL_LINES = 3000
WAVE_EXT = (".fst", ".vcd")

markdown = mistune.create_markdown(escape=False, plugins=["table", "strikethrough", "url"])


def esc(s):
    return html.escape(str(s), quote=True)


def read(path, default=""):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return default


def fmt_duration(seconds):
    if seconds is None:
        return ""
    seconds = int(seconds)
    if seconds < 60:
        return f"{seconds}s"
    m, s = divmod(seconds, 60)
    if m < 60:
        return f"{m}m {s}s"
    h, m = divmod(m, 60)
    return f"{h}h {m}m"


def fmt_size(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024


def emojify(text):
    return re.sub(r":([a-z_]+):", lambda m: EMOJI.get(m.group(1), m.group(0)), text)


# --- JUnit summary --------------------------------------------------------------

def junit_summary(path):
    if not os.path.isfile(path):
        return f"<p>⚠️ No test results found at <code>{esc(path)}</code>.</p>\n"
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as e:
        return f"<p>⚠️ Could not parse <code>{esc(path)}</code>: {esc(e)}</p>\n"
    rows, counts = [], {"passed": 0, "failed": 0, "skipped": 0}
    for tc in root.iter("testcase"):
        name = tc.get("name", "?")
        cls = tc.get("classname", "")
        t = tc.get("time")
        fail = tc.find("failure")
        if fail is None:
            fail = tc.find("error")
        if fail is not None:
            status = "failed"
            detail = (fail.get("message") or "") + "\n" + (fail.text or "")
        elif tc.find("skipped") is not None:
            status, detail = "skipped", tc.find("skipped").get("message") or ""
        else:
            status, detail = "passed", ""
        counts[status] += 1
        rows.append((status, cls, name, t, detail.strip()))
    total = sum(counts.values())
    icon = "❌" if counts["failed"] else ("✅" if total else "⚠️")
    out = [f'<div class="junit"><h2>{icon} {total} test{"s" if total != 1 else ""} — '
           f'<span class="ok">{counts["passed"]} passed</span>, '
           f'<span class="bad">{counts["failed"]} failed</span>, '
           f'<span class="dim">{counts["skipped"]} skipped</span></h2>',
           '<table><thead><tr><th></th><th>Test</th><th>Time</th></tr></thead><tbody>']
    for status, cls, name, t, detail in rows:
        mark = {"passed": "✅", "failed": "❌", "skipped": "⏭️"}[status]
        label = f"{esc(cls)}.<b>{esc(name)}</b>" if cls else f"<b>{esc(name)}</b>"
        time = f"{float(t):.3f}s" if t else ""
        out.append(f"<tr><td>{mark}</td><td>{label}")
        if detail:
            out.append(f"<pre>{esc(detail)}</pre>")
        out.append(f"</td><td>{time}</td></tr>")
    out.append("</tbody></table></div>\n")
    return "".join(out)


# --- Run data ---------------------------------------------------------------------

def load_job(run_dir, job_id, artifacts=True):
    d = os.path.join(run_dir, "jobs", job_id)
    job = {
        "id": job_id,
        "dir": d,
        "workflow": read(os.path.join(d, "workflow"), "?").strip(),
        "name": read(os.path.join(d, "name"), job_id).strip(),
        "status": read(os.path.join(d, "status"), "queued").strip(),
        "reason": read(os.path.join(d, "reason")).strip(),
        "summary": read(os.path.join(d, "summary.md")),
        "steps": [],
        "artifacts": [],
    }
    try:
        started = int(read(os.path.join(d, "started"), "0") or 0) or None
        finished = int(read(os.path.join(d, "finished"), "0") or 0) or None
    except ValueError:
        started = finished = None
    job["started"] = started
    if started:
        end = finished or datetime.datetime.now().timestamp()
        job["duration"] = end - started
    else:
        job["duration"] = None
    for line in read(os.path.join(d, "steps.tsv")).splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        n, name, status = parts[:3]
        t0 = int(parts[3]) if len(parts) > 3 and parts[3] else None
        t1 = int(parts[4]) if len(parts) > 4 and parts[4] else None
        job["steps"].append({"n": n, "name": name, "status": status,
                             "duration": (t1 - t0) if t0 and t1 else None})
    current = read(os.path.join(d, "current")).strip()
    if current and "\t" in current:
        n, name = current.split("\t", 1)
        job["steps"].append({"n": n, "name": name, "status": "running", "duration": None})
    adir = os.path.join(d, "artifacts")
    if os.path.isdir(adir):
        for name in sorted(os.listdir(adir)):
            files, size = [], 0
            if not artifacts:  # names only, for the index page
                job["artifacts"].append({"name": name, "path": "", "files": [], "size": 0})
                continue
            for root, _, fnames in os.walk(os.path.join(adir, name)):
                for fn in fnames:
                    p = os.path.join(root, fn)
                    try:
                        sz = os.path.getsize(p)
                    except OSError:
                        continue
                    size += sz
                    files.append((os.path.relpath(p, run_dir), sz))
            job["artifacts"].append({"name": name, "path": os.path.relpath(os.path.join(adir, name), run_dir),
                                     "files": sorted(files), "size": size})
    return job


def load_run(run_dir, full=True):
    try:
        meta = json.loads(read(os.path.join(run_dir, "meta.json"), "{}"))
    except json.JSONDecodeError:
        meta = {}
    meta.setdefault("id", os.path.basename(run_dir))
    jobs_dir = os.path.join(run_dir, "jobs")
    ids = os.listdir(jobs_dir) if os.path.isdir(jobs_dir) else []
    ids.sort(key=lambda j: (JOB_ORDER.index(j) if j in JOB_ORDER else 99, j))
    jobs = [load_job(run_dir, j, artifacts=full) for j in ids]
    finished = read(os.path.join(run_dir, "finished")).strip()
    statuses = [j["status"] for j in jobs]
    if not finished:
        status = "running"
    elif "failure" in statuses:
        status = "failure"
    elif "cancelled" in statuses:
        status = "cancelled"
    elif statuses and all(s in ("success", "skipped") for s in statuses):
        status = "success"
    else:
        status = "cancelled"
    started = meta.get("started")
    duration = None
    if started:
        try:
            t0 = datetime.datetime.fromisoformat(started)
            t1 = datetime.datetime.fromisoformat(finished) if finished else datetime.datetime.now(t0.tzinfo)
            duration = (t1 - t0).total_seconds()
        except ValueError:
            pass
    return {"meta": meta, "jobs": jobs, "status": status, "duration": duration,
            "finished": bool(finished), "dir": run_dir}


# --- HTML ---------------------------------------------------------------------------

ICONS = {
    "success": '<span class="ic ic-success" title="success">✓</span>',
    "failure": '<span class="ic ic-failure" title="failure">✕</span>',
    "skipped": '<span class="ic ic-skipped" title="skipped">⊘</span>',
    "running": '<span class="ic ic-running" title="running"></span>',
    "queued": '<span class="ic ic-queued" title="queued">•</span>',
    "cancelled": '<span class="ic ic-skipped" title="cancelled">⊘</span>',
}


def icon(status):
    return ICONS.get(status, ICONS["queued"])


CSS = """
:root{--bg:#f6f7f9;--panel:#fff;--panel2:#f0f2f5;--fg:#1c2128;--dim:#5b636e;--line:#d8dde3;
--accent:#7b3fe4;--ok:#1a7f37;--bad:#cf222e;--warn:#9a6700;--code:#f3f4f6;--logbg:#0f1419;--logfg:#d6dde6}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#0d1117;--panel:#161b22;--panel2:#1c222b;
--fg:#e6edf3;--dim:#8b949e;--line:#30363d;--accent:#a67cf5;--ok:#3fb950;--bad:#f85149;--warn:#d29922;--code:#1f2630;
--logbg:#010409;--logfg:#c9d1d9}}
:root[data-theme=dark]{--bg:#0d1117;--panel:#161b22;--panel2:#1c222b;--fg:#e6edf3;--dim:#8b949e;--line:#30363d;
--accent:#a67cf5;--ok:#3fb950;--bad:#f85149;--warn:#d29922;--code:#1f2630;--logbg:#010409;--logfg:#c9d1d9}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}
code,pre{font-family:ui-monospace,"JetBrains Mono",Menlo,Consolas,monospace;font-size:12.5px}
header.top{background:var(--panel);border-bottom:1px solid var(--line);padding:12px 24px;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
header.top .brand{font-weight:700;letter-spacing:.2px}header.top .brand b{color:var(--accent)}
header.top .crumbs{color:var(--dim)}
.wrap{max-width:1280px;margin:0 auto;padding:20px 16px}
.layout{display:grid;grid-template-columns:240px 1fr;gap:20px;align-items:start}
@media (max-width:820px){.layout{grid-template-columns:1fr}}
nav.jobs{position:sticky;top:12px;background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:10px}
nav.jobs h4{margin:10px 6px 4px;font-size:12px;text-transform:uppercase;letter-spacing:.6px;color:var(--dim)}
nav.jobs a{display:flex;gap:8px;align-items:center;padding:5px 8px;border-radius:6px;color:var(--fg)}
nav.jobs a:hover{background:var(--panel2);text-decoration:none}nav.jobs a .t{margin-left:auto;color:var(--dim);font-size:12px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:10px;margin-bottom:18px;overflow:hidden}
.card>.hd{display:flex;gap:10px;align-items:center;padding:12px 16px;border-bottom:1px solid var(--line)}
.card>.hd h2{margin:0;font-size:16px}.card>.hd .meta{margin-left:auto;color:var(--dim);font-size:13px}
.card>.bd{padding:12px 16px}
.runhead{display:flex;flex-wrap:wrap;gap:18px 32px;align-items:center}
.runhead .big{font-size:20px;font-weight:650;display:flex;gap:10px;align-items:center}
.kv{color:var(--dim);font-size:13px}.kv b{color:var(--fg);font-weight:600}
.pill{display:inline-block;padding:1px 8px;border-radius:999px;font-size:12px;border:1px solid var(--line);color:var(--dim)}
.pill.warn{color:var(--warn);border-color:currentColor}
.ic{display:inline-flex;width:18px;height:18px;border-radius:50%;align-items:center;justify-content:center;font-size:11px;font-weight:700;flex:none;color:#fff}
.ic-success{background:var(--ok)}.ic-failure{background:var(--bad)}
.ic-skipped,.ic-queued{background:transparent;color:var(--dim);border:1.5px solid var(--dim);font-size:10px}
.ic-running{border:2.5px solid var(--warn);border-top-color:transparent;animation:spin 1s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.section-title{font-size:12px;text-transform:uppercase;letter-spacing:.6px;color:var(--dim);margin:14px 0 6px}
details.step{border-top:1px solid var(--line)}details.step:first-child{border-top:0}
details.step>summary{list-style:none;cursor:pointer;display:flex;gap:10px;align-items:center;padding:7px 4px}
details.step>summary::-webkit-details-marker{display:none}
details.step>summary:before{content:"▸";color:var(--dim);width:10px}details.step[open]>summary:before{content:"▾"}
details.step>summary .t{margin-left:auto;color:var(--dim);font-size:12px}
details.step>summary:hover{background:var(--panel2)}
pre.log{margin:0 0 8px;background:var(--logbg);color:var(--logfg);padding:10px 12px;border-radius:8px;overflow:auto;max-height:560px;white-space:pre}
.loglinks{font-size:12px;margin:-2px 0 10px}
.summary{border:1px solid var(--line);border-radius:8px;padding:4px 16px;background:var(--panel);overflow:auto}
.summary table{border-collapse:collapse;margin:8px 0}.summary th,.summary td{border:1px solid var(--line);padding:4px 10px;text-align:left;vertical-align:top}
.summary th{background:var(--panel2)}.summary pre{background:var(--code);padding:8px;border-radius:6px;overflow:auto}
.summary code{background:var(--code);padding:1px 4px;border-radius:4px}.summary pre code{background:none;padding:0}
.summary h1{font-size:20px}.summary h2{font-size:17px}.summary h1,.summary h2{border-bottom:1px solid var(--line);padding-bottom:4px}
.summary details summary h1,.summary details summary h4{display:inline}
.junit .ok{color:var(--ok)}.junit .bad{color:var(--bad)}.junit .dim{color:var(--dim)}.junit pre{white-space:pre-wrap;color:var(--bad)}
table.arts{width:100%;border-collapse:collapse}table.arts td{padding:6px 4px;border-top:1px solid var(--line);vertical-align:top}
table.arts tr:first-child td{border-top:0}table.arts .sz{color:var(--dim);white-space:nowrap;text-align:right}
table.arts ul{margin:4px 0 0;padding-left:18px;font-size:13px}
table.runs{width:100%;border-collapse:collapse}table.runs td,table.runs th{padding:9px 10px;border-top:1px solid var(--line);text-align:left}
table.runs th{color:var(--dim);font-weight:500;font-size:12px;text-transform:uppercase;letter-spacing:.5px;border-top:0}
table.runs tr:hover td{background:var(--panel2)}
.chips{display:flex;gap:6px;flex-wrap:wrap}.chip{display:inline-flex;gap:5px;align-items:center;font-size:12px;color:var(--dim)}
.chip .ic{width:14px;height:14px;font-size:9px}
.empty{color:var(--dim);font-style:italic}
img{max-width:100%}
"""

JS = """
(function(){
  // Rewrite 3D-viewer links to point at this server
  document.querySelectorAll('a.model-link').forEach(function(a){
    var url = new URL(a.dataset.model, location.href).href;
    a.href = a.dataset.viewer + encodeURIComponent(url);
    a.target = '_blank'; a.rel = 'noopener';
  });
  document.querySelectorAll('a.surfer-link').forEach(function(a){
    a.href = 'https://app.surfer-project.org/?load_url=' + encodeURIComponent(new URL(a.dataset.wave, location.href).href);
  });
  // Remember which <details> are open across reloads
  var key = 'open:' + location.pathname;
  var saved = []; try { saved = JSON.parse(sessionStorage.getItem(key) || '[]'); } catch(e) {}
  saved.forEach(function(id){ var d = document.getElementById(id); if (d) d.open = true; });
  function save(){ try { sessionStorage.setItem(key, JSON.stringify(
    Array.from(document.querySelectorAll('details[id]')).filter(function(d){return d.open}).map(function(d){return d.id}))); } catch(e) {} }
  document.querySelectorAll('details[id]').forEach(function(d){ d.addEventListener('toggle', save); });
  try { var y = sessionStorage.getItem('scroll:' + location.pathname); if (y) window.scrollTo(0, +y); } catch(e) {}
  var ANSI = /\\x1b\\[[0-9;?]*[ -\\/]*[@-~]|\\x1b\\][^\\x07]*\\x07|\\r(?!\\n)/g;
  // Live-tail the running step's log
  document.querySelectorAll('pre[data-live]').forEach(function(pre){
    function tick(){
      fetch(pre.dataset.live + '?t=' + Date.now()).then(function(r){ return r.ok ? r.text() : ''; }).then(function(t){
        var atBottom = pre.scrollTop + pre.clientHeight >= pre.scrollHeight - 30;
        var lines = t.replace(ANSI, '').split('\\n');
        pre.textContent = lines.slice(-%(tail)d).join('\\n');
        if (atBottom) pre.scrollTop = pre.scrollHeight;
      }).catch(function(){});
    }
    tick(); setInterval(tick, 2000);
  });
  // Reload when the run state changes
  var sig = document.body.dataset.sig;
  if (sig && document.body.dataset.live === '1') {
    setInterval(function(){
      fetch('state.json?t=' + Date.now()).then(function(r){ return r.json(); }).then(function(s){
        if (s.sig !== sig) { try { sessionStorage.setItem('scroll:' + location.pathname, window.scrollY); } catch(e) {} location.reload(); }
      }).catch(function(){});
    }, 3000);
  }
})();
""".replace("%(tail)d", str(LOG_TAIL_LINES))


def page(title, body, crumbs="", sig="", live=False):
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{esc(title)}</title><style>{CSS}</style></head>
<body data-sig="{esc(sig)}" data-live="{1 if live else 0}">
<header class="top"><span class="brand"><b>◆</b> Local CI</span><span class="crumbs">{crumbs}</span></header>
<div class="wrap">{body}</div>
<script>{JS}</script></body></html>
"""


def render_log(run_dir, job, step):
    rel = os.path.join("jobs", job["id"], "steps", f"{step['n']}.log")
    if step["status"] == "running":
        return f'<pre class="log" data-live="{esc(rel)}">…</pre>'
    text = ANSI_RE.sub("", read(os.path.join(run_dir, rel)))
    lines = text.rstrip("\n").split("\n")
    note = ""
    if len(lines) > LOG_TAIL_LINES:
        note = f'<div class="loglinks">Showing the last {LOG_TAIL_LINES} of {len(lines)} lines. '
        note += f'<a href="{esc(rel)}" target="_blank">Full log</a></div>'
        lines = lines[-LOG_TAIL_LINES:]
    else:
        note = f'<div class="loglinks"><a href="{esc(rel)}" target="_blank">Raw log</a></div>'
    body = esc("\n".join(lines)) if text.strip() else '<span class="empty">(no output)</span>'
    return f'<pre class="log">{body}</pre>{note}'


def render_artifacts(job):
    if not job["artifacts"]:
        return ""
    out = ['<div class="section-title">Artifacts</div><table class="arts">']
    for a in job["artifacts"]:
        out.append(f'<tr><td><b>{esc(a["name"])}</b> · <a href="{esc(a["path"])}/">browse</a> · '
                   f'<a href="{esc(a["path"])}/?zip=1">download .zip</a>')
        shown = a["files"] if len(a["files"]) <= 12 else [f for f in a["files"] if f[0].endswith(WAVE_EXT + (".png", ".pdf", ".gds", ".oas", ".xml", ".v"))][:12]
        if shown:
            out.append("<ul>")
            for path, sz in shown:
                extra = ""
                if path.endswith(WAVE_EXT):
                    extra = f' · <a class="surfer-link" data-wave="{esc(path)}" href="#" target="_blank" rel="noopener">open in Surfer</a>'
                out.append(f'<li><a href="{esc(path)}">{esc(os.path.relpath(path, a["path"]))}</a> '
                           f'<span class="kv">{fmt_size(sz)}</span>{extra}</li>')
            if len(shown) < len(a["files"]):
                out.append(f'<li class="kv">… {len(a["files"]) - len(shown)} more files</li>')
            out.append("</ul>")
        out.append(f'</td><td class="sz">{len(a["files"])} files · {fmt_size(a["size"])}</td></tr>')
    out.append("</table>")
    return "".join(out)


def render_job(run, job):
    run_dir = run["dir"]
    head = (f'<div class="hd">{icon(job["status"])}<h2>{esc(job["workflow"])} / {esc(job["name"])}</h2>'
            f'<span class="meta">{esc(job["status"])}{" · " + fmt_duration(job["duration"]) if job["duration"] else ""}</span></div>')
    body = []
    if job["reason"]:
        body.append(f'<p class="kv">Skipped ({esc(job["reason"])}).</p>')
    if job["status"] == "queued":
        body.append('<p class="empty">Waiting to start…</p>')
    if job["summary"].strip():
        body.append('<div class="section-title">Summary</div><div class="summary">')
        body.append(markdown(emojify(job["summary"])))
        body.append("</div>")
    body.append(render_artifacts(job))
    if job["steps"]:
        body.append('<div class="section-title">Steps</div><div>')
        for s in job["steps"]:
            sid = f'{job["id"]}-step-{s["n"]}'
            is_open = " open" if s["status"] in ("failure", "running") else ""
            body.append(f'<details class="step" id="{sid}"{is_open}><summary>{icon(s["status"])} {esc(s["name"])}'
                        f'<span class="t">{fmt_duration(s["duration"])}</span></summary>')
            if s["status"] != "skipped":
                body.append(render_log(run_dir, job, s))
            body.append("</details>")
        body.append("</div>")
    return f'<section class="card" id="job-{esc(job["id"])}">{head}<div class="bd">{"".join(body)}</div></section>'


def run_signature(run):
    parts = [run["status"]]
    for j in run["jobs"]:
        parts.append(j["id"] + j["status"] + "".join(s["n"] + s["status"] for s in j["steps"]))
        parts.append(str(len(j["summary"])) + str(len(j["artifacts"])))
    return hashlib.sha1("|".join(parts).encode()).hexdigest()[:16]


def render_run(run):
    m = run["meta"]
    sig = run_signature(run)
    workflows = {}
    for j in run["jobs"]:
        workflows.setdefault(j["workflow"], []).append(j)
    nav = ['<nav class="jobs">']
    for wf in sorted(workflows, key=lambda w: WORKFLOW_ORDER.index(w) if w in WORKFLOW_ORDER else 99):
        nav.append(f"<h4>{esc(wf)}</h4>")
        for j in workflows[wf]:
            nav.append(f'<a href="#job-{esc(j["id"])}">{icon(j["status"])} {esc(j["name"])}'
                       f'<span class="t">{fmt_duration(j["duration"])}</span></a>')
    nav.append("</nav>")
    dirty = ' <span class="pill warn" title="Uncommitted changes were included in this run">uncommitted changes</span>' if m.get("dirty") else ""
    head = (f'<section class="card"><div class="bd runhead"><div class="big">{icon(run["status"])} '
            f'{esc(m.get("message") or m["id"])}</div>'
            f'<div class="kv">Commit <b>{esc(m.get("commit", "?"))}</b>{dirty}</div>'
            f'<div class="kv">Branch <b>{esc(m.get("branch", "?"))}</b></div>'
            f'<div class="kv">Started <b>{esc((m.get("started") or "")[:19].replace("T", " "))}</b></div>'
            f'<div class="kv">Duration <b>{fmt_duration(run["duration"])}</b></div>'
            f'<div class="kv">Status <b>{esc(run["status"])}</b></div></div></section>')
    jobs = "".join(render_job(run, j) for j in run["jobs"])
    body = f'<div class="layout">{"".join(nav)}<div>{head}{jobs}</div></div>'
    crumbs = f'<a href="../../">All runs</a> / {esc(m["id"])}'
    live = run["status"] == "running"
    with open(os.path.join(run["dir"], "state.json"), "w") as f:
        json.dump({"sig": sig, "status": run["status"]}, f)
    with open(os.path.join(run["dir"], "index.html"), "w", encoding="utf-8") as f:
        f.write(page(f'{run["status"]} · {m["id"]} · Local CI', body, crumbs, sig, live))


def render_index(out_dir, runs):
    rows = []
    for r in runs:
        m = r["meta"]
        chips = "".join(f'<span class="chip">{icon(j["status"])}{esc(j["name"])}</span>' for j in r["jobs"])
        dirty = ' <span class="pill warn">dirty</span>' if m.get("dirty") else ""
        rows.append(f'<tr><td>{icon(r["status"])}</td><td><a href="runs/{esc(m["id"])}/"><b>{esc(m.get("message") or m["id"])}</b></a>'
                    f'<div class="kv">{esc(m.get("commit", ""))}{dirty} · {esc(m.get("branch", ""))}</div></td>'
                    f'<td><div class="chips">{chips}</div></td>'
                    f'<td class="kv">{esc((m.get("started") or "")[:19].replace("T", " "))}</td>'
                    f'<td class="kv">{fmt_duration(r["duration"])}</td></tr>')
    table = ('<table class="runs"><thead><tr><th></th><th>Run</th><th>Jobs</th><th>Started</th><th>Duration</th></tr></thead>'
             f'<tbody>{"".join(rows)}</tbody></table>') if rows else '<p class="empty">No runs yet. Start one with <code>local-ci/run</code>.</p>'
    body = f'<section class="card"><div class="hd"><h2>Workflow runs</h2><span class="meta">{len(runs)} runs</span></div><div class="bd">{table}</div></section>'
    sig = hashlib.sha1("|".join(run_signature(r) + r["meta"]["id"] for r in runs).encode()).hexdigest()[:16]
    live = any(r["status"] == "running" for r in runs)
    with open(os.path.join(out_dir, "state.json"), "w") as f:
        json.dump({"sig": sig}, f)
    with open(os.path.join(out_dir, "index.html"), "w", encoding="utf-8") as f:
        f.write(page("Local CI", body, "All runs", sig, live))


def build(out_dir, only=None):
    """Render the index and run pages. With `only` (the run in progress), other
    runs are loaded without walking their artifacts and their pages are kept,
    unless a page is missing."""
    runs_dir = os.path.join(out_dir, "runs")
    os.makedirs(runs_dir, exist_ok=True)
    only = os.path.realpath(only) if only else None
    runs = []
    for rid in sorted(os.listdir(runs_dir), reverse=True):
        d = os.path.join(runs_dir, rid)
        if not os.path.isfile(os.path.join(d, "meta.json")):
            continue
        full = only is None or os.path.realpath(d) == only or not os.path.isfile(os.path.join(d, "index.html"))
        run = load_run(d, full=full)
        if full:
            render_run(run)
        runs.append(run)
    render_index(out_dir, runs)


if __name__ == "__main__":
    if len(sys.argv) in (3, 4) and sys.argv[1] == "build":
        build(sys.argv[2], sys.argv[3] if len(sys.argv) == 4 else None)
    elif len(sys.argv) == 3 and sys.argv[1] == "junit":
        sys.stdout.write(junit_summary(sys.argv[2]))
    else:
        sys.exit(__doc__)
