#!/usr/bin/env python3
"""
Normalise data/recipe_library.json pour le loader C++ (src/core/recipe_library.cpp).

Applique/renforce en amont les règles du loader afin que le fichier livré à
l'app soit déjà "propre" (moins de travail de filtrage au runtime, stats
vérifiables hors-ligne) :

  - schéma : {"recipes": [{id, title, category, servings, ingredients:[...]}]}
  - name  : libellé ingrédient, <= 200 caractères
  - qty   : quantité libre ("2", "700 g", "1/2 sachet"), <= 100 caractères
  - note  : précision optionnelle, <= 200 caractères
  - dédoublonnage des titres (déaccentué, minuscule, trim -- même clé que
    `normKey()` côté C++)
  - dédoublonnage des ingrédients par recette (même clé sur le nom)
  - rejet des recettes avec moins de 2 ingrédients après dédoublonnage
  - amélioration du parsing français des ingrédients bruts
    ("700 g de pommes de terre" -> qty="700 g", name="pommes de terre")

Usage:
  python3 scripts/normalize_recipe_library.py \
      [--in data/recipe_library.json] [--out data/recipe_library.json] \
      [--target 1500]

Par défaut, le fichier est normalisé en place (--in == --out).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import unicodedata
from pathlib import Path
from typing import Any

MAX_NAME_LEN = 200
MAX_QTY_LEN = 100
MAX_NOTE_LEN = 200
MAX_INSTRUCTIONS_LEN = 12000

# Unités triées par longueur (aligné avec scrape_recipe_library.py).
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
UNIT_RE = r"(?:" + "|".join(_UNIT_WORDS) + r")"
NUM_RE = r"\d+(?:[.,]\d+)?(?:\s*(?:-|à|/)\s*\d+(?:[.,]\d+)?)?"

LEADING_QTY_RE = re.compile(
    rf"^({NUM_RE})\s*(?:({UNIT_RE})(?![^\W\d_]))?\.?\s*(?:d['’]\s*|de\s+|des\s+|du\s+)?(.+)$",
    re.IGNORECASE,
)
PARENTHETICAL_RE = re.compile(r"\(([^()]{1,120})\)")


def deaccent(s: str) -> str:
    """Retire les diacritiques -- équivalent Python de la fonction Qt côté C++."""
    n = unicodedata.normalize("NFD", s)
    out = "".join(c for c in n if unicodedata.category(c) != "Mn")
    out = out.replace("œ", "oe").replace("Œ", "Oe")
    return out


def app_norm_key(s: str) -> str:
    """Reproduit exactement `normalizeIngredientKey()` de src/core/ingredient_norm.cpp."""
    return deaccent(s.strip().lower())


# Alias → clé de correspondance (aligné avec ingredient_norm.cpp).
_MATCH_ALIASES: dict[str, str] = {
    "oeuf": "oeufs", "oeufs": "oeufs",
    "oignon": "oignons", "oignons": "oignons",
    "gousse d ail": "ail", "gousses d ail": "ail",
    "gousse d'ail": "ail", "gousses d'ail": "ail", "ail": "ail",
    "tomate": "tomates", "tomates": "tomates",
    "pomme de terre": "pommes de terre", "pommes de terre": "pommes de terre",
    "patate": "pommes de terre", "patates": "pommes de terre",
    "carotte": "carottes", "carottes": "carottes",
    "courgette": "courgettes", "courgettes": "courgettes",
    "poivron": "poivrons", "poivrons": "poivrons",
    "champignon": "champignons", "champignons": "champignons",
    "citron": "citrons", "citrons": "citrons",
    "pate": "pates", "pâte": "pates", "pates": "pates", "pâtes": "pates",
    "epinard": "epinards", "épinard": "epinards", "epinards": "epinards",
    "épinards": "epinards",
    "yaourt": "yaourts", "yaourts": "yaourts",
    "banane": "bananes", "bananes": "bananes",
}

# Clé canonique → nom d'affichage préféré (singulier court → pluriel).
_CANONICAL_NAMES: dict[str, str] = {
    "oeufs": "œufs", "oignons": "oignons", "ail": "ail", "tomates": "tomates",
    "pommes de terre": "pommes de terre", "carottes": "carottes",
    "courgettes": "courgettes", "poivrons": "poivrons", "champignons": "champignons",
    "citrons": "citrons", "pates": "pâtes", "epinards": "épinards",
    "yaourts": "yaourts", "bananes": "bananes",
}


def ingredient_match_key(s: str) -> str:
    key = app_norm_key(s)
    return _MATCH_ALIASES.get(key, key)


def canonical_ingredient_name(s: str) -> str:
    trimmed = s.strip()
    if not trimmed:
        return trimmed
    norm_key = app_norm_key(trimmed)
    match_key = _MATCH_ALIASES.get(norm_key)
    if match_key is None:
        return trimmed
    if norm_key == match_key:
        return trimmed
    return _CANONICAL_NAMES.get(match_key, trimmed)


def truncate(s: str, limit: int) -> tuple[str, bool]:
    s = s.strip()
    if len(s) <= limit:
        return s, False
    return s[:limit].rstrip(), True


def to_str(value: Any) -> str:
    """Coerce les champs schema.org potentiellement listes (recipeYield, recipeCategory)."""
    if value is None:
        return ""
    if isinstance(value, str):
        return value.strip()
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, list):
        for item in value:
            if isinstance(item, str) and item.strip():
                return item.strip()
        return ""
    return str(value).strip()


def extract_parenthetical_note(name: str, note: str) -> tuple[str, str]:
    """Déplace un contenu entre parenthèses du nom vers la note."""
    m = PARENTHETICAL_RE.search(name)
    if not m:
        return name, note
    extra = m.group(1).strip()
    cleaned_name = (name[: m.start()] + name[m.end() :]).strip()
    cleaned_name = re.sub(r"\s{2,}", " ", cleaned_name).strip(" ,;")
    if not cleaned_name:
        return name, note  # ne rien casser si le nom devient vide
    if extra:
        note = f"{note}; {extra}".strip("; ") if note else extra
    return cleaned_name, note


def repair_g_qty_split(name: str, qty: str) -> tuple[str, str]:
    """Répare « 2 g » + « ousses d'ail » issu d'un mauvais parseur."""
    m = re.match(r"^(\d+(?:[.,]\d+)?)\s*g$", qty.strip(), re.IGNORECASE)
    if not m or not name:
        return name, qty
    num = m.group(1)
    if name.startswith("ousses "):
        return name[6:].strip(), f"{num} gousses"
    if name.startswith("ousse "):
        return name[5:].strip(), f"{num} gousse"
    if name.startswith("ros "):
        return name[4:].strip(), f"{num} gros"
    if name.startswith("ros"):
        return name[3:].strip(), f"{num} gros"
    if name.startswith("itre "):
        return name[5:].strip(), f"{num} litre"
    if name == "s" and qty.endswith("g"):
        return "oeufs", f"{num} oeufs"
    return name, qty


def reparse_ingredient(name: str, qty: str, note: str) -> tuple[str, str, str]:
    """Re-découpe qty/name depuis le libellé brut (répare les scrapes défectueux)."""
    name = re.sub(r"\s{2,}", " ", name.strip())
    qty = qty.strip()
    note = note.strip()

    name, qty = repair_g_qty_split(name, qty)

    raw = f"{qty} {name}".strip() if qty else name
    m = LEADING_QTY_RE.match(raw)
    if m:
        num, unit, rest = m.group(1), m.group(2) or "", m.group(3).strip()
        rest = re.sub(r"\s{2,}", " ", rest).strip(" ,;")
        if rest:
            qty = f"{num} {unit}".strip()
            name = rest

    name, note = extract_parenthetical_note(name, note)
    return name.strip(" ,;"), qty.strip(), note.strip()


def format_servings_label(text: str, count: int) -> str:
    if count > 0:
        return f"{count} personnes"
    m = re.match(r"^(\d+)", (text or "").strip())
    if m:
        return f"{m.group(1)} personnes"
    return text.strip()


def normalize_ingredient(raw: dict, stats: dict) -> dict | None:
    name = to_str(raw.get("name"))
    qty = to_str(raw.get("qty"))
    note = to_str(raw.get("note"))
    if not name:
        return None

    name, qty, note = reparse_ingredient(name, qty, note)
    if not name:
        return None

    name = canonical_ingredient_name(name)

    name, trunc_n = truncate(name, MAX_NAME_LEN)
    qty, trunc_q = truncate(qty, MAX_QTY_LEN)
    note, trunc_no = truncate(note, MAX_NOTE_LEN)
    if trunc_n or trunc_q or trunc_no:
        stats["truncated_fields"] += 1

    return {"name": name, "qty": qty, "note": note}


def normalize_recipe(raw: dict, stats: dict) -> dict | None:
    if not isinstance(raw, dict):
        return None

    rid = to_str(raw.get("id"))
    title = to_str(raw.get("title"))
    if not rid or not title:
        stats["dropped_missing_id_or_title"] += 1
        return None

    category = to_str(raw.get("category"))
    servings = to_str(raw.get("servings"))
    servings_count = raw.get("servings_count")
    if isinstance(servings_count, int) and servings_count > 0:
        base_servings = servings_count
    else:
        m = re.match(r"^(\d+)", servings.strip())
        base_servings = int(m.group(1)) if m else 4
    servings = format_servings_label(servings, base_servings)

    raw_ingredients = raw.get("ingredients")
    if not isinstance(raw_ingredients, list):
        stats["dropped_bad_ingredients"] += 1
        return None

    ingredients: list[dict] = []
    seen_ing_keys: set[str] = set()
    for raw_ing in raw_ingredients:
        if not isinstance(raw_ing, dict):
            continue
        ing = normalize_ingredient(raw_ing, stats)
        if not ing:
            continue
        key = ingredient_match_key(ing["name"])
        if key in seen_ing_keys:
            stats["duplicate_ingredients_removed"] += 1
            continue
        seen_ing_keys.add(key)
        ingredients.append(ing)

    if len(ingredients) < 2:
        stats["dropped_too_few_ingredients"] += 1
        return None

    title, _ = truncate(title, MAX_NAME_LEN)

    recipe = {
        "id": rid,
        "title": title,
        "category": category,
        "servings": servings,
        "servings_count": base_servings,
        "ingredients": ingredients,
    }
    instr_raw = to_str(raw.get("instructions"))
    if instr_raw:
        instr_text, truncated = truncate(instr_raw, MAX_INSTRUCTIONS_LEN)
        if truncated:
            stats["truncated_fields"] += 1
        if instr_text:
            recipe["instructions"] = instr_text
    rating_raw = raw.get("rating")
    if rating_raw is not None:
        try:
            recipe["rating"] = round(float(rating_raw), 1)
        except (TypeError, ValueError):
            pass
    rc_raw = raw.get("review_count")
    if rc_raw is not None:
        try:
            recipe["review_count"] = int(rc_raw)
        except (TypeError, ValueError):
            pass
    # Champs informatifs conservés s'ils existent (ignorés par le loader C++).
    for extra_key in ("source", "prep_time"):
        if raw.get(extra_key):
            recipe[extra_key] = to_str(raw.get(extra_key))
    return recipe


def normalize_library(data: dict) -> tuple[dict, dict]:
    stats = {
        "input_count": 0,
        "output_count": 0,
        "dropped_missing_id_or_title": 0,
        "dropped_bad_ingredients": 0,
        "dropped_too_few_ingredients": 0,
        "duplicate_titles_removed": 0,
        "duplicate_ids_removed": 0,
        "duplicate_ingredients_removed": 0,
        "truncated_fields": 0,
    }

    raw_recipes = data.get("recipes")
    if not isinstance(raw_recipes, list):
        raise ValueError("Schéma invalide : champ 'recipes' manquant ou non-liste")
    stats["input_count"] = len(raw_recipes)

    seen_ids: set[str] = set()
    seen_titles: set[str] = set()
    out_recipes: list[dict] = []

    for raw in raw_recipes:
        recipe = normalize_recipe(raw, stats)
        if recipe is None:
            continue

        id_key = recipe["id"]
        title_key = app_norm_key(recipe["title"])

        if id_key in seen_ids:
            stats["duplicate_ids_removed"] += 1
            continue
        if title_key in seen_titles:
            stats["duplicate_titles_removed"] += 1
            continue

        seen_ids.add(id_key)
        seen_titles.add(title_key)
        out_recipes.append(recipe)

    stats["output_count"] = len(out_recipes)

    out_data = {
        "version": data.get("version", 1),
        "count": len(out_recipes),
        "sources": data.get("sources", []),
        "recipes": out_recipes,
    }
    return out_data, stats


def main() -> int:
    parser = argparse.ArgumentParser(description="Normalise la bibliothèque de recettes")
    parser.add_argument("--in", dest="in_path", type=Path, default=Path("data/recipe_library.json"))
    parser.add_argument("--out", dest="out_path", type=Path, default=None)
    parser.add_argument("--target", type=int, default=1500, help="Seuil minimal attendu (avertissement uniquement)")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    in_path = args.in_path if args.in_path.is_absolute() else root / args.in_path
    out_path = args.out_path or args.in_path
    out_path = out_path if out_path.is_absolute() else root / out_path

    if not in_path.exists():
        print(f"Erreur : {in_path} n'existe pas.", file=sys.stderr)
        return 2

    with in_path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    out_data, stats = normalize_library(data)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(out_data, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(f"Entrée  : {stats['input_count']} recettes ({in_path})")
    print(f"Sortie  : {stats['output_count']} recettes ({out_path})")
    print(f"  doublons de titre supprimés     : {stats['duplicate_titles_removed']}")
    print(f"  doublons d'id supprimés         : {stats['duplicate_ids_removed']}")
    print(f"  rejetées (< 2 ingrédients)      : {stats['dropped_too_few_ingredients']}")
    print(f"  rejetées (id/title manquant)    : {stats['dropped_missing_id_or_title']}")
    print(f"  rejetées (ingrédients invalides): {stats['dropped_bad_ingredients']}")
    print(f"  ingrédients dupliqués retirés   : {stats['duplicate_ingredients_removed']}")
    print(f"  champs tronqués (name/qty/note) : {stats['truncated_fields']}")

    if stats["output_count"] < args.target:
        print(
            f"Avertissement : {stats['output_count']} < cible {args.target}.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
