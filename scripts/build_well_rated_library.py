#!/usr/bin/env python3
"""
Étend data/recipe_library.json avec toutes les recettes Marmiton « bien notées »
(>= min-rating / 5, au moins min-reviews avis), sans doublons (id + titre).

Usage:
  python3 scripts/build_well_rated_library.py [--min-rating 4.0] [--workers 8]

Fusionne avec le catalogue existant : les recettes déjà présentes sont conservées.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from scrape_recipe_library import (
    collect_all_sitemap_urls,
    normalize_title,
    parse_rating_from_html,
    scrape_one,
    fetch,
)

USER_AGENT = "ColoCourse-RecipeBuilder/1.0 (+https://github.com/colocourse)"


def load_existing(path: Path) -> tuple[list[dict], set[str], set[str]]:
    if not path.is_file():
        return [], set(), set()
    data = json.loads(path.read_text(encoding="utf-8"))
    recipes = data.get("recipes", [])
    seen_ids: set[str] = set()
    seen_titles: set[str] = set()
    for r in recipes:
        if r.get("id"):
            seen_ids.add(r["id"])
        if r.get("title"):
            seen_titles.add(normalize_title(r["title"]))
    return recipes, seen_ids, seen_titles


def rating_probe(url: str, min_rating: float, min_reviews: int) -> tuple[str, bool]:
    time.sleep(0.06)
    try:
        html = fetch(url)
    except Exception:
        return url, False
    rating, reviews = parse_rating_from_html(html)
    ok = rating >= min_rating and reviews >= min_reviews
    return url, ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--min-rating", type=float, default=4.0)
    parser.add_argument("--min-reviews", type=int, default=1)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument(
        "--in", dest="input_path", type=Path, default=Path("data/recipe_library.json")
    )
    parser.add_argument(
        "--out", dest="output_path", type=Path, default=Path("data/recipe_library.json")
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    in_path = args.input_path if args.input_path.is_absolute() else root / args.input_path
    out_path = args.output_path if args.output_path.is_absolute() else root / args.output_path

    recipes, seen_ids, seen_titles = load_existing(in_path)
    print(f"Catalogue existant : {len(recipes)} recettes", flush=True)

    print("Collecte des URLs Marmiton (sitemap complet)…", flush=True)
    all_urls = collect_all_sitemap_urls()
    print(f"  {len(all_urls)} URLs", flush=True)

    print(
        f"Scan des notes (>= {args.min_rating}/5, {args.min_reviews}+ avis)…",
        flush=True,
    )
    well_rated_urls: list[str] = []
    done = 0
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {
            pool.submit(rating_probe, url, args.min_rating, args.min_reviews): url
            for url in all_urls
        }
        for fut in as_completed(futures):
            done += 1
            if done % 2000 == 0:
                print(f"  {done}/{len(all_urls)} pages scannées…", flush=True)
            url, ok = fut.result()
            if ok:
                well_rated_urls.append(url)

    print(f"  {len(well_rated_urls)} recettes bien notées sur Marmiton", flush=True)

    # URLs à scraper en complet (pas déjà dans le catalogue par URL Marmiton).
    existing_sources = {r.get("source") for r in recipes if r.get("source")}
    to_scrape = [u for u in well_rated_urls if u not in existing_sources]
    print(f"  {len(to_scrape)} nouvelles à intégrer (hors catalogue actuel)", flush=True)

    added = 0
    skipped_dup = 0
    failed = 0
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(scrape_one, url): url for url in to_scrape}
        done = 0
        for fut in as_completed(futures):
            done += 1
            if done % 100 == 0:
                print(
                    f"  scrape {done}/{len(to_scrape)} — {added} ajoutées",
                    flush=True,
                )
            try:
                recipe = fut.result()
            except Exception as e:
                failed += 1
                print(f"  erreur: {e}", file=sys.stderr, flush=True)
                continue
            if not recipe:
                failed += 1
                continue
            if recipe["id"] in seen_ids:
                skipped_dup += 1
                continue
            norm = normalize_title(recipe["title"])
            if norm in seen_titles:
                skipped_dup += 1
                continue
            seen_ids.add(recipe["id"])
            seen_titles.add(norm)
            recipes.append(recipe)
            added += 1

    library = {
        "version": 1,
        "count": len(recipes),
        "sources": ["marmiton.org"],
        "recipes": recipes,
    }
    out_path.write_text(
        json.dumps(library, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"Terminé : {library['count']} recettes (+{added} bien notées, "
        f"{skipped_dup} doublons ignorés, {failed} échecs) → {out_path}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
