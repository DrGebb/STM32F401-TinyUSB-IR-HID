
#!/usr/bin/env python3
# Arm Compiler 6 / armlink MAP Analyzer
# Generates standalone interactive HTML dashboard.

import argparse
import html
import json
import re
from pathlib import Path


# MAP component columns:
# 0 + 1 = Code (inc. data)
# 2     = RO Data
# 3     = RW Data
# 4     = ZI Data
# 5     = Debug

ROW_RE = re.compile(
    r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+"
    r"(\d+)\s+(\d+)\s+(.+?)\s*$"
)

TOTAL_RE = re.compile(
    r"^\s*Total\s+(RO|RW|ROM)\s+Size\b.*?\s+(\d+)",
    re.IGNORECASE
)

REGION_RE = re.compile(
    r"^\s*(Load|Execution) Region\s+(\S+)\s+\((.*)\)\s*$"
)

UNUSED_RE = re.compile(
    r"(\d+)\s+unused section\(s\)\s+"
    r"\(total\s+(\d+)\s+bytes\)\s+removed",
    re.IGNORECASE
)


def parse_hex(pattern, text):
    m = re.search(pattern, text, re.IGNORECASE)
    return int(m.group(1), 16) if m else None


def parse_region(line):
    m = REGION_RE.match(line)

    if not m:
        return None

    kind, name, details = m.groups()

    return {
        "type": kind,
        "name": name,
        "base": parse_hex(
            r"\b(?:Base|Exec base):\s*(0x[0-9a-f]+)", details
        ),
        "size": parse_hex(r"\bSize:\s*(0x[0-9a-f]+)", details),
        "max": parse_hex(r"\bMax:\s*(0x[0-9a-f]+)", details),
        "attributes": details,
    }


def parse_map(path):
    lines = path.read_text(
        encoding="utf-8",
        errors="replace"
    ).splitlines()

    data = {
        "source": path.name,
        "totals": {},
        "regions": [],
        "objects": [],
        "libraries": [],
        "library_members": [],
        "component_totals": {},
        "unused_count": None,
        "unused_bytes": None,
        "warnings": [],
    }

    in_components = False
    table_type = None

    for line in lines:
        s = line.strip()

        region = parse_region(line)
        if region:
            data["regions"].append(region)

        total = TOTAL_RE.match(line)
        if total:
            data["totals"][total.group(1).upper()] = int(total.group(2))

        if s == "Image component sizes":
            in_components = True
            table_type = None
            continue

        if in_components:

            if "Object Name" in line:
                table_type = "objects"
                continue

            if "Library Member Name" in line:
                table_type = "library_members"
                continue

            if "Library Name" in line:
                table_type = "libraries"
                continue

            if s.startswith("Load Region ") or s.startswith("Execution Region "):
                in_components = False
                table_type = None

            row = ROW_RE.match(line)

            if row:
                values = [
                    int(row.group(i))
                    for i in range(1, 7)
                ]

                name = row.group(7).strip()

                record = {
                    "name": name,
                    "columns": values,
                }

                if (
                    name in (
                        "Object Totals",
                        "Library Totals",
                        "Grand Totals",
                        "ELF Image Totals",
                        "ROM Totals",
                    )
                    or name.startswith("(incl.")
                ):
                    data["component_totals"][name] = values

                elif table_type == "objects":
                    data["objects"].append(record)

                elif table_type == "libraries":
                    data["libraries"].append(record)

                elif table_type == "library_members":
                    data["library_members"].append(record)

        unused = UNUSED_RE.search(line)

        if unused:
            data["unused_count"] = int(unused.group(1))
            data["unused_bytes"] = int(unused.group(2))

    if not data["totals"]:
        data["warnings"].append("Linker totals not found.")

    if not data["objects"]:
        data["warnings"].append("Object table not detected.")

    return data


def object_metrics(item):
    c = item["columns"]

    code = c[0] + c[1]
    ro = c[2]
    rw = c[3]
    zi = c[4]
    debug = c[5]

    return {
        "code": code,
        "ro": ro,
        "rw": rw,
        "zi": zi,
        "debug": debug,
        "flash": code + ro + rw,
        "ram": rw + zi,
    }


def fmt(value):
    if value is None:
        return "—"

    return f"{value:,} B"


def kib(value):
    if value is None:
        return "—"

    return f"{value / 1024:.2f} KiB"


def esc(value):
    return html.escape(str(value))


def svg_donut(items, center_value, center_label):
    total = sum(value for _, value, _ in items)

    if total <= 0:
        return "<p class='muted'>No data</p>"

    radius = 65
    circumference = 2 * 3.141592653589793 * radius
    offset = 0

    colors = [
        "#38BDF8",
        "#A78BFA",
        "#34D399",
        "#FBBF24",
        "#F472B6",
    ]

    circles = ""
    legend = ""

    for index, (name, value, color) in enumerate(items):
        if value <= 0:
            continue

        length = circumference * value / total

        circles += f"""
        <circle
            cx="100" cy="100" r="{radius}"
            fill="none"
            stroke="{color}"
            stroke-width="19"
            stroke-dasharray="{length:.2f} {circumference-length:.2f}"
            stroke-dashoffset="{-offset:.2f}"
            transform="rotate(-90 100 100)">
            <title>{esc(name)}: {fmt(value)} ({value/total*100:.1f}%)</title>
        </circle>
        """

        offset += length

        legend += f"""
        <div class="legend-row">
            <span class="legend-dot" style="background:{color}"></span>
            <span>{esc(name)}</span>
            <strong>{value/total*100:.1f}%</strong>
        </div>
        """

    return f"""
    <div class="donut-layout">
        <svg viewBox="0 0 200 200" class="donut">
            <circle cx="100" cy="100" r="{radius}"
                fill="none" stroke="#263244" stroke-width="19"/>
            {circles}
            <text x="100" y="96" text-anchor="middle"
                class="donut-value">{esc(center_value)}</text>
            <text x="100" y="115" text-anchor="middle"
                class="donut-label">{esc(center_label)}</text>
        </svg>
        <div class="legend">{legend}</div>
    </div>
    """


def svg_bars(items, color="#38BDF8"):
    if not items:
        return "<p class='muted'>No data</p>"

    max_value = max(value for _, value in items) or 1
    rows = ""

    for name, value in items:
        width = max(0, value / max_value * 100)

        rows += f"""
        <div class="bar-row">
            <div class="bar-name" title="{esc(name)}">{esc(name)}</div>
            <div class="bar-track">
                <div class="bar-fill"
                    style="width:{width:.2f}%;background:{color}">
                </div>
            </div>
            <div class="bar-value">{kib(value)}</div>
        </div>
        """

    return f"<div class='bars'>{rows}</div>"


def make_table(headers, rows):
    if not rows:
        return "<p class='muted'>No data</p>"

    head = "".join(
        f"<th>{esc(x)}</th>"
        for x in headers
    )

    body = ""

    for row in rows:
        cells = "".join(
            f"<td>{esc(x)}</td>"
            for x in row
        )
        body += f"<tr>{cells}</tr>"

    return f"""
    <div class="table-wrap">
        <table>
            <thead><tr>{head}</tr></thead>
            <tbody>{body}</tbody>
        </table>
    </div>
    """


def make_html(data):
    totals = data["totals"]

    flash = totals.get("ROM")
    ro = totals.get("RO")
    ram = totals.get("RW")

    # Object table
    objects = []

    for item in data["objects"]:
        m = object_metrics(item)

        objects.append({
            "name": item["name"],
            **m,
        })

    objects.sort(
        key=lambda x: x["flash"],
        reverse=True
    )

    top_objects = objects[:15]

    # Flash composition:
    # Code + RO Data + RW Data
    code = 0
    ro_data = 0
    rw_data = 0
    zi_data = 0

    component_totals = data["component_totals"]

    grand = component_totals.get("Grand Totals")

    if grand and len(grand) >= 6:
        code = grand[0] + grand[1]
        ro_data = grand[2]
        rw_data = grand[3]
        zi_data = grand[4]

    flash_chart = svg_donut(
        [
            ("Code", code, "#38BDF8"),
            ("RO Data", ro_data, "#A78BFA"),
            ("RW Data", rw_data, "#34D399"),
        ],
        kib(flash) if flash is not None else "—",
        "FLASH",
    )

    ram_chart = svg_donut(
        [
            ("RW Data", rw_data, "#34D399"),
            ("ZI Data", zi_data, "#FBBF24"),
        ],
        kib(ram) if ram is not None else "—",
        "RAM",
    )

    # Top objects
    top_chart = svg_bars(
        [(x["name"], x["flash"]) for x in top_objects],
        "#38BDF8"
    )

    # Libraries
    library_items = []

    for item in data["libraries"]:
        m = object_metrics(item)

        if m["flash"] > 0:
            library_items.append(
                (item["name"], m["flash"])
            )

    library_items.sort(
        key=lambda x: x[1],
        reverse=True
    )

    library_chart = svg_bars(
        library_items[:15],
        "#A78BFA"
    )

    # Object table
    object_rows = []

    for x in objects:
        object_rows.append([
            x["name"],
            fmt(x["code"]),
            fmt(x["ro"]),
            fmt(x["rw"]),
            fmt(x["zi"]),
            fmt(x["flash"]),
            fmt(x["ram"]),
        ])

    object_table = make_table(
        [
            "Object",
            "Code",
            "RO Data",
            "RW Data",
            "ZI Data",
            "Flash",
            "RAM",
        ],
        object_rows
    )

    # Regions
    region_rows = []

    for r in data["regions"]:
        base = r["base"]

        region_rows.append([
            r["type"],
            r["name"],
            f"0x{base:08X}" if base is not None else "—",
            fmt(r["size"]),
            fmt(r["max"]),
            r["attributes"],
        ])

    region_table = make_table(
        ["Type", "Region", "Base", "Size", "Max", "Attributes"],
        region_rows
    )

    # Memory utilization
    flash_max = max(
        (r["max"] or 0)
        for r in data["regions"]
        if r["type"] == "Load"
    ) if any(r["type"] == "Load" for r in data["regions"]) else 0

    ram_max = max(
        (r["max"] or 0)
        for r in data["regions"]
        if r["type"] == "Execution"
        and r["name"].startswith("RW_")
    ) if any(
        r["type"] == "Execution"
        and r["name"].startswith("RW_")
        for r in data["regions"]
    ) else 0

    flash_pct = (
        min(100, flash / flash_max * 100)
        if flash is not None and flash_max else 0
    )

    ram_pct = (
        min(100, ram / ram_max * 100)
        if ram is not None and ram_max else 0
    )

    def progress(value):
        return f"{value:.1f}%"

    # Metric cards
    cards = f"""
    <div class="metric-grid">

        <div class="metric-card">
            <div class="metric-label">FLASH IMAGE</div>
            <div class="metric-value">{kib(flash)}</div>
            <div class="metric-sub">Code + RO Data + RW Data</div>
            <div class="progress">
                <div style="width:{flash_pct:.2f}%;background:#38BDF8"></div>
            </div>
            <div class="metric-foot">
                {progress(flash_pct)} of configured region
            </div>
        </div>

        <div class="metric-card">
            <div class="metric-label">STATIC RAM</div>
            <div class="metric-value">{kib(ram)}</div>
            <div class="metric-sub">RW Data + ZI Data</div>
            <div class="progress">
                <div style="width:{ram_pct:.2f}%;background:#34D399"></div>
            </div>
            <div class="metric-foot">
                {progress(ram_pct)} of detected RAM region
            </div>
        </div>

        <div class="metric-card">
            <div class="metric-label">OBJECT FILES</div>
            <div class="metric-value">{len(objects)}</div>
            <div class="metric-sub">Parsed from MAP</div>
        </div>

        <div class="metric-card">
            <div class="metric-label">UNUSED SECTIONS</div>
            <div class="metric-value">
                {data["unused_count"] if data["unused_count"] is not None else "—"}
            </div>
            <div class="metric-sub">
                {fmt(data["unused_bytes"])}
            </div>
        </div>

    </div>
    """

    warnings = "".join(
        f"<li>{esc(x)}</li>"
        for x in data["warnings"]
    )

    if not warnings:
        warnings = "<li>No parser warnings.</li>"

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">

<title>Arm MAP Analyzer — {esc(data['source'])}</title>

<style>
:root {{
    color-scheme: dark;
    --bg: #0b1120;
    --panel: #131d2e;
    --panel2: #19253a;
    --border: #26364e;
    --text: #e5edf8;
    --muted: #8ea0b9;
    --cyan: #38bdf8;
    --green: #34d399;
    --purple: #a78bfa;
}}

* {{
    box-sizing: border-box;
}}

body {{
    margin: 0;
    padding: 28px;
    background: var(--bg);
    color: var(--text);
    font: 14px/1.5 "Segoe UI", Arial, sans-serif;
}}

main {{
    max-width: 1450px;
    margin: auto;
}}

header {{
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 20px;
    margin-bottom: 28px;
}}

h1 {{
    margin: 0;
    font-size: 28px;
    letter-spacing: -0.5px;
}}

h2 {{
    font-size: 17px;
    margin: 0 0 18px;
}}

h3 {{
    font-size: 14px;
    margin: 0 0 15px;
}}

.muted {{
    color: var(--muted);
}}

.subtitle {{
    color: var(--muted);
    margin-top: 5px;
}}

.panel {{
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 20px;
    min-width: 0;
}}

.metric-grid {{
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 14px;
    margin-bottom: 18px;
}}

.metric-card {{
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 18px;
    min-width: 0;
}}

.metric-label {{
    font-size: 11px;
    color: var(--muted);
    letter-spacing: 1px;
    font-weight: 700;
}}

.metric-value {{
    font-size: 27px;
    font-weight: 700;
    margin-top: 7px;
}}

.metric-sub {{
    font-size: 12px;
    color: var(--muted);
    margin-top: 3px;
}}

.metric-foot {{
    color: var(--muted);
    font-size: 11px;
    margin-top: 7px;
}}

.progress {{
    background: #25354b;
    height: 7px;
    border-radius: 20px;
    overflow: hidden;
    margin-top: 15px;
}}

.progress div {{
    height: 100%;
    border-radius: 20px;
}}

.grid-two {{
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 18px;
    margin-bottom: 18px;
}}

.donut-layout {{
    display: flex;
    align-items: center;
    justify-content: space-around;
    flex-wrap: wrap;
    gap: 10px;
}}

.donut {{
    width: 190px;
    max-width: 48%;
}}

.donut-value {{
    fill: var(--text);
    font-size: 18px;
    font-weight: 700;
}}

.donut-label {{
    fill: var(--muted);
    font-size: 10px;
}}

.legend {{
    min-width: 125px;
    flex: 1;
}}

.legend-row {{
    display: flex;
    align-items: center;
    gap: 8px;
    margin: 10px 0;
    font-size: 12px;
}}

.legend-row strong {{
    margin-left: auto;
}}

.legend-dot {{
    width: 9px;
    height: 9px;
    border-radius: 50%;
    flex-shrink: 0;
}}

.bars {{
    display: flex;
    flex-direction: column;
    gap: 10px;
}}

.bar-row {{
    display: grid;
    grid-template-columns: minmax(100px, 1.3fr) 2fr minmax(65px, auto);
    gap: 12px;
    align-items: center;
}}

.bar-name {{
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    color: #cbd8e9;
    font-size: 12px;
}}

.bar-track {{
    height: 10px;
    background: #26364d;
    border-radius: 10px;
    overflow: hidden;
}}

.bar-fill {{
    height: 100%;
    border-radius: 10px;
    transition: width .3s ease;
}}

.bar-value {{
    color: var(--muted);
    text-align: right;
    font-size: 11px;
    white-space: nowrap;
}}

.table-wrap {{
    overflow-x: auto;
    max-height: 600px;
    border: 1px solid var(--border);
    border-radius: 8px;
}}

table {{
    border-collapse: collapse;
    width: 100%;
    white-space: nowrap;
    font-size: 12px;
}}

th, td {{
    padding: 9px 12px;
    text-align: left;
    border-bottom: 1px solid var(--border);
}}

th {{
    position: sticky;
    top: 0;
    background: var(--panel2);
    color: #a9bdd7;
    z-index: 1;
}}

tbody tr:hover {{
    background: #1d2b40;
}}

tbody tr:last-child td {{
    border-bottom: none;
}}

.section {{
    margin-bottom: 18px;
}}

.badge {{
    border: 1px solid var(--border);
    border-radius: 20px;
    padding: 5px 10px;
    color: var(--muted);
    font-size: 11px;
}}

footer {{
    color: var(--muted);
    font-size: 11px;
    text-align: center;
    margin: 25px 0 5px;
}}

@media(max-width: 950px) {{
    .metric-grid {{
        grid-template-columns: repeat(2, 1fr);
    }}
}}

@media(max-width: 700px) {{
    body {{ padding: 14px; }}
    .grid-two {{ grid-template-columns: 1fr; }}
    header {{ align-items: flex-start; flex-direction: column; }}
    .metric-grid {{ grid-template-columns: 1fr 1fr; }}
}}

@media(max-width: 420px) {{
    .metric-grid {{ grid-template-columns: 1fr; }}
}}
</style>
</head>

<body>
<main>

<header>
    <div>
        <h1>Arm MAP Analyzer</h1>
        <div class="subtitle">{esc(data['source'])}</div>
    </div>
    <div class="badge">ARM COMPILER 6 · ARMLINK</div>
</header>

{cards}

<div class="grid-two section">

    <section class="panel">
        <h2>Flash composition</h2>
        {flash_chart}
    </section>

    <section class="panel">
        <h2>RAM composition</h2>
        {ram_chart}
    </section>

</div>

<div class="grid-two section">

    <section class="panel">
        <h2>Top 15 objects by Flash usage</h2>
        {top_chart}
    </section>

    <section class="panel">
        <h2>Library Flash usage</h2>
        {library_chart}
    </section>

</div>

<section class="panel section">
    <h2>Object details</h2>
    <p class="muted">
        Code = Code (inc. data). Flash = Code + RO Data + RW Data.
        RAM = RW Data + ZI Data.
    </p>
    {object_table}
</section>

<section class="panel section">
    <h2>Memory regions</h2>
    {region_table}
</section>

<section class="panel section">
    <h2>Parser diagnostics</h2>
    <ul>{warnings}</ul>
</section>

<footer>
    Generated by arm_map_analyzer.py · Standalone HTML report
</footer>

</main>
</body>
</html>
"""


def main():
    parser = argparse.ArgumentParser(
        description="Arm Compiler 6 MAP Analyzer"
    )

    parser.add_argument("map_file", type=Path)
    parser.add_argument(
        "-o", "--output",
        required=True,
        type=Path
    )
    parser.add_argument(
        "--json",
        type=Path,
        help="Optional JSON output"
    )

    args = parser.parse_args()

    if not args.map_file.is_file():
        parser.error(f"MAP file not found: {args.map_file}")

    try:
        data = parse_map(args.map_file)

        args.output.parent.mkdir(
            parents=True,
            exist_ok=True
        )

        args.output.write_text(
            make_html(data),
            encoding="utf-8"
        )

        if args.json:
            args.json.parent.mkdir(
                parents=True,
                exist_ok=True
            )

            args.json.write_text(
                json.dumps(data, indent=2, ensure_ascii=False),
                encoding="utf-8"
            )

    except OSError as error:
        parser.error(str(error))

    print("Arm MAP Analyzer")
    print("----------------")
    print(f"MAP: {args.map_file}")
    print(f"Objects: {len(data['objects'])}")
    print(f"Flash: {fmt(data['totals'].get('ROM'))}")
    print(f"RAM: {fmt(data['totals'].get('RW'))}")
    print(f"HTML: {args.output}")

    for warning in data["warnings"]:
        print(f"WARNING: {warning}")


if __name__ == "__main__":
    main()
