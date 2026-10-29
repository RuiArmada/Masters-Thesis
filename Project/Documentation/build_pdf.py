#!/usr/bin/env python3
"""Build Boid_Pedestrian_Simulation_Documentation.pdf from Documentation/*.md."""

from __future__ import annotations

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent
OUT_PDF = ROOT / "Boid_Pedestrian_Simulation_Documentation.pdf"
ASSETS = ROOT / "_pdf_assets"
CSS_PATH = ASSETS / "report.css"
HTML_PATH = ASSETS / "report.html"
MD_PATH = ASSETS / "report.md"

CHROME = Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe")


def read_md(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def strip_internal_links(text: str) -> str:
    # [label](01_Something.md) -> label ; keep http links
    def repl(m: re.Match) -> str:
        label, href = m.group(1), m.group(2)
        if href.startswith("http"):
            return m.group(0)
        return label

    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", repl, text)
    text = re.sub(r"(?m)^## Related documents\n(?:.*\n)*?(?=^## |\Z)", "", text)
    text = re.sub(r"(?m)^## Table of contents\n(?:.*\n)*?(?=^## |\Z)", "", text)
    return text


def demote_headings(text: str, levels: int = 1) -> str:
    """Shift # headings down by `levels` (max # stays within ### for body)."""
    lines = []
    for line in text.splitlines():
        m = re.match(r"^(#{1,6})\s+(.*)$", line)
        if m:
            hashes = "#" * min(6, len(m.group(1)) + levels)
            lines.append(f"{hashes} {m.group(2)}")
        else:
            lines.append(line)
    return "\n".join(lines)


def extract_and_render_mermaid(text: str) -> str:
    """Replace ```mermaid blocks with PNG images via mmdc."""
    ASSETS.mkdir(parents=True, exist_ok=True)
    pattern = re.compile(r"```mermaid\n(.*?)```", re.DOTALL)
    idx = [0]

    def replace(m: re.Match) -> str:
        idx[0] += 1
        src = m.group(1).strip() + "\n"
        mmd = ASSETS / f"diagram_{idx[0]}.mmd"
        png = ASSETS / f"diagram_{idx[0]}.png"
        mmd.write_text(src, encoding="utf-8")
        try:
            subprocess.run(
                ["npx", "--yes", "@mermaid-js/mermaid-cli", "-i", str(mmd), "-o", str(png),
                 "-b", "white", "-s", "2"],
                check=True,
                capture_output=True,
                text=True,
                shell=True,
            )
        except subprocess.CalledProcessError as e:
            print(f"Mermaid render failed for diagram {idx[0]}: {e.stderr or e}")
            return (
                "\n*Architecture diagram (Mermaid source retained below).*\n\n"
                f"```\n{src}\n```\n"
            )
        # Use file URI relative for pandoc --resource-path
        rel = png.name
        return f"\n![Architecture diagram {idx[0]}]({rel})\n\n"

    return pattern.sub(replace, text)


def build_markdown() -> str:
    parts: list[str] = []

    parts.append("# 1. Introduction\n")
    overview = strip_internal_links(read_md("01_Project_Overview.md"))
    # Drop the top H1 and TOC already stripped; demote remaining
    overview = re.sub(r"(?m)^# Project Overview\s*\n+", "", overview)
    overview = demote_headings(overview, 1)
    parts.append(overview)
    parts.append("\n")

    setup = strip_internal_links(read_md("02_Setup_Guide.md"))
    setup = re.sub(r"(?m)^# Setup Guide\s*\n+", "", setup)
    setup = demote_headings(setup, 1)
    parts.append("## Setup and runtime notes\n")
    parts.append(setup)
    parts.append("\n")

    parts.append("# 2. System Architecture\n")
    arch = strip_internal_links(read_md("03_System_Architecture.md"))
    arch = re.sub(r"(?m)^# System Architecture\s*\n+", "", arch)
    arch = demote_headings(arch, 1)
    parts.append(arch)
    parts.append("\n")

    parts.append("# 3. XML Schedule System\n")
    xml = strip_internal_links(read_md("04_XML_Guide.md"))
    xml = re.sub(r"(?m)^# XML Guide\s*\n+", "", xml)
    xml = demote_headings(xml, 1)
    parts.append(xml)
    parts.append("\n")

    parts.append("# 4. Use Cases\n")
    uc = strip_internal_links(read_md("05_Use_Cases.md"))
    uc = re.sub(r"(?m)^# Use Cases\s*\n+", "", uc)
    uc = demote_headings(uc, 1)
    # No screenshots exist in the repository.
    parts.append(
        "Screenshots were not available in the repository, so the use-case write-ups "
        "refer to the maps and schedules by name only "
        "(`Evacuation.umap`, `DailyUrbanLife.umap`, `MallA.umap`, `MallB.umap`).\n\n"
    )
    parts.append(uc)
    parts.append("\n")

    parts.append("# 5. Validation and Verification\n")
    val = strip_internal_links(read_md("06_Validation_and_Verification.md"))
    val = re.sub(r"(?m)^# Validation and Verification\s*\n+", "", val)
    val = demote_headings(val, 1)
    parts.append(val)
    parts.append("\n")

    parts.append("## Debug panel\n")
    dbg = strip_internal_links(read_md("07_Debug_Panel.md"))
    dbg = re.sub(r"(?m)^# Debug Panel\s*\n+", "", dbg)
    dbg = demote_headings(dbg, 1)
    parts.append(dbg)
    parts.append("\n")

    parts.append("# 6. Configuration\n")
    cfg = strip_internal_links(read_md("08_Configuration.md"))
    cfg = re.sub(r"(?m)^# Configuration\s*\n+", "", cfg)
    cfg = demote_headings(cfg, 1)
    parts.append(cfg)
    parts.append("\n")

    parts.append("# 7. Core Classes\n")
    api = strip_internal_links(read_md("09_API_and_Classes.md"))
    api = re.sub(r"(?m)^# API and Classes\s*\n+", "", api)
    api = demote_headings(api, 1)
    parts.append(api)
    parts.append("\n")

    parts.append("# 8. Extending the Project\n")
    ext = strip_internal_links(read_md("10_Extending_the_Project.md"))
    ext = re.sub(r"(?m)^# Extending the Project\s*\n+", "", ext)
    ext = demote_headings(ext, 1)
    parts.append(ext)
    parts.append("\n")

    parts.append("# 9. Known Limitations\n")
    lim = strip_internal_links(read_md("11_Known_Limitations.md"))
    lim = re.sub(r"(?m)^# Known Limitations\s*\n+", "", lim)
    lim = demote_headings(lim, 1)
    parts.append(lim)
    parts.append("\n")

    parts.append(
        """# 10. Conclusion

What exists today is a working Unreal 5.4 pedestrian simulation with flocking, sidewalk-filtered NavMesh paths, XML schedules, and capacity-limited stations. On that base sit three demos: timed evacuation with exit queues and CSV export; a compressed daily-life schedule with homes, desks, needs, and breaks; and a Mall A / Mall B comparison with infrastructure metrics and a short V&V summary when both runs have been exported. Validation maps V01–V06 plus the on-screen statistics panel cover simpler flow experiments.

Config is mostly `SimulationConfig.xml` plus editor properties on `AFlockController` and stations. The limitations chapter lists the sharp edges (legacy RuleParser paths, no validation CSV, map assumptions, and so on).
"""
    )

    body = "\n".join(parts)
    body = extract_and_render_mermaid(body)
    return body


CSS = """
@page {
  size: A4;
  margin: 20mm 18mm 18mm 18mm;
}

html {
  font-family: "Segoe UI", Helvetica, Arial, sans-serif;
  font-size: 10.5pt;
  line-height: 1.45;
  color: #1a1a1a;
}

body {
  max-width: none;
}

header#title-block-header {
  page-break-after: always;
  padding-top: 55mm;
  text-align: center;
  min-height: 200mm;
}

header#title-block-header h1.title {
  font-size: 26pt;
  font-weight: 600;
  margin: 0 0 14pt 0;
  color: #111;
  border: none;
}

header#title-block-header p.subtitle {
  font-size: 14pt;
  color: #333;
  margin: 0 0 40pt 0;
}

header#title-block-header p.author,
header#title-block-header p.date {
  font-size: 11pt;
  color: #444;
  margin: 4pt 0;
}

.title-meta {
  margin-top: 40pt;
  font-size: 10.5pt;
  color: #333;
  line-height: 1.6;
}

nav#TOC {
  page-break-after: always;
}

nav#TOC > h2 {
  font-size: 16pt;
  border-bottom: 1px solid #ccc;
  padding-bottom: 6pt;
  page-break-before: avoid;
}

nav#TOC ul {
  list-style: none;
  padding-left: 0;
}

nav#TOC > ul > li {
  margin: 6pt 0;
  font-weight: 600;
}

nav#TOC ul ul {
  padding-left: 14pt;
  font-weight: 400;
}

nav#TOC a {
  color: #111;
  text-decoration: none;
}

h1 {
  font-size: 16pt;
  font-weight: 600;
  margin-top: 18pt;
  margin-bottom: 10pt;
  page-break-before: always;
  border-bottom: 1px solid #bbb;
  padding-bottom: 4pt;
}

nav#TOC + h1,
body > h1:first-of-type {
  page-break-before: avoid;
}

h2 {
  font-size: 13pt;
  font-weight: 600;
  margin-top: 16pt;
  margin-bottom: 6pt;
  page-break-after: avoid;
}

h3 {
  font-size: 11.5pt;
  font-weight: 600;
  margin-top: 12pt;
  margin-bottom: 4pt;
  page-break-after: avoid;
}

h4 {
  font-size: 10.5pt;
  font-weight: 600;
  margin-top: 10pt;
}

p, li {
  orphans: 3;
  widows: 3;
}

table {
  border-collapse: collapse;
  width: 100%;
  margin: 10pt 0 14pt 0;
  font-size: 9.5pt;
  page-break-inside: avoid;
}

th, td {
  border: 1px solid #ccc;
  padding: 4pt 6pt;
  vertical-align: top;
  text-align: left;
}

th {
  background: #f0f0f0;
  font-weight: 600;
}

code {
  font-family: Consolas, "Courier New", monospace;
  font-size: 9pt;
  background: #f5f5f5;
  padding: 0 2pt;
}

pre {
  background: #f5f5f5;
  border: 1px solid #ddd;
  padding: 8pt 10pt;
  font-size: 8.5pt;
  line-height: 1.35;
  overflow-x: auto;
  page-break-inside: avoid;
  white-space: pre-wrap;
  word-break: break-word;
}

pre code {
  background: transparent;
  padding: 0;
}

blockquote {
  margin: 10pt 0;
  padding: 6pt 12pt;
  border-left: 3px solid #999;
  color: #333;
  background: #fafafa;
}

img {
  max-width: 95%;
  height: auto;
  display: block;
  margin: 12pt auto;
  page-break-inside: avoid;
}

hr {
  border: none;
  border-top: 1px solid #ddd;
  margin: 16pt 0;
}
"""


def pandoc_to_html(md_path: Path, html_path: Path, css_path: Path) -> None:
    cmd = [
        "pandoc",
        str(md_path),
        "-f", "markdown+pipe_tables+fenced_code_attributes+raw_html",
        "-t", "html5",
        "-s",
        "--toc",
        "--toc-depth=3",
        "--metadata", "toc-title=Table of Contents",
        f"--css={css_path.name}",
        f"--resource-path={ASSETS}",
        "-o", str(html_path),
    ]
    subprocess.run(cmd, check=True, cwd=str(ASSETS))


def chrome_print_pdf(html_path: Path, pdf_path: Path) -> None:
    uri = html_path.resolve().as_uri()
    out = str(pdf_path.resolve())
    cmd = [
        str(CHROME),
        "--headless=new",
        "--disable-gpu",
        "--no-pdf-header-footer",
        f"--print-to-pdf={out}",
        uri,
    ]
    subprocess.run(cmd, check=True, capture_output=True, text=True)


def stamp_page_numbers(pdf_path: Path) -> None:
    """Overlay page numbers; Chrome does not reliably honour CSS @page margin boxes."""
    from io import BytesIO

    from pypdf import PdfReader, PdfWriter
    from reportlab.lib.pagesizes import A4
    from reportlab.pdfgen import canvas

    reader = PdfReader(str(pdf_path))
    writer = PdfWriter()
    total = len(reader.pages)

    for i, page in enumerate(reader.pages):
        packet = BytesIO()
        c = canvas.Canvas(packet, pagesize=A4)
        c.setFont("Helvetica", 9)
        c.setFillColorRGB(0.35, 0.35, 0.35)
        label = f"{i + 1} / {total}"
        c.drawCentredString(A4[0] / 2.0, 12, label)
        if i > 0:
            c.setFont("Helvetica", 8)
            c.drawRightString(A4[0] - 18, 12, "Boid-Based Pedestrian Simulation")
        c.save()
        packet.seek(0)
        overlay = PdfReader(packet)
        page.merge_page(overlay.pages[0])
        writer.add_page(page)

    tmp = pdf_path.with_suffix(".stamped.pdf")
    with tmp.open("wb") as f:
        writer.write(f)
    tmp.replace(pdf_path)


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    print("Assembling markdown...")
    md = build_markdown()
    MD_PATH.write_text(md, encoding="utf-8")
    CSS_PATH.write_text(CSS, encoding="utf-8")

    print("Converting with pandoc...")
    pandoc_to_html(MD_PATH, HTML_PATH, CSS_PATH)

    html = HTML_PATH.read_text(encoding="utf-8")
    css = CSS_PATH.read_text(encoding="utf-8")
    if "report.css" in html:
        html = re.sub(
            r'<link[^>]*report\.css[^>]*>',
            f"<style>\n{css}\n</style>",
            html,
            count=1,
        )
    else:
        html = html.replace("</head>", f"<style>\n{css}\n</style>\n</head>")

    def abs_img(m: re.Match) -> str:
        src = m.group(1)
        p = (ASSETS / src).resolve()
        if p.exists():
            return f'src="{p.as_uri()}"'
        return m.group(0)

    html = re.sub(r'src="([^"]+\.png)"', abs_img, html)
    HTML_PATH.write_text(html, encoding="utf-8")

    print(f"Printing PDF via Chrome -> {OUT_PDF}")
    chrome_print_pdf(HTML_PATH, OUT_PDF)
    print("Stamping page numbers...")
    stamp_page_numbers(OUT_PDF)

    if not OUT_PDF.exists() or OUT_PDF.stat().st_size < 1000:
        raise SystemExit(f"PDF missing or too small: {OUT_PDF}")
    print(f"OK: {OUT_PDF} ({OUT_PDF.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
