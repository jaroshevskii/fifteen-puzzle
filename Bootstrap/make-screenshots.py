#!/usr/bin/env python3
"""Generate representative store screenshots for itch.io as SVGs, faithful to
the actual in-game UI (same palette, layout and monospace font that raylib's
default font renders). Rendered to Bootstrap/screenshots/*.png.

    python3 Bootstrap/make-screenshots.py      # writes SVGs
    # then: for f in build-shots/*.svg; do rsvg-convert ... ; done  (see below)

The wrapper at the bottom of Bootstrap (or CI) renders them; here we only emit
the SVG so the script stays dependency-free.
"""

import os

W, H = 960, 720
BLACK = "#000000"
PURPLE = "#701f7e"
ORANGE = "#ffa100"
GREEN = "#00e430"
GRAY = "#828282"
WHITE = "#ffffff"
MONO = "'DejaVu Sans Mono','Menlo','Consolas',monospace"

OUT = os.path.join(os.path.dirname(__file__), "..", "build-shots")


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def text(x, y, s, size, fill, anchor="start", weight="700"):
    return (
        f'<text x="{x}" y="{y}" font-family="{MONO}" font-weight="{weight}" '
        f'font-size="{size}" fill="{fill}" text-anchor="{anchor}">{esc(s)}</text>'
    )


def board(ox, oy, tile, grid, layout, numbers=True):
    """A puzzle board: purple panel + cells. `layout` is grid*grid labels
    ('' = the empty slot). Cells are sharp rects with a 2px black gap and a
    centred number — exactly how the game draws them."""
    span = tile * grid
    out = [f'<rect x="{ox}" y="{oy}" width="{span}" height="{span}" fill="{PURPLE}"/>']
    for i, label in enumerate(layout):
        r, c = divmod(i, grid)
        x, y = ox + c * tile, oy + r * tile
        out.append(f'<rect x="{x}" y="{y}" width="{tile}" height="{tile}" fill="{BLACK}"/>')
        body = PURPLE if label == "" else ORANGE
        out.append(
            f'<rect x="{x+2}" y="{y+2}" width="{tile-4}" height="{tile-4}" fill="{body}"/>'
        )
        if label and numbers:
            fs = int(tile * 0.5)
            out.append(
                text(x + tile / 2, y + tile / 2 + fs * 0.36, label, fs, BLACK, "middle", "800")
            )
    return "".join(out)


def svg(body):
    return (
        f'<svg width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
        f'xmlns="http://www.w3.org/2000/svg">'
        f'<rect width="{W}" height="{H}" fill="{BLACK}"/>{body}</svg>'
    )


SOLVED = [str(i) for i in range(1, 16)] + [""]


def shot_solo():
    layout = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "", "13", "14", "15", "12"]
    b = board(240, 98, 120, 4, layout)
    status = text(W / 2, 688, "00:23   4x4  (0-9 resize · Esc menu)", 22, WHITE, "middle")
    return svg(b + status)


def mini_board(ox, oy, tile, grid, layout, solved=False):
    span = tile * grid
    out = [
        f'<rect x="{ox-4}" y="{oy-4}" width="{span+8}" height="{span+8}" rx="6" fill="#14141a"/>'
    ]
    for i, label in enumerate(layout):
        r, c = divmod(i, grid)
        x, y = ox + c * tile, oy + r * tile
        if label == "":
            out.append(f'<rect x="{x+1}" y="{y+1}" width="{tile-2}" height="{tile-2}" fill="#281e33"/>')
        else:
            fill = GREEN if solved else "#c8702c" if False else "#e08528"
            out.append(f'<rect x="{x+1}" y="{y+1}" width="{tile-2}" height="{tile-2}" fill="{ORANGE}"/>')
    return "".join(out)


def shot_multiplayer():
    layout = ["1", "2", "3", "4", "5", "6", "", "8", "9", "10", "7", "11", "13", "14", "15", "12"]
    parts = [board(240, 120, 120, 4, layout)]
    # HUD
    parts.append(text(W / 2, 38, "00:18", 28, WHITE, "middle"))
    parts.append(text(12, 60, "You: 12 moves", 18, GREEN))
    parts.append(text(W - 12, 60, "Bob: 9 moves", 18, ORANGE, "end"))
    # opponent live preview (top-right)
    opp = ["1", "2", "3", "4", "5", "", "6", "8", "9", "10", "7", "11", "13", "14", "15", "12"]
    parts.append(mini_board(798, 72, 37, 4, opp))
    parts.append(text(798, 246, "Bob", 14, ORANGE))
    return svg("".join(parts))


def shot_live():
    p = []
    p.append(text(W / 2, 52, "Live Games", 40, WHITE, "middle"))
    p.append(text(W / 2, 104, "12 online   ·   4 racing   ·   2 in queue", 20, "#78c878", "middle"))
    p.append(text(40, 150, "In progress", 22, WHITE))
    rows = ["4x4   Ada  vs  Bob", "5x5   Cy  vs  Dot", "4x4   Eve  vs  Finn"]
    y = 186
    for r in rows:
        p.append(text(48, y, r, 18, ORANGE))
        y += 28
    y += 20
    p.append(text(40, y, "Recent finishes", 22, WHITE))
    y += 34
    done = [
        "Ada won a 4x4 race in 00:41",
        "Gil won a 7x7 race in 02:04",
        "Cy won a 5x5 race in 01:12",
    ]
    for d in done:
        p.append(text(48, y, d, 16, "#aaaaaa"))
        y += 24
    p.append(text(W / 2, H - 30, "Esc to leave", 16, GRAY, "middle"))
    return svg("".join(p))


def shot_leaderboard():
    # a faint solved board behind, then the dim overlay + list
    behind = board(240, 98, 120, 4, SOLVED)
    p = [behind, f'<rect width="{W}" height="{H}" fill="#000000" fill-opacity="0.87"/>']
    p.append(text(16, 36, "Leaderboard", 28, WHITE))
    p.append(text(16, 60, "4x4   (L to close)", 16, GRAY))
    rows = [
        " 1. Ada        00:42  80 mv",
        " 2. Bob        00:51  96 mv",
        " 3. Cy         01:03  120 mv",
        " 4. Dot        01:10  132 mv",
        " 5. Player     01:24  150 mv",
    ]
    y = 96
    for r in rows:
        p.append(text(16, y, r, 18, WHITE))
        y += 24
    return svg("".join(p))


def shot_victory():
    behind = board(240, 98, 120, 4, SOLVED)
    p = [behind, f'<rect width="{W}" height="{H}" fill="#000000" fill-opacity="0.72"/>']
    p.append(text(W / 2, 254, "Victory!", 40, WHITE, "middle", "800"))
    p.append(text(W / 2, 300, "00:42   80 moves", 18, GRAY, "middle"))
    # buttons (MenuView): 280x46, first selected (orange), second grey
    bx = (W - 280) / 2
    p.append(f'<rect x="{bx}" y="332" width="280" height="46" fill="{ORANGE}"/>')
    p.append(text(W / 2, 362, "Play Again", 22, BLACK, "middle"))
    p.append(f'<rect x="{bx}" y="390" width="280" height="46" fill="#3c3c46"/>')
    p.append(text(W / 2, 420, "Main Menu", 22, WHITE, "middle"))
    return svg("".join(p))


def main():
    os.makedirs(OUT, exist_ok=True)
    shots = {
        "01-gameplay": shot_solo(),
        "02-multiplayer": shot_multiplayer(),
        "03-live-games": shot_live(),
        "04-leaderboard": shot_leaderboard(),
        "05-victory": shot_victory(),
    }
    for name, content in shots.items():
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w") as f:
            f.write(content)
        print("wrote", path)


if __name__ == "__main__":
    main()
