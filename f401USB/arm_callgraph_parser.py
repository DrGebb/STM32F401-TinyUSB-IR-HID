#!/usr/bin/env python3
"""Parser for Arm Linker Static Call Graph HTML reports."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from bs4 import BeautifulSoup


def clean_text(node) -> str:
    return re.sub(
        r"\s+",
        " ",
        node.get_text(" ", strip=True)
    ).strip()


def parse_link_list(block: str, label: str) -> list[dict]:

    match = re.search(
        rf"\[{re.escape(label)}\]\s*<UL>(.*?)</UL>",
        block,
        re.I | re.S
    )

    if not match:
        return []

    soup = BeautifulSoup(match.group(1), "html.parser")

    result = []

    for a in soup.find_all("a", href=True):

        result.append({
            "id": a.get("href", "").lstrip("#"),
            "name": clean_text(a)
        })

    return result


def parse_symbol_block(block: str, scope: str) -> dict | None:

    soup = BeautifulSoup(block, "html.parser")

    anchor = soup.find("a", attrs={"name": True})

    strong = anchor.find_parent("strong") if anchor else None

    if not anchor or not strong:
        return None

    symbol_id = anchor.get("name", "").strip()

    name = clean_text(strong).replace(
        clean_text(anchor),
        "",
        1
    ).strip()

    text = clean_text(soup)

    desc_match = re.search(
        r"\(\s*Thumb\s*,\s*(\d+)\s*bytes\s*,\s*"
        r"Stack size\s+(unknown|\d+)\s+bytes\s*,\s*(.*?)\s*\)",
        text,
        re.I
    )

    size = int(desc_match.group(1)) if desc_match else None

    stack_text = (
        desc_match.group(2).lower()
        if desc_match else None
    )

    object_section = (
        desc_match.group(3).strip()
        if desc_match else None
    )

    unused = bool(
        re.search(
            r"\bUNUSED\b",
            desc_match.group(0) if desc_match else ""
        )
    )

    # Stack information

    stack = None

    stack_match = re.search(
        r"Max Depth\s*=\s*(\d+)"
        r"(?:\s*\+\s*(Unknown Stack Size))?",
        text,
        re.I
    )

    if stack_match:

        stack = {
            "max_depth": int(stack_match.group(1)),
            "unknown_stack_size": bool(stack_match.group(2))
        }

    # Call Chain

    chain_match = re.search(
        r"Call Chain\s*=\s*(.*?)"
        r"(?=\[Calls\]|\[Called By\]|\[Address Reference Count|$)",
        text,
        re.I
    )

    if chain_match:

        chain = [
            x.strip()
            for x in re.split(
                r"\s*(?:&rArr;|⇒)\s*",
                chain_match.group(1)
            )
            if x.strip()
        ]

    else:
        chain = []

    # Address References

    address_refs = []

    address_match = re.search(
        r"\[Address Reference Count[^]]*\]\s*<UL>(.*?)</UL>",
        block,
        re.I | re.S
    )

    if address_match:

        ref_soup = BeautifulSoup(
            address_match.group(1),
            "html.parser"
        )

        address_refs = [
            clean_text(li)
            for li in ref_soup.find_all("li")
        ]

    return {

        "id": symbol_id,
        "name": name,
        "scope": scope,

        "size_bytes": size,

        "stack_size_bytes": (
            int(stack_text)
            if stack_text and stack_text.isdigit()
            else None
        ),

        "stack_size_unknown": stack_text == "unknown",

        "object_section": object_section,

        "unused": unused,

        "stack": stack,
        "call_chain": chain,

        "calls": parse_link_list(block, "Calls"),

        "called_by": parse_link_list(
            block,
            "Called By"
        ),

        "address_references": address_refs,
    }


def extract_section(
    html: str,
    heading: str,
    next_heading: str | None
) -> str:

    start = re.search(
        rf"<H3[^>]*>\s*{re.escape(heading)}\s*</H3>",
        html,
        re.I
    )

    if not start:
        return ""

    end = (
        re.search(
            r"<H3[^>]*>\s*" +
            re.escape(next_heading) +
            r"\s*</H3>",
            html[start.end():],
            re.I
        )
        if next_heading else None
    )

    return (
        html[start.end(): start.end() + end.start()]
        if end else html[start.end():]
    )


def parse_symbols(
    html: str,
    heading: str,
    next_heading: str
) -> list[dict]:

    section = extract_section(
        html,
        heading,
        next_heading
    )

    blocks = re.split(
        r"(?=<P>\s*<STRONG>\s*<a\s+name=)",
        section,
        flags=re.I
    )

    symbols = []

    for block in blocks:

        if re.search(
            r"<STRONG>\s*<a\s+name=",
            block,
            re.I
        ):

            item = parse_symbol_block(
                block,
                heading[:-8].lower()
                if heading.endswith(" Symbols")
                else heading.lower()
            )

            if item:
                symbols.append(item)

    return symbols


def parse_report(path: Path) -> dict:

    html = path.read_text(
        encoding="utf-8",
        errors="replace"
    )

    # Maximum Stack Usage

    max_match = re.search(
        r"Maximum Stack Usage\s*=\s*(\d+)\s*bytes\s*"
        r"\+\s*(Unknown\([^<]*\))?",
        html,
        re.I
    )

    chain_match = re.search(
        r"Call chain for Maximum Stack Depth:</H3>\s*"
        r"(.*?)\s*<P>",
        html,
        re.I | re.S
    )

    summary_chain = (
        [
            x.strip()
            for x in re.split(
                r"\s*(?:&rArr;|⇒)\s*",
                clean_text(
                    BeautifulSoup(
                        chain_match.group(1),
                        "html.parser"
                    )
                )
            )
        ]
        if chain_match else []
    )

    # Global and Local Symbols

    global_symbols = parse_symbols(
        html,
        "Global Symbols",
        "Local Symbols"
    )

    local_symbols = parse_symbols(
        html,
        "Local Symbols",
        "Undefined Global Symbols"
    )

    symbols = global_symbols + local_symbols

    by_id = {
        s["id"]: s
        for s in symbols
    }

    # Function Pointers

    fp_section = extract_section(
        html,
        "Function Pointers",
        "Global Symbols"
    )

    fp_soup = BeautifulSoup(
        fp_section,
        "html.parser"
    )

    function_pointers = []

    for li in fp_soup.find_all("li"):

        a = li.find("a", href=True)

        if a:

            row = clean_text(li)

            m = re.search(
                r"from\s+(.*?)\s+referenced from\s+(.*)$",
                row,
                re.I
            )

            function_pointers.append({

                "target_id": a.get(
                    "href",
                    ""
                ).lstrip("#"),

                "target_name": clean_text(a),

                "source_object": (
                    m.group(1) if m else None
                ),

                "referenced_from": (
                    m.group(2) if m else None
                )

            })

    # Functions without stack information

    no_stack_section = extract_section(
        html,
        "Functions with no stack information",
        "Mutually Recursive functions"
    )

    no_stack = []

    for a in BeautifulSoup(
        no_stack_section,
        "html.parser"
    ).find_all("a", href=True):

        no_stack.append({

            "id": a.get("href", "").lstrip("#"),

            "name": clean_text(a)

        })

    # Mutually Recursive Functions

    recursion_section = extract_section(
        html,
        "Mutually Recursive functions",
        "Function Pointers"
    )

    recursion = []

    for li in BeautifulSoup(
        recursion_section,
        "html.parser"
    ).find_all("li"):

        links = li.find_all("a", href=True)

        if links:

            recursion.append([

                {
                    "id": a.get(
                        "href",
                        ""
                    ).lstrip("#"),

                    "name": clean_text(a)
                }

                for a in links

            ])

    # Call Relations

    edges = set()

    for s in symbols:

        for callee in s["calls"]:

            if callee["id"]:

                edges.add((
                    s["id"],
                    callee["id"]
                ))

    # Unresolved References

    unresolved = sorted({

        ref["id"]

        for s in symbols

        for key in ("calls", "called_by")

        for ref in s[key]

        if ref["id"] and ref["id"] not in by_id

    })

    # Calls / Called By consistency check

    reverse_mismatches = []

    for caller_id, callee_id in sorted(edges):

        callee = by_id.get(callee_id)

        if callee and not any(
            x["id"] == caller_id
            for x in callee["called_by"]
        ):

            reverse_mismatches.append({

                "caller_id": caller_id,

                "callee_id": callee_id,

                "caller": by_id[caller_id]["name"],

                "callee": callee["name"]

            })

    return {

        "source_file": str(path),

        "stack_summary": {

            "maximum_stack_bytes": (
                int(max_match.group(1))
                if max_match else None
            ),

            "has_unknown_component": bool(
                max_match and max_match.group(2)
            ),

            "maximum_stack_chain": summary_chain,

            "functions_without_stack_info": no_stack,

            "mutually_recursive_groups": recursion

        },

        "function_pointers": function_pointers,

        "symbols": symbols,

        "call_relations": [

            {
                "caller_id": a,
                "callee_id": b
            }

            for a, b in sorted(edges)

        ],

        "diagnostics": {

            "global_symbol_count": len(global_symbols),

            "local_symbol_count": len(local_symbols),

            "symbol_count": len(symbols),

            "call_edge_count": len(edges),

            "unresolved_reference_ids": unresolved,

            "calls_without_matching_called_by": reverse_mismatches

        }

    }


def main() -> None:

    ap = argparse.ArgumentParser(
        description="Parse Arm Linker Static Call Graph HTML"
    )

    ap.add_argument(
        "input",
        type=Path
    )

    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("callgraph.json")
    )

    args = ap.parse_args()

    report = parse_report(args.input)

    args.output.write_text(
        json.dumps(
            report,
            ensure_ascii=False,
            indent=2
        ),
        encoding="utf-8"
    )

    print(
        f"Symbols: {report['diagnostics']['symbol_count']} "
        f"(global {report['diagnostics']['global_symbol_count']}, "
        f"local {report['diagnostics']['local_symbol_count']})"
    )

    print(
        f"Call edges: "
        f"{report['diagnostics']['call_edge_count']}"
    )

    print(
        f"Unresolved IDs: "
        f"{len(report['diagnostics']['unresolved_reference_ids'])}"
    )

    print(
        f"Reverse-link mismatches: "
        f"{len(report['diagnostics']['calls_without_matching_called_by'])}"
    )

    print(f"JSON: {args.output}")


if __name__ == "__main__":
    main()