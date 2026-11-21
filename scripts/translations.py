#!/usr/bin/env python3
"""Sincroniza e verifica as traduções PT do VTHOST Studio.

  python3 scripts/translations.py          # acrescenta textos novos ao .ts (por traduzir)
  python3 scripts/translations.py --check  # falha se houver textos sem tradução (usado na CI)

Extrai os tr("...") de src/studio/*.cpp. O contexto é a classe (nome do
ficheiro: main_window.cpp -> MainWindow). Traduções existentes mantêm-se;
textos que já não existem no código são removidos.
"""
import html
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STUDIO = ROOT / "src" / "studio"
TS = STUDIO / "translations" / "vthost_studio_pt_PT.ts"
# tr("...") incluindo literais concatenados em várias linhas: tr("a " "b")
TR_RE = re.compile(r'\btr\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')
PIECE_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def context_for(path: Path) -> str:
    return "".join(p.capitalize() for p in path.stem.split("_"))


def unescape_cpp(s: str) -> str:
    return s.replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\')


def extract():
    found = {}
    for f in sorted(STUDIO.glob("*.cpp")):
        if f.name == "main.cpp":
            continue
        strings = sorted({unescape_cpp("".join(PIECE_RE.findall(m))) for m in TR_RE.findall(f.read_text(encoding="utf-8"))})
        if strings:
            found[context_for(f)] = strings
    return found


def load_existing():
    existing = {}
    if not TS.exists():
        return existing
    root = ET.parse(TS).getroot()
    for ctx in root.findall("context"):
        name = ctx.findtext("name")
        for msg in ctx.findall("message"):
            tr = msg.find("translation")
            text = tr.text if tr is not None and tr.get("type") != "unfinished" else None
            existing[(name, msg.findtext("source"))] = text
    return existing


def esc(s: str) -> str:
    return html.escape(s, quote=False)


def write(found, existing):
    out = ['<?xml version="1.0" encoding="utf-8"?>', '<!DOCTYPE TS>',
           '<TS version="2.1" language="pt_PT" sourcelanguage="en">']
    missing = []
    for ctx, strings in found.items():
        out += ['<context>', f'    <name>{ctx}</name>']
        for s in strings:
            t = existing.get((ctx, s))
            if t is None:
                # Mesmo texto já traduzido noutro contexto: reaproveita.
                t = next((v for (c, src), v in existing.items() if src == s and v), None)
            out += ['    <message>', f'        <source>{esc(s)}</source>']
            if t:
                out.append(f'        <translation>{esc(t)}</translation>')
            else:
                out.append('        <translation type="unfinished"></translation>')
                missing.append(f"{ctx}: {s}")
            out.append('    </message>')
        out.append('</context>')
    out += ['</TS>', '']
    return "\n".join(out), missing


def main():
    found = extract()
    content, missing = write(found, load_existing())
    if "--check" in sys.argv:
        current = TS.read_text(encoding="utf-8") if TS.exists() else ""
        if missing or content != current:
            print("Traduções PT desatualizadas. Corra scripts/translations.py e traduza:")
            for m in missing:
                print("  -", m)
            return 1
        print("Traduções PT completas.")
        return 0
    TS.write_text(content, encoding="utf-8")
    for m in missing:
        print("POR TRADUZIR:", m)
    return 0


if __name__ == "__main__":
    sys.exit(main())
