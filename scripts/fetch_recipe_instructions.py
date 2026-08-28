#!/usr/bin/env python3
"""
Ajoute le champ « instructions » aux recettes de data/recipe_library.json
en repassant sur les pages Marmiton (JSON-LD recipeInstructions).

Usage:
  python3 scripts/fetch_recipe_instructions.py [--in data/recipe_library.json] [--workers 6]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

# Réutilise le parseur Marmiton du scraper principal.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from scrape_recipe_library import fetch, parse_instructions_from_node, _iter_jsonld_nodes, _is_recipe_type  # noqa: E402

USER_AGENT = "ColoCourse-RecipeBuilder/1.0 (+https://github.com/colocourse)"
REQUEST_DELAY = 0.25


def instructions_from_url(url: str) -> str:
    time.sleep(REQUEST_DELAY)
    try:
        html = fetch(url)
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        print(f"  skip {url}: {e}", file=sys.stderr)
        return ""
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
            return parse_instructions_from_node(node)
    return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--in", dest="input_path", type=Path, default=Path("data/recipe_library.json"))
    parser.add_argument("--workers", type=int, default=6)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    path = args.input_path if args.input_path.is_absolute() else root / args.input_path
    library = json.loads(path.read_text(encoding="utf-8"))
    recipes = library.get("recipes", [])
    todo = [r for r in recipes if not (r.get("instructions") or "").strip()]
    print(f"{len(todo)} recettes sans instructions sur {len(recipes)}", flush=True)

    updated = 0
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {
            pool.submit(instructions_from_url, r["source"]): r
            for r in todo
            if r.get("source")
        }
        done = 0
        for fut in as_completed(futures):
            done += 1
            recipe = futures[fut]
            try:
                text = fut.result()
            except Exception as e:
                print(f"  erreur {recipe.get('id')}: {e}", file=sys.stderr)
                continue
            if text:
                recipe["instructions"] = text
                updated += 1
            if done % 100 == 0:
                print(f"  {done}/{len(futures)} pages, {updated} avec texte", flush=True)

    path.write_text(json.dumps(library, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with_instr = sum(1 for r in recipes if (r.get("instructions") or "").strip())
    print(f"Terminé : {with_instr}/{len(recipes)} recettes avec instructions.", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
