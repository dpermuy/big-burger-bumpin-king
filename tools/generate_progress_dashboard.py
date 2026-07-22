#!/usr/bin/env python3
"""Generates the static progress dashboard from live repo state.

Reads host/kernel_imports.txt, host/kernel_impl.cpp, host/kernel_stubs.cpp,
private/ppc/*.cpp, git history, and the phase3 findings log, then writes a
self-contained HTML dashboard to site/index.html. Run by
.github/workflows/progress-dashboard.yml on every push to the default
branch; safe to run locally too (no side effects outside site/).
"""

import html
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "site"

FINDINGS_LOG = ROOT / "docs/superpowers/specs/phase3-past-loading-screen-investigation.txt"

# Boot-progress milestones, in order. "current" is inferred as the last
# milestone whose marker text appears in the findings log; edit this list
# as the investigation reaches new named stages.
MILESTONES = [
    ("Guest memory\nimage loaded", "Guest memory image ready"),
    ("Disc I/O +\nAPC delivery", "Finding 11"),
    ("Loading-screen\nasset load", "Finding 12"),
    ("Boot retry loop\n(sub_8212DAE8)", "Finding 16"),
    ("First rendered\nframe", None),  # not yet reached
]

PREFIX_RE = re.compile(r"^(Nt|Ke|Rtl|Mm|Ex|Xam|Vd|Xex2|Xex|NetDll|X|Dbg|Hal|Obj|Io|__)")


def run(cmd):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True).stdout


def categorize(name):
    m = PREFIX_RE.match(name)
    if not m:
        return "other"
    p = m.group(1)
    return "CRT/other" if p == "__" else p


def extract_impl_names(path, macro_prefix="PPC_FUNC"):
    text = path.read_text()
    return set(re.findall(rf"{macro_prefix}\(__imp__([A-Za-z0-9_]+)\)", text))


def kernel_coverage():
    real = extract_impl_names(ROOT / "host/kernel_impl.cpp")
    stub = extract_impl_names(ROOT / "host/kernel_stubs.cpp")

    groups = defaultdict(lambda: [0, 0])
    for n in real:
        groups[categorize(n)][0] += 1
    for n in stub:
        groups[categorize(n)][1] += 1

    # Roll anything under 4 total into "misc" for a scannable chart.
    rows = []
    misc = [0, 0]
    for name, (r, s) in groups.items():
        if r + s < 4:
            misc[0] += r
            misc[1] += s
        else:
            rows.append((name, r, s))
    if misc[0] + misc[1] > 0:
        rows.append(("misc", misc[0], misc[1]))

    rows.sort(key=lambda row: -(row[1] + row[2]))
    return rows, len(real), len(real) + len(stub)


def function_stats():
    # private/ppc is XenonRecomp-generated output and is gitignored (see .gitignore) --
    # it never exists in a fresh CI checkout, only in a developer's local worktree after
    # running the generator manually. Returning (None, None) here (rather than 0, 0) lets
    # the dashboard say "not available" instead of falsely implying zero functions have
    # been recompiled.
    ppc_dir = ROOT / "private/ppc"
    total_lines = 0
    total_funcs = 0
    found_any = False
    if ppc_dir.exists():
        for f in ppc_dir.glob("*.cpp"):
            found_any = True
            text = f.read_text(errors="ignore")
            total_lines += text.count("\n")
            total_funcs += len(re.findall(r"PPC_FUNC_IMPL\(__imp__sub_", text))
    return (total_funcs, total_lines) if found_any else (None, None)


def git_stats():
    count = int(run(["git", "log", "--oneline"]).count("\n"))
    dates = run(["git", "log", "--format=%ad", "--date=short"]).splitlines()
    first, last = (dates[-1], dates[0]) if dates else ("?", "?")
    return count, first, last


def parse_findings():
    if not FINDINGS_LOG.exists():
        return []
    text = FINDINGS_LOG.read_text()
    # Finding headers can wrap across several lines before the closing "---"
    # (e.g. "--- Finding 5: symbol breakpoints are unreliable under this\n
    # build's -O3, and the ---"); match across newlines and stop at the
    # nearest "---", which is always the real closing delimiter since no
    # finding title contains a literal "---".
    headers = list(re.finditer(r"^--- (Finding \d+): (.+?) ---\s*$", text, re.MULTILINE | re.DOTALL))
    findings = []
    for i, m in enumerate(headers):
        num = re.search(r"\d+", m.group(1)).group(0)
        title = re.sub(r"\s+", " ", m.group(2)).strip()
        title = title[:1].upper() + title[1:] if title else title
        # Grab the first real sentence of the body as a one-line summary.
        body_start = m.end()
        body_end = headers[i + 1].start() if i + 1 < len(headers) else len(text)
        body = text[body_start:body_end].strip()
        body = re.sub(r"\s+", " ", body)
        summary = body.split(". ")[0].strip()
        if summary and not summary.endswith("."):
            summary += "."
        findings.append({
            "num": num,
            "title": html.escape(title),
            "summary": html.escape(summary[:220]),
        })
    return findings


def render_kpi(value, label):
    return f'''<div class="kpi">
      <div class="kpi-value">{value}</div>
      <div class="kpi-label">{label}</div>
    </div>'''


def render_coverage_row(name, real, total, head=False):
    if head:
        return ""
    pct = round(100 * real / total) if total else 0
    return (f'<div class="cov-row"><div class="cov-name">{name}</div>'
            f'<div class="cov-bar-track"><div class="cov-bar-fill" style="width:{pct}%"></div></div>'
            f'<div class="cov-count"><b>{real}</b> / {total}</div></div>')


def render_finding(f):
    pivotal = "PIVOTAL" in f["title"].upper() or f["title"].startswith("Root cause") or "ROOT MECHANISM" in f["title"].upper()
    cls = "finding pivotal" if pivotal else "finding"
    tag = '<span class="f-tag">pivotal</span>' if pivotal else ""
    return (f'<div class="{cls}"><div class="f-num">{int(f["num"]):02d}</div>'
            f'<div><p class="f-title">{f["title"]}{tag}</p>'
            f'<p class="f-desc">{f["summary"]}</p></div></div>')


def render_milestones(findings):
    titles_seen = {f["num"] for f in findings}
    current_idx = 0
    for i, (_, marker) in enumerate(MILESTONES):
        if marker is None:
            continue
        m = re.search(r"\d+", marker)
        if m and m.group(0) in titles_seen:
            current_idx = i
    total = len(MILESTONES)
    fill_pct = round(100 * (current_idx + 0.5) / total)
    stops_html = []
    for i, (label, _) in enumerate(MILESTONES):
        left = round(100 * (i + 0.5) / total)
        state = "done" if i < current_idx else ("current" if i == current_idx else "")
        stops_html.append(
            f'<div class="stop {state}" style="left:{left}%">'
            f'<div class="stop-dot"></div><div class="stop-label">{label}</div></div>'
        )
    return fill_pct, "\n".join(stops_html), current_idx


def main():
    coverage_rows, real_total, all_total = kernel_coverage()
    func_count, line_count = function_stats()
    commit_count, first_date, last_date = git_stats()
    findings = parse_findings()
    fill_pct, stops_html, current_idx = render_milestones(findings)

    kpis = "\n".join([
        render_kpi(f"{func_count:,}" if func_count is not None else "N/A", "Game functions recompiled"),
        render_kpi(f'{real_total}<small>&nbsp;/&nbsp;{all_total}</small>', "Kernel imports, real vs. stub"),
        render_kpi(str(len(findings)), "Findings this investigation"),
        render_kpi(f"{line_count / 1_000_000:.1f}M" if line_count is not None else "N/A", "Lines of generated C++"),
        render_kpi(f"{commit_count}", f"Commits / {first_date} → {last_date}"),
    ])

    coverage_html = "\n".join(render_coverage_row(name, r, r + s) for name, r, s in coverage_rows)

    findings_html = "\n".join(render_finding(f) for f in reversed(findings)) if findings else "<p style=\"padding:16px\">No findings logged yet.</p>"

    current_blocker = ""
    if findings:
        latest = findings[-1]
        current_blocker = f'<b>Latest —</b> Finding {latest["num"]}: {latest["title"]}. {latest["summary"]}'

    template = (ROOT / "tools/dashboard_template.html").read_text()
    html = (template
            .replace("{{KPIS}}", kpis)
            .replace("{{COVERAGE_ROWS}}", coverage_html)
            .replace("{{FINDINGS}}", findings_html)
            .replace("{{MILESTONE_FILL}}", str(fill_pct))
            .replace("{{MILESTONE_STOPS}}", stops_html)
            .replace("{{CURRENT_BLOCKER}}", current_blocker)
            .replace("{{GENERATED_DATE}}", last_date))

    OUT_DIR.mkdir(exist_ok=True)
    (OUT_DIR / "index.html").write_text(html)
    print(f"Wrote {OUT_DIR / 'index.html'} "
          f"({func_count} functions, {real_total}/{all_total} kernel imports, "
          f"{len(findings)} findings, {commit_count} commits)")


if __name__ == "__main__":
    sys.exit(main())
