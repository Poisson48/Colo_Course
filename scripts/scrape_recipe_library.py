#!/usr/bin/env python3
"""
Construit data/recipe_library.json depuis Marmiton (JSON-LD schema.org).

Usage:
  python3 scripts/scrape_recipe_library.py [--target 1500] [--out data/recipe_library.json]

Respecte robots.txt : URLs via sitemap officiel, pas de pagination de recherche.

Format JSON produit (version 1) :
  {
    "version": 1,
    "count": N,
    "sources": ["marmiton.org"],
    "recipes": [
      {
        "id": "m12345",           # identifiant Marmiton (extrait de l'URL)
        "title": "...",
        "category": "...",
        "ingredients": [{"name": "...", "qty": "...", "note": ""}],
        "source": "https://...",
        "prep_time": "PT30M",     # durée ISO 8601 (prepTime schema.org)
        "servings": "4 personnes"
      }
    ]
  }

Déduplication : par id Marmiton et par titre normalisé (minuscules, sans accents).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import unicodedata
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

USER_AGENT = "ColoCourse-RecipeBuilder/1.0 (+https://github.com/colocourse)"
SITEMAP_INDEX = "https://www.marmiton.org/wsitemap_recipes_index.xml"
REQUEST_DELAY = 0.35  # seconde entre requêtes (poli)

# Unités triées par longueur : « g » ne doit pas matcher dans « gousses ».
_UNIT_WORDS = [
    "cuillères à soupe", "cuillères à café", "cuillere a soupe", "cuillere a cafe",
    "c.à.s", "c.à.c", "càs", "càc",
    "cuillères", "cuilleres", "sachets", "pincées", "pincees", "gousses", "gousse",
    "bottes", "botte", "tranches", "tranche", "paquets", "paquet", "boîtes", "boites",
    "branches", "branche", "feuilles", "feuille", "morceaux", "morceau", "verres",
    "tasses", "poignées", "poignees", "barquettes", "barquette", "litres", "litre",
    "kg", "mg", "ml", "cl", "dl", "cs", "cc", "zeste", "zestes", "filet", "filets",
    "pot", "pots", "cube", "cubes", "brin", "brins", "noix", "bouquet", "bouquets",
    "g", "l",
]
_UNIT_WORDS.sort(key=len, reverse=True)
UNIT_RE = "(?:" + "|".join(re.escape(w) for w in _UNIT_WORDS) + ")"
NUM_RE = r"\d+(?:[.,]\d+)?(?:\s*/\s*\d+)?"
LEADING_QTY_RE = re.compile(
    rf"^({NUM_RE})\s*(?:({UNIT_RE})(?![^\W\d_]))?\.?\s*(?:d['’]\s*|de\s+|des\s+|du\s+)?(.+)$",
    re.IGNORECASE,
)


def fetch(url: str, timeout: int = 45) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8", "replace")


def normalize_title(title: str) -> str:
    t = title.lower().strip()
    t = re.sub(r"\s*:\s*la meilleure recette\s*$", "", t, flags=re.I)
    t = unicodedata.normalize("NFD", t)
    t = "".join(c for c in t if unicodedata.category(c) != "Mn")
    t = re.sub(r"[^\w\s]", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t


def parse_ingredient(raw: str) -> dict[str, str] | None:
    s = raw.strip()
    if not s or len(s) < 2:
        return None

    m = LEADING_QTY_RE.match(s)
    if m:
        num, unit, name = m.group(1), m.group(2) or "", m.group(3).strip()
        if name:
            qty = f"{num} {unit}".strip()
            return {"name": name, "qty": qty, "note": ""}

    # "poivre", "sel"
    return {"name": s, "qty": "", "note": ""}


def parse_servings_count(text: str) -> int:
    m = re.match(r"^(\d+)", (text or "").strip())
    if not m:
        return 0
    n = int(m.group(1))
    return n if n > 0 else 0


def recipe_id_from_url(url: str) -> str:
    m = re.search(r"_(\d+)\.aspx", url)
    return f"m{m.group(1)}" if m else url


def _is_recipe_type(rtype: object) -> bool:
    if rtype == "Recipe":
        return True
    if isinstance(rtype, list):
        return "Recipe" in rtype
    return False


def _as_str(value: object) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value.strip()
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, list):
        parts = [_as_str(v) for v in value if v is not None]
        return ", ".join(p for p in parts if p)
    return str(value).strip()


def _iter_jsonld_nodes(data: object) -> list[dict]:
    """Extrait tous les nœuds dict d'un bloc JSON-LD (graph, listes imbriquées)."""
    nodes: list[dict] = []
    if isinstance(data, dict):
        graph = data.get("@graph")
        if isinstance(graph, list):
            for item in graph:
                nodes.extend(_iter_jsonld_nodes(item))
        else:
            nodes.append(data)
    elif isinstance(data, list):
        for item in data:
            nodes.extend(_iter_jsonld_nodes(item))
    return nodes


def parse_recipe_page(html: str, url: str) -> dict | None:
    for block in re.finditer(
        r"<script type=\"application/ld\+json\">(.*?)</script>", html, re.DOTALL
    ):
        try:
            data = json.loads(block.group(1))
        except json.JSONDecodeError:
            continue
        for node in _iter_jsonld_nodes(data):
            if not _is_recipe_type(node.get("@type")):
                continue
            title = _as_str(node.get("name"))
            if not title:
                continue
            title = re.sub(r"\s*:\s*la meilleure recette\s*$", "", title, flags=re.I)

            ingredients: list[dict[str, str]] = []
            seen: set[str] = set()
            for raw in node.get("recipeIngredient", []) or []:
                if not isinstance(raw, str):
                    continue
                ing = parse_ingredient(raw)
                if not ing:
                    continue
                key = normalize_title(ing["name"])
                if key in seen:
                    continue
                seen.add(key)
                ingredients.append(ing)

            if len(ingredients) < 2:
                return None

            servings_text = _as_str(node.get("recipeYield"))
            servings_count = parse_servings_count(servings_text)
            if servings_count <= 0:
                servings_count = 4

            return {
                "id": recipe_id_from_url(url),
                "title": title,
                "category": _as_str(node.get("recipeCategory")),
                "ingredients": ingredients,
                "source": url,
                "prep_time": _as_str(node.get("prepTime")),
                "servings": servings_text,
                "servings_count": servings_count,
            }
    return None


def collect_sitemap_urls(limit: int) -> list[str]:
    index_xml = fetch(SITEMAP_INDEX)
    root = ET.fromstring(index_xml)
    ns = {"sm": "http://www.sitemaps.org/schemas/sitemap/0.9"}
    sitemap_locs = [
        el.text.strip()
        for el in root.findall("sm:sitemap/sm:loc", ns)
        if el.text
    ]

    urls: list[str] = []
    for sm_url in sitemap_locs:
        if len(urls) >= limit:
            break
        sm_xml = fetch(sm_url)
        sm_root = ET.fromstring(sm_xml)
        for loc in sm_root.findall("sm:url/sm:loc", ns):
            if loc.text and "/recettes/recette_" in loc.text:
                urls.append(loc.text.strip())
                if len(urls) >= limit:
                    break
    return urls


def scrape_one(url: str) -> dict | None:
    time.sleep(REQUEST_DELAY)
    try:
        html = fetch(url)
        return parse_recipe_page(html, url)
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        print(f"  skip {url}: {e}", file=sys.stderr)
        return None


def build_library(target: int, workers: int) -> dict:
    # Marge pour échecs et doublons (~40 % de perte observée sur échantillon).
    url_pool = collect_sitemap_urls(target * 3)
    print(f"URLs du sitemap : {len(url_pool)}", flush=True)

    recipes: list[dict] = []
    seen_titles: set[str] = set()
    seen_ids: set[str] = set()

    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(scrape_one, url): url for url in url_pool}
        done = 0
        for fut in as_completed(futures):
            done += 1
            try:
                recipe = fut.result()
            except Exception as e:
                url = futures[fut]
                print(f"  erreur {url}: {e}", file=sys.stderr, flush=True)
                continue
            if not recipe:
                continue
            if recipe["id"] in seen_ids:
                continue
            norm = normalize_title(recipe["title"])
            if norm in seen_titles:
                continue
            seen_ids.add(recipe["id"])
            seen_titles.add(norm)
            recipes.append(recipe)
            if len(recipes) % 50 == 0:
                print(
                    f"  {len(recipes)} recettes collectées ({done}/{len(url_pool)} pages)",
                    flush=True,
                )
            if len(recipes) >= target:
                for pending in futures:
                    if not pending.done():
                        pending.cancel()
                break

    return {
        "version": 1,
        "count": len(recipes),
        "sources": ["marmiton.org"],
        "recipes": recipes,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Scraper bibliothèque de recettes Marmiton")
    parser.add_argument("--target", type=int, default=1500, help="Nombre de recettes visées")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("data/recipe_library.json"),
        help="Fichier JSON de sortie",
    )
    parser.add_argument("--workers", type=int, default=4, help="Requêtes parallèles")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    out_path = args.out if args.out.is_absolute() else root / args.out
    out_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"Cible : {args.target} recettes → {out_path}", flush=True)
    library = build_library(args.target, args.workers)
    out_path.write_text(
        json.dumps(library, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Terminé : {library['count']} recettes écrites.", flush=True)
    return 0 if library["count"] >= args.target else 1


if __name__ == "__main__":
    sys.exit(main())
