#!/usr/bin/env python3
"""Generate standalone HTML from Arm Linker Static Call Graph."""

from __future__ import annotations

import argparse
import json
import re
import base64
import zlib

from pathlib import Path
from bs4 import BeautifulSoup


# ============================================================
# HTML parsing helpers
# ============================================================

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

    soup = BeautifulSoup(
        match.group(1),
        "html.parser"
    )

    result = []

    for a in soup.find_all("a", href=True):

        result.append({
            "id": a.get("href", "").lstrip("#"),
            "name": clean_text(a)
        })

    return result


# ============================================================
# Symbol parser
# ============================================================

def parse_symbol_block(block: str, scope: str) -> dict | None:

    soup = BeautifulSoup(
        block,
        "html.parser"
    )

    anchor = soup.find(
        "a",
        attrs={"name": True}
    )

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

    size = (
        int(desc_match.group(1))
        if desc_match else None
    )

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


# ============================================================
# Section extraction
# ============================================================

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


# ============================================================
# Complete Call Graph parser
# ============================================================

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


# ============================================================
# Embedded standalone HTML interface
# ============================================================
_HTML_TEMPLATE = r"""<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Arm Call Graph — f401USB</title>
<style>
:root{color-scheme:dark;--bg:#0b1020;--panel:#121a2d;--panel2:#18233a;--line:#293752;--text:#e7edf7;--muted:#91a0b8;--accent:#68a7ff;--green:#59d6a0;--orange:#ffbf69;--red:#ff7777}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 Inter,Segoe UI,Arial,sans-serif}
header{padding:22px 26px;border-bottom:1px solid var(--line);background:#0e1628}
h1{font-size:22px;margin:0 0 4px}
h2{font-size:16px;margin:0 0 14px}
h3{font-size:13px;color:var(--muted);text-transform:uppercase;letter-spacing:.07em;margin:18px 0 8px}
.sub,.muted{color:var(--muted)}
.wrap{padding:20px;max-width:1600px;margin:auto}
.metrics{display:grid;grid-template-columns:repeat(5,minmax(130px,1fr));gap:12px;margin-bottom:18px}
.metric,.panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:15px}
.metric strong{display:block;font-size:24px;margin-top:5px}
.layout{display:grid;grid-template-columns:340px minmax(0,1fr);gap:16px}
.sidebar{min-height:650px}
.search{width:100%;padding:11px 12px;background:#0b1324;border:1px solid var(--line);border-radius:8px;color:var(--text);outline:none}
.filters{display:flex;gap:6px;margin:10px 0}
.filters button,.smallbtn{background:var(--panel2);border:1px solid var(--line);color:var(--text);border-radius:7px;padding:7px 9px;cursor:pointer}
.filters button.active{border-color:var(--accent);color:var(--accent)}
.sym-list{height:560px;overflow:auto;margin-top:8px}
.sym{padding:8px 9px;border-bottom:1px solid #202d45;cursor:pointer;border-radius:6px}
.sym:hover,.sym.selected{background:#1b2a45}
.sym .name{font-weight:600}
.tag{font-size:11px;color:var(--muted);margin-left:6px}
.right{display:grid;gap:14px;align-content:start}
.topline{display:flex;align-items:flex-start;justify-content:space-between;gap:10px}
.title{font-size:21px;font-weight:700;overflow-wrap:anywhere}
.chips{display:flex;flex-wrap:wrap;gap:7px;margin:10px 0}
.chip{background:var(--panel2);border:1px solid var(--line);padding:5px 8px;border-radius:6px;color:var(--muted)}
.chip b{color:var(--text)}
.columns{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.linklist{display:flex;flex-direction:column;gap:5px}
.linkrow{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:7px 9px;border:1px solid var(--line);border-radius:7px;background:#0e1729}
.linkrow button{border:0;background:none;color:var(--accent);cursor:pointer;text-align:left;font:inherit}
.id{color:var(--muted);font:11px ui-monospace,monospace}
.chain{display:flex;flex-wrap:wrap;align-items:center;gap:7px}
.node{border:1px solid #35517a;background:#14243e;padding:8px 10px;border-radius:7px;cursor:pointer;color:var(--text)}
.arrow{color:var(--accent)}
.tablewrap{overflow:auto;max-height:360px}
table{border-collapse:collapse;width:100%;font-size:13px}
th,td{text-align:left;padding:8px;border-bottom:1px solid var(--line);vertical-align:top}
th{color:var(--muted);font-weight:600;position:sticky;top:0;background:var(--panel)}
.notice{padding:10px 12px;border-radius:8px;background:#17263b;color:#c8d8f0}
.ok{color:var(--green)}
.warn{color:var(--orange)}
.empty{color:var(--muted);padding:18px;text-align:center}
.hidden{display:none}
@media(max-width:900px){.layout{grid-template-columns:1fr}.sym-list{height:300px}.metrics{grid-template-columns:repeat(2,1fr)}}
@media(max-width:600px){.columns{grid-template-columns:1fr}.wrap{padding:12px}}
.graph-wrap{overflow:auto;margin-top:10px;background:#0d1526;border:1px solid var(--line);border-radius:10px;padding:8px}
.callgraph-svg{display:block;width:100%;min-width:760px;height:auto}
.cg-edge{stroke:#536987;stroke-width:1.5;fill:none;opacity:.8}
.cg-node{cursor:pointer}
.cg-node rect{fill:#182640;stroke:#476080;stroke-width:1.2;rx:8}
.cg-node.current rect{fill:#203d63;stroke:var(--accent);stroke-width:2}
.cg-node text{fill:var(--text);font-size:12px;pointer-events:none}
.cg-node .cg-id{fill:var(--muted);font-size:10px}
.cg-label{fill:var(--muted);font-size:11px;font-weight:600}
</style>
</head>
<body>
<header>
<h1>Arm Linker · Static Call Graph</h1>
<div class="sub">f401USB · интерактивный анализ функций и связей</div>
</header>
<main class="wrap">
<section class="metrics" id="metrics"></section>
<section class="panel" style="margin-bottom:16px">
<div class="topline">
<div><h2>Maximum Stack Depth</h2><div id="stacksummary"></div></div>
<div class="chip">Источник: f401USB.htm</div>
</div>
<div id="maxchain" class="chain" style="margin-top:12px"></div>
</section>
<div class="layout">
<aside class="panel sidebar">
<h2>Символы</h2>
<input id="search" class="search" placeholder="Поиск функции / объекта…">
<div class="filters">
<button class="active" data-filter="all">Все</button>
<button data-filter="global">Global</button>
<button data-filter="local">Local</button>
</div>
<div id="symcount" class="muted"></div>
<div id="symbols" class="sym-list"></div>
</aside>
<section class="right">
<div class="panel" id="details">
<div class="empty">Выберите функцию в списке слева.</div>
</div>
<div class="panel" style="margin-top:14px">
<div class="topline">
<div>
<h2>Граф вызовов</h2>
<div class="muted">Слева — вызывающие функции, в центре — выбранная, справа — вызываемые. Нажми на узел для перехода.</div>
</div>
<div style="display:flex;gap:8px;align-items:center">
<label class="muted" for="graphdepth">Уровни</label>
<select id="graphdepth" class="smallbtn">
<option value="1">1</option>
<option value="2" selected>2</option>
<option value="3">3</option>
</select>
<div class="chip" id="graphcount">—</div>
</div>
</div>
<div id="callgraph" class="graph-wrap">
<div class="empty">Выберите функцию.</div>
</div>
</div>
<div class="columns">
<div class="panel">
<h2>Function Pointers</h2>
<div class="muted" id="fpcount"></div>
<input id="fpsearch" class="search" style="margin:10px 0" placeholder="Фильтр по функции / месту ссылки…">
<div class="tablewrap">
<table>
<thead><tr><th>Target</th><th>Referenced from</th></tr></thead>
<tbody id="fptable"></tbody>
</table>
</div>
</div>
<div class="panel">
<h2>Stack / Recursion diagnostics</h2>
<h3>Без stack information</h3>
<div id="nostack" class="linklist"></div>
<h3>Mutually recursive groups</h3>
<div id="recursion" class="linklist"></div>
</div>
</div>
<div class="panel">
<h2>Проверки парсера</h2>
<div id="diagnostics"></div>
</div>
</section>
</div>
</main>

<script>
const DATA=__DATA_JSON__;

const byId=new Map(DATA.symbols.map(s=>[s.id,s]));
let selected=null,filter='all';
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

function go(id){
 const s=byId.get(id);
 if(!s)return;
 selected=id;
 renderList();
 renderDetails(s);
}

function metrics(){
 const d=DATA.diagnostics;
 const totalCalls=DATA.symbols.reduce((n,s)=>n+s.calls.length,0);
 $('metrics').innerHTML=[
  ['Symbols',d.symbol_count],
  ['Global',d.global_symbol_count],
  ['Local',d.local_symbol_count],
  ['Call edges',d.call_edge_count],
  ['Function pointers',DATA.function_pointers.length]
 ].map(x=>`<div class="metric"><span class="muted">${x[0]}</span><strong>${x[1]}</strong></div>`).join('');

 const st=DATA.stack_summary;
 $('stacksummary').innerHTML=
  `<span style="font-size:25px;font-weight:700">${st.maximum_stack_bytes??'—'} bytes</span>
   <span class="${st.has_unknown_component?'warn':'ok'}">${st.has_unknown_component?'+ есть неизвестные компоненты':'полная оценка'}</span>
   <div class="muted">Stack depth из отчёта линкера; неизвестные участки нельзя считать нулевыми.</div>`;

 $('maxchain').innerHTML=st.maximum_stack_chain.map((n,i)=>
  `${i?'<span class="arrow">→</span>':''}<button class="node" onclick="goByName('${esc(n).replace(/'/g,"\\'")}')">${esc(n)}</button>`
 ).join('');
}

function goByName(name){
 const s=DATA.symbols.find(x=>x.name===name);
 if(s)go(s.id);
}

function renderList(){
 const q=$('search').value.toLowerCase();
 const arr=DATA.symbols.filter(s=>
  (filter==='all'||s.scope===filter)&&
  [s.name,s.id,s.object_section].join(' ').toLowerCase().includes(q)
 ).sort((a,b)=>a.name.localeCompare(b.name));

 $('symcount').textContent=`Показано ${arr.length} из ${DATA.symbols.length}`;
 $('symbols').innerHTML=arr.map(s=>
  `<div class="sym ${selected===s.id?'selected':''}" onclick="go('${s.id}')">
   <span class="name">${esc(s.name)}</span>
   <span class="tag">${s.scope}</span>
   <div class="muted">${esc(s.id)} · ${s.size_bytes??'—'} B</div>
  </div>`
 ).join('')||'<div class="empty">Ничего не найдено</div>';
}

function linkRows(items){
 return items.length?items.map(x=>
  `<div class="linkrow">
   <button onclick="go('${x.id}')">${esc(x.name||byId.get(x.id)?.name||'Unknown')}</button>
   <span class="id">${esc(x.id)}</span>
  </div>`
 ).join(''):'<div class="muted">Нет данных в отчёте</div>';
}

function renderDetails(s){
 const callers=s.called_by.map(x=>({...x,name:byId.get(x.id)?.name||x.name}));
 const callees=s.calls.map(x=>({...x,name:byId.get(x.id)?.name||x.name}));
 const st=s.stack||{};

 $('details').innerHTML=`
  <div class="topline">
   <div>
    <div class="title">${esc(s.name)}</div>
    <div class="muted">${esc(s.id)} · ${s.scope} symbol</div>
   </div>
   <button class="smallbtn" onclick="navigator.clipboard?.writeText(${JSON.stringify(s.name)})">Копировать имя</button>
  </div>
  <div class="chips">
   <span class="chip">Code size: <b>${s.size_bytes??'—'} B</b></span>
   <span class="chip">Stack: <b>${s.stack_size_unknown?'unknown':(s.stack_size_bytes??'—')+' B'}</b></span>
   <span class="chip">Max depth: <b>${st.max_depth??'—'}${st.unknown_stack_size?' + unknown':''}</b></span>
   <span class="chip">${s.unused?'UNUSED':'Used / status not marked'}</span>
  </div>
  <div class="muted">Object / section: ${esc(s.object_section||'—')}</div>
  ${s.call_chain?.length?`<h3>Call chain</h3><div class="chain">${s.call_chain.map((n,i)=>`${i?'<span class="arrow">→</span>':''}<span class="node">${esc(n)}</span>`).join('')}</div>`:''}
  <div class="columns" style="margin-top:15px">
   <div><h3>Calls · вызываемые</h3><div class="linklist">${linkRows(callees)}</div></div>
   <div><h3>Called By · вызывающие</h3><div class="linklist">${linkRows(callers)}</div></div>
  </div>
  <h3>Address Reference Count (${s.address_references.length})</h3>
  <div class="muted">${s.address_references.map(esc).join('<br>')||'Нет данных'}</div>`;

 drawGraph(s);
}

function drawGraph(s){
 const depth=Number($('graphdepth')?.value||2);
 const maxPerLevel=14;
 const incoming=new Map(),outgoing=new Map();

 DATA.call_relations.forEach(e=>{
  if(!outgoing.has(e.caller_id))outgoing.set(e.caller_id,[]);
  outgoing.get(e.caller_id).push(e.callee_id);
  if(!incoming.has(e.callee_id))incoming.set(e.callee_id,[]);
  incoming.get(e.callee_id).push(e.caller_id);
 });

 function layers(map,start){
  let result=[[start]],seen=new Set([start]);
  for(let d=1;d<=depth;d++){
   let next=[];
   for(const id of result[d-1]){
    for(const n of (map.get(id)||[])){
     if(!seen.has(n)&&byId.has(n)){
      seen.add(n);
      next.push(n);
     }
    }
   }
   result.push(next.slice(0,maxPerLevel));
  }
  return result;
 }

 const left=layers(incoming,s.id);
 const right=layers(outgoing,s.id);
 const cols=[...left.slice(1).reverse(),[s.id],...right.slice(1)];
 const maxRows=Math.max(1,...cols.map(a=>a.length));
 const W=cols.length*245+30;
 const H=Math.max(190,maxRows*58+70);
 const boxW=215,boxH=42,gap=245;
 const xAt=i=>15+i*gap;
 const yAt=(i,n)=>35+(H-55-n*58)/2+i*58;
 let nodes='',edges='';
 const pos=new Map();

 cols.forEach((arr,ci)=>arr.forEach((id,i)=>{
  pos.set(id,{x:xAt(ci),y:yAt(i,arr.length),ci});
 }));

 for(let ci=0;ci<depth;ci++){
  for(const id of cols[ci]){
   for(const tid of (outgoing.get(id)||[])){
    const a=pos.get(id),b=pos.get(tid);
    if(a&&b&&b.ci===ci+1){
     edges+=`<path class="cg-edge" d="M ${a.x+boxW} ${a.y+boxH/2} C ${a.x+boxW+35} ${a.y+boxH/2}, ${b.x-35} ${b.y+boxH/2}, ${b.x} ${b.y+boxH/2}" marker-end="url(#cgArrow)"/>`;
    }
   }
  }
 }

 for(let ci=depth;ci<cols.length-1;ci++){
  for(const id of cols[ci]){
   for(const tid of (outgoing.get(id)||[])){
    const a=pos.get(id),b=pos.get(tid);
    if(a&&b&&b.ci===ci+1){
     edges+=`<path class="cg-edge" d="M ${a.x+boxW} ${a.y+boxH/2} C ${a.x+boxW+35} ${a.y+boxH/2}, ${b.x-35} ${b.y+boxH/2}, ${b.x} ${b.y+boxH/2}" marker-end="url(#cgArrow)"/>`;
    }
   }
  }
 }

 cols.forEach((arr,ci)=>arr.forEach(id=>{
  const o=byId.get(id),p=pos.get(id);
  if(!o||!p)return;
  const label=o.name.length>25?o.name.slice(0,23)+'…':o.name;
  nodes+=`<g class="cg-node ${id===s.id?'current':''}" transform="translate(${p.x},${p.y})" onclick="go('${id}')">
   <title>${esc(o.name)} · ${esc(id)}</title>
   <rect width="${boxW}" height="${boxH}"/>
   <text x="9" y="17">${esc(label)}</text>
   <text class="cg-id" x="9" y="33">${esc(id)} · ${o.size_bytes??'—'} B</text>
  </g>`;
 }));

 const labels=cols.map((a,i)=>
  `<text class="cg-label" x="${xAt(i)}" y="20">${i<depth?'CALLERS −'+(depth-i):i===depth?'SELECTED':'CALLS +'+(i-depth)}</text>`
 ).join('');

 const total=cols.reduce((n,a)=>n+a.length,0);
 $('graphcount').textContent=`${total} узлов · глубина ${depth}`;
 $('callgraph').innerHTML=`
  <svg class="callgraph-svg" style="min-width:${W}px" viewBox="0 0 ${W} ${H}" role="img" aria-label="Многоуровневый граф вызовов">
   <defs><marker id="cgArrow" markerWidth="8" markerHeight="8" refX="6" refY="3" orient="auto"><path d="M0,0 L0,6 L7,3 z" fill="#7892b5"/></marker></defs>
   ${labels}${edges}${nodes}
  </svg>
  <div class="muted" style="padding:7px">Нажми на любой узел, чтобы сделать его центральным. Для длинных графов доступна горизонтальная прокрутка. На каждом уровне отображается до ${maxPerLevel} новых функций.</div>`;
}

$('graphdepth')?.addEventListener('change',()=>{
 if(selected&&byId.has(selected))drawGraph(byId.get(selected));
});

function fp(){
 const q=$('fpsearch').value.toLowerCase();
 const rows=DATA.function_pointers.filter(x=>
  [x.target_name,x.source_object,x.referenced_from].join(' ').toLowerCase().includes(q)
 );

 $('fpcount').textContent=`${rows.length} / ${DATA.function_pointers.length} references`;
 $('fptable').innerHTML=rows.map(x=>
  `<tr>
   <td>
    <button class="smallbtn" onclick="go('${x.target_id}')">${esc(x.target_name)}</button>
    <div class="id">${esc(x.target_id)}</div>
    <div class="muted">${esc(x.source_object||'')}</div>
   </td>
   <td>${esc(x.referenced_from||'—')}</td>
  </tr>`
 ).join('');
}

function diagnostics(){
 const d=DATA.diagnostics;
 $('diagnostics').innerHTML=`
  <div class="notice">
   <span class="ok">✓ ${d.unresolved_reference_ids.length} unresolved IDs</span> ·
   <span class="ok">✓ ${d.calls_without_matching_called_by.length} Calls / Called By mismatches</span>
   <br>
   <span class="muted">Edges построены по ID; обратные ссылки сверены.</span>
  </div>`;

 $('nostack').innerHTML=DATA.stack_summary.functions_without_stack_info.map(x=>
  `<div class="linkrow">
   <button onclick="go('${x.id}')">${esc(x.name)}</button>
   <span class="id">${esc(x.id)}</span>
  </div>`
 ).join('');

 $('recursion').innerHTML=DATA.stack_summary.mutually_recursive_groups.map((g,i)=>
  `<div class="linkrow"><span>Group ${i+1}: ${g.map(x=>esc(x.name)).join(' ↔ ')}</span></div>`
 ).join('');
}

document.querySelectorAll('[data-filter]').forEach(b=>b.onclick=()=>{
 filter=b.dataset.filter;
 document.querySelectorAll('[data-filter]').forEach(x=>x.classList.toggle('active',x===b));
 renderList();
});

$('search').oninput=renderList;
$('fpsearch').oninput=fp;
metrics();
renderList();
fp();
diagnostics();
</script>
</body>
</html>
"""


def build_html(report: dict, output: Path) -> None:
    payload = json.dumps(
        report,
        ensure_ascii=False,
        separators=(",", ":")
    )

    page = _HTML_TEMPLATE.replace("__DATA_JSON__", payload, 1)

    output.write_text(page, encoding="utf-8")
# ============================================================
# Input resolver: AXF -> matching HTM
# ============================================================

def resolve_callgraph_input(path: Path) -> Path:

    path = path.expanduser()

    if path.suffix.lower() in {".htm", ".html"}:
        report_path = path
    else:
        report_path = path.with_suffix(".htm")

    if not report_path.is_file():

        raise FileNotFoundError(
            "Arm Linker Call Graph report was not found.\n"
            f"Expected: {report_path}\n"
            "Check that Static Call Graph generation is enabled."
        )

    return report_path


# ============================================================
# Main
# ============================================================

def main() -> None:

    ap = argparse.ArgumentParser(
        description="Generate standalone Arm Linker Call Graph HTML"
    )

    ap.add_argument(
        "input",
        type=Path,
        help="AXF file or Arm Linker .htm report"
    )

    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output HTML path"
    )

    args = ap.parse_args()

    report_path = resolve_callgraph_input(
        args.input
    )

    output = args.output or report_path.with_name(
        report_path.stem + "_CallGraph.html"
    )

    report = parse_report(report_path)

    build_html(
        report,
        output
    )

    d = report["diagnostics"]

    print(f"Call Graph source: {report_path}")
    print(f"HTML: {output}")

    print(
        f"Symbols: {d['symbol_count']} "
        f"(global {d['global_symbol_count']}, "
        f"local {d['local_symbol_count']})"
    )

    print(f"Call edges: {d['call_edge_count']}")

    print(
        f"Unresolved IDs: "
        f"{len(d['unresolved_reference_ids'])}"
    )

    print(
        f"Calls / Called By mismatches: "
        f"{len(d['calls_without_matching_called_by'])}"
    )


if __name__ == "__main__":
    main()