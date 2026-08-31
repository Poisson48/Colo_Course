#!/usr/bin/env python3
"""
Construit data/recipe_catalog.db depuis data/recipe_library.json.

Reproduit les règles de filtrage de src/core/recipe_library.cpp (recettes valides,
search_blob normalisé). Le fichier JSON doit déjà être normalisé
(scripts/normalize_recipe_library.py).

Usage:
  python3 scripts/json_to_catalog_db.py [input.json] [output.db]
"""

from __future__ import annotations

import json
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from normalize_recipe_library import (  # noqa: E402
    app_norm_key,
    canonical_ingredient_name,
    ingredient_match_key,
)
from scrape_recipe_library import parse_servings_count  # noqa: E402


def build_search_blob(rec: dict) -> str:
    parts = [
        app_norm_key(rec.get("title", "")),
        app_norm_key(rec.get("category", "")),
    ]
    for ing in rec.get("ingredients", []):
        parts.append(app_norm_key(ing.get("name", "")))
    instr = rec.get("instructions", "")
    if instr:
        parts.append(app_norm_key(instr))
    return " ".join(p for p in parts if p)


def parse_recipes(data: dict) -> list[dict]:
    seen_titles: set[str] = set()
    out: list[dict] = []
    for node in data.get("recipes", []):
        if not isinstance(node, dict):
            continue
        rid = str(node.get("id", "")).strip()
        title = str(node.get("title", "")).strip()
        if not rid or not title:
            continue
        title_key = app_norm_key(title)
        if title_key in seen_titles:
            continue
        seen_titles.add(title_key)

        ingredients: list[dict] = []
        seen_ing: set[str] = set()
        for ing_node in node.get("ingredients", []):
            if not isinstance(ing_node, dict):
                continue
            name = canonical_ingredient_name(str(ing_node.get("name", "")).strip())
            if not name:
                continue
            ing_key = ingredient_match_key(name)
            if ing_key in seen_ing:
                continue
            seen_ing.add(ing_key)
            ingredients.append(
                {
                    "name": name,
                    "qty": str(ing_node.get("qty", "")).strip(),
                    "note": str(ing_node.get("note", "")).strip(),
                }
            )
        if len(ingredients) < 2:
            continue

        servings = str(node.get("servings", "")).strip()
        servings_count = node.get("servings_count")
        if not isinstance(servings_count, int) or servings_count <= 0:
            servings_count = parse_servings_count(servings)
        if servings_count <= 0:
            servings_count = 4

        rec = {
            "id": rid,
            "title": title,
            "category": str(node.get("category", "")).strip(),
            "servings": servings,
            "servings_count": servings_count,
            "instructions": str(node.get("instructions", "")).strip(),
            "ingredients": ingredients,
        }
        rec["search_blob"] = build_search_blob(rec)
        out.append(rec)
    return out


def write_db(recipes: list[dict], out_path: Path) -> None:
    tmp = out_path.with_suffix(out_path.suffix + ".tmp")
    if tmp.exists():
        tmp.unlink()
    conn = sqlite3.connect(tmp)
    try:
        conn.executescript(
            """
            PRAGMA journal_mode=WAL;
            PRAGMA foreign_keys=ON;
            CREATE TABLE catalog_meta (
              key TEXT PRIMARY KEY,
              value TEXT NOT NULL
            );
            CREATE TABLE recipes (
              sort_key INTEGER PRIMARY KEY,
              id TEXT NOT NULL UNIQUE,
              title TEXT NOT NULL,
              category TEXT NOT NULL DEFAULT '',
              servings TEXT NOT NULL DEFAULT '',
              servings_count INTEGER NOT NULL DEFAULT 4,
              instructions TEXT NOT NULL DEFAULT '',
              search_blob TEXT NOT NULL DEFAULT ''
            );
            CREATE TABLE ingredients (
              recipe_id TEXT NOT NULL,
              position INTEGER NOT NULL,
              name TEXT NOT NULL,
              qty TEXT NOT NULL DEFAULT '',
              note TEXT NOT NULL DEFAULT '',
              PRIMARY KEY (recipe_id, position),
              FOREIGN KEY (recipe_id) REFERENCES recipes(id) ON DELETE CASCADE
            );
            CREATE INDEX idx_recipes_category ON recipes(category);
            CREATE VIRTUAL TABLE recipes_fts USING fts5(
              sort_key UNINDEXED, search_blob
            );
            """
        )
        for sort_key, rec in enumerate(recipes):
            conn.execute(
                "INSERT INTO recipes "
                "(sort_key, id, title, category, servings, servings_count, instructions, search_blob) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    sort_key,
                    rec["id"],
                    rec["title"],
                    rec["category"],
                    rec["servings"],
                    rec["servings_count"],
                    rec["instructions"],
                    rec["search_blob"],
                ),
            )
            for pos, ing in enumerate(rec["ingredients"]):
                conn.execute(
                    "INSERT INTO ingredients (recipe_id, position, name, qty, note) "
                    "VALUES (?, ?, ?, ?, ?)",
                    (rec["id"], pos, ing["name"], ing["qty"], ing["note"]),
                )
        conn.execute(
            "INSERT INTO catalog_meta (key, value) VALUES (?, ?)",
            ("recipe_count", str(len(recipes))),
        )
        conn.execute(
            "INSERT INTO catalog_meta (key, value) VALUES (?, ?)",
            ("schema_version", "2"),
        )
        conn.execute(
            "INSERT INTO recipes_fts(sort_key, search_blob) "
            "SELECT sort_key, search_blob FROM recipes"
        )
        conn.commit()
    finally:
        conn.close()
    tmp.replace(out_path)


def main() -> int:
    in_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data/recipe_library.json"
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "data/recipe_catalog.db"

    if not in_path.is_file():
        print(f"Fichier introuvable : {in_path}", file=sys.stderr)
        return 1

    with open(in_path, encoding="utf-8") as f:
        data = json.load(f)

    recipes = parse_recipes(data)
    if not recipes:
        print("Aucune recette valide", file=sys.stderr)
        return 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_db(recipes, out_path)
    print(f"OK {len(recipes)} recettes → {out_path} ({out_path.stat().st_size // (1024 * 1024)} Mo)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
