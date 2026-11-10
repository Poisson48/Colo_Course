#!/usr/bin/env python3
"""Insère (ou remplace) les locations Colo Sync dans le vhost colo-apps.

Ne touche à aucune autre location : /releases/ et / restent telles quelles.
Sauvegarde le fichier avant écriture. À appeler sous root, puis `nginx -t`.
"""

import datetime
import pathlib
import sys

BEGIN = "# BEGIN colosync"
END = "# END colosync"

BLOCK = """\
    # BEGIN colosync — signaling / credentials TURN (aucune donnée de liste)
    location = /signal {
        proxy_pass http://127.0.0.1:7778;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto https;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
        proxy_buffering off;
    }
    location = /turn-credentials {
        proxy_pass http://127.0.0.1:7778;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto https;
        access_log off;
        client_max_body_size 8k;
    }
    location = /config {
        proxy_pass http://127.0.0.1:7778;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto https;
        access_log off;
    }
    location = /healthz {
        allow 127.0.0.1;
        deny all;
        proxy_pass http://127.0.0.1:7778;
        access_log off;
    }
    # END colosync
"""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch-nginx.py /etc/nginx/sites-available/colo-apps", file=sys.stderr)
        return 2
    path = pathlib.Path(sys.argv[1])
    text = path.read_text()
    backup = path.with_suffix(path.suffix + ".bak." + datetime.datetime.now().strftime("%Y%m%d%H%M%S"))
    backup.write_text(text)

    if BEGIN in text and END in text:
        start = text.index(BEGIN)
        end = text.index(END, start) + len(END)
        # Conserver l'indentation de début de ligne.
        line_start = text.rfind("\n", 0, start) + 1
        text = text[:line_start] + BLOCK + text[end:]
    else:
        marker = "    location / {"
        if marker not in text:
            print("location / introuvable : vhost inattendu", file=sys.stderr)
            return 1
        text = text.replace(marker, BLOCK + "\n" + marker, 1)

    path.write_text(text)
    print(f"patched {path} (backup: {backup})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
