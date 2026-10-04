import hashlib
import io
import json
import os
import struct
import tempfile
import threading
import time
import urllib.error
import urllib.request
from flask import Flask, request, send_from_directory, Response, abort

try:
    from PIL import Image          # only needed for theme previews
except ImportError:
    Image = None

app = Flask(__name__)

ROMS_DIR = os.environ.get("ROMS_DIR", os.path.join(os.path.dirname(__file__), "roms"))

GITHUB_RELEASE_API = "https://api.github.com/repos/BwahFox/ds-shop/releases/latest"
UPDATE_INTERVAL_SECONDS = 7 * 24 * 60 * 60
MAX_UPDATE_SIZE = 32 * 1024 * 1024
ENABLE_GITHUB_UPDATES = os.environ.get("ENABLE_GITHUB_UPDATES", "false").lower() in (
    "1", "true", "yes", "on"
)
LATEST_RELEASE_VERSION = None
UPDATE_STATUS_LOCK = threading.Lock()


def update_rom_from_github():
    """Download the latest published shop ROM, replacing the current copy atomically."""
    global LATEST_RELEASE_VERSION
    temp_path = None
    try:
        request = urllib.request.Request(
            GITHUB_RELEASE_API,
            headers={"Accept": "application/vnd.github+json", "User-Agent": "ds-shop-server"},
        )
        with urllib.request.urlopen(request, timeout=20) as response:
            release = json.load(response)

        asset = next((item for item in release.get("assets", [])
                      if item.get("name") == "ds-shop.nds"), None)
        if asset is None:
            app.logger.warning("Latest GitHub release has no ds-shop.nds asset")
            return False

        expected_size = int(asset.get("size", 0))
        expected_digest = asset.get("digest") or ""
        if expected_size < 0x160 or expected_size > MAX_UPDATE_SIZE:
            app.logger.warning("Refusing GitHub ROM with unexpected size: %d", expected_size)
            return False
        if expected_digest and not expected_digest.startswith("sha256:"):
            app.logger.warning("Refusing GitHub ROM with unsupported digest: %s", expected_digest)
            return False

        os.makedirs(ROMS_DIR, exist_ok=True)
        fd, temp_path = tempfile.mkstemp(prefix=".ds-shop-", suffix=".tmp", dir=ROMS_DIR)
        digest = hashlib.sha256()
        downloaded = 0
        request = urllib.request.Request(
            asset["browser_download_url"],
            headers={"Accept": "application/octet-stream", "User-Agent": "ds-shop-server"},
        )
        with os.fdopen(fd, "wb") as output, urllib.request.urlopen(request, timeout=30) as response:
            while True:
                chunk = response.read(64 * 1024)
                if not chunk:
                    break
                downloaded += len(chunk)
                if downloaded > expected_size or downloaded > MAX_UPDATE_SIZE:
                    raise ValueError("GitHub ROM exceeded its declared size")
                digest.update(chunk)
                output.write(chunk)

        if downloaded != expected_size:
            raise ValueError(f"GitHub ROM size mismatch: expected {expected_size}, got {downloaded}")
        if expected_digest and digest.hexdigest() != expected_digest.removeprefix("sha256:").lower():
            raise ValueError("GitHub ROM SHA-256 digest mismatch")

        new_hash = digest.hexdigest()
        destination = os.path.join(ROMS_DIR, "ds-shop.nds")
        if os.path.isfile(destination) and os.path.getsize(destination) == downloaded:
            with open(destination, "rb") as current:
                if hashlib.file_digest(current, "sha256").digest() == digest.digest():
                    os.unlink(temp_path)
                    temp_path = None
                    with UPDATE_STATUS_LOCK:
                        LATEST_RELEASE_VERSION = release.get("tag_name", "").lstrip("vV") or None
                    app.logger.info("ds-shop.nds is already up to date (%s)", release.get("tag_name"))
                    return True

        os.replace(temp_path, destination)
        temp_path = None
        with UPDATE_STATUS_LOCK:
            LATEST_RELEASE_VERSION = release.get("tag_name", "").lstrip("vV") or None
        app.logger.info("Updated ds-shop.nds to GitHub release %s", release.get("tag_name", "unknown"))
        return True
    except (OSError, ValueError, KeyError, urllib.error.URLError) as exc:
        app.logger.warning("GitHub ROM update failed: %s", exc)
        return False
    finally:
        if temp_path and os.path.exists(temp_path):
            os.unlink(temp_path)


def _github_update_loop():
    while True:
        try:
            update_rom_from_github()
        except Exception:
            app.logger.exception("Unexpected error in GitHub ROM updater")
        time.sleep(UPDATE_INTERVAL_SECONDS)


def start_github_update_checker():
    if not ENABLE_GITHUB_UPDATES:
        app.logger.info("GitHub ROM updates are disabled")
        return
    threading.Thread(target=_github_update_loop, name="github-rom-updater", daemon=True).start()

# NDS banner layout (relative to the banner offset stored at header 0x68):
#   0x020  icon bitmap   512 bytes (32x32, 4bpp, 16 tiles of 8x8)
#   0x220  icon palette   32 bytes (16 colors, BGR1555)
#   0x240  title[0] JP   256 bytes (UTF-16LE)
#   0x340  title[1] EN   256 bytes (UTF-16LE)  <- we use English
ICON_BYTES = 512 + 32          # tiles + palette served per game
_BANNER_TITLE_EN = 0x340


# Category -> (subdirectory under ROMS_DIR, matching file extensions).
CATEGORIES = {
    "nds": ("",       (".nds",)),
    "nes": ("vc/nes", (".nes",)),
    "gb":  ("vc/gb",  (".gb",)),
    "gbc": ("vc/gbc", (".gbc",)),
    "gba": ("vc/gba", (".gba",)),
    "dsiware": ("dsiware", (".nds", ".dsi")),
}

# TWiLight Menu++ themes: each theme is a FOLDER (theme.ini, background/, ui/, ...)
# under themes/<menu>/, mirroring /_nds/TWiLightMenu/<menu>/themes/ on the SD card.
THEME_CATEGORIES = {
    "theme-dsi": "themes/dsimenu",
    "theme-3ds": "themes/3dsmenu",
    "theme-r4":  "themes/r4menu",
    "theme-ak":  "themes/akmenu",
}

# Theme previews: the theme's own top-screen background, shrunk for the DS.
PREVIEW_W, PREVIEW_H = 128, 96
_HIDDEN = (".DS_Store", "Thumbs.db", "desktop.ini")

# Curated "Popular Titles" — matched as case-insensitive substrings of NDS
# filenames; only those present in the collection appear (capped at 20).
POPULAR = [
    "mario kart", "new super mario bros", "super mario 64", "mario party",
    "mario & luigi", "mario vs", "pokemon", "pokémon", "zelda",
    "animal crossing", "nintendogs", "professor layton", "advance wars",
    "kirby", "metroid prime hunters", "castlevania", "dragon quest",
    "final fantasy", "the world ends with you", "phoenix wright",
    "ace attorney", "rhythm", "warioware", "scribblenauts", "sonic",
    "grand theft auto", "chrono trigger", "elite beat",
]


def _list_dir(subdir, exts):
    d = os.path.join(ROMS_DIR, subdir)
    try:
        return sorted(f for f in os.listdir(d)
                      if f.lower().endswith(exts)
                      and not (not subdir and f.lower() == "ds-shop.nds"))
    except OSError:
        return []


def _theme_files(theme_dir):
    """(relative path, size) of every file in a theme folder, sorted, no OS cruft."""
    out = []
    for root, dirs, files in os.walk(theme_dir):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for f in sorted(files):
            if f.startswith(".") or f in _HIDDEN:
                continue
            full = os.path.join(root, f)
            out.append((os.path.relpath(full, theme_dir).replace(os.sep, "/"),
                        os.path.getsize(full)))
    return out


def _list_themes(subdir):
    """Theme folder names with real content (TWiLight's 'Default' placeholder
    only holds an empty marker file, so it's skipped)."""
    d = os.path.join(ROMS_DIR, subdir)
    try:
        names = sorted(n for n in os.listdir(d)
                       if not n.startswith(".") and os.path.isdir(os.path.join(d, n)))
    except OSError:
        return []
    return [n for n in names if any(size for _, size in _theme_files(os.path.join(d, n)))]


def is_theme_cat(cat):
    return cat in THEME_CATEGORIES


def list_cat(cat):
    """Sorted filenames for a category — the canonical catalog order."""
    if is_theme_cat(cat):
        return _list_themes(THEME_CATEGORIES[cat])
    if cat == "popular":
        files = _list_dir("", (".nds",))
        low = [(f, f.lower()) for f in files]
        out = []
        for key in POPULAR:
            for f, fl in low:
                if key in fl and f not in out:
                    out.append(f)
        return out[:20]
    sub, exts = CATEGORIES.get(cat, CATEGORIES["nds"])
    return _list_dir(sub, exts)


def cat_dir(cat):
    """Disk/URL subdirectory for a category ('' for nds, 'vc/nes', ...)."""
    if cat == "popular":
        return ""
    if is_theme_cat(cat):
        return THEME_CATEGORIES[cat]
    return CATEGORIES.get(cat, CATEGORIES["nds"])[0]


def _req_cat():
    cat = request.args.get("cat", "nds")
    ok = cat in CATEGORIES or cat == "popular" or is_theme_cat(cat)
    return cat if ok else "nds"


def _banner_offset(f):
    f.seek(0x68)
    raw = f.read(4)
    if len(raw) < 4:
        return 0
    return struct.unpack("<I", raw)[0]


def read_banner_title(path):
    """English banner title with newlines collapsed, or None if unavailable."""
    try:
        with open(path, "rb") as f:
            off = _banner_offset(f)
            if off == 0:
                return None
            f.seek(off + _BANNER_TITLE_EN)
            raw = f.read(256)
    except OSError:
        return None
    if len(raw) < 2:
        return None
    title = raw.decode("utf-16-le", errors="ignore").split("\x00", 1)[0]
    title = " ".join(p.strip() for p in title.splitlines() if p.strip())
    return title or None


def read_banner_icon(path):
    """544 bytes (512 tile + 32 palette), or a blank block if unavailable."""
    blank = bytes(ICON_BYTES)
    try:
        with open(path, "rb") as f:
            off = _banner_offset(f)
            if off == 0:
                return blank
            f.seek(off + 0x20)
            data = f.read(ICON_BYTES)
    except OSError:
        return blank
    return data if len(data) == ICON_BYTES else blank


def _apply_query(entries):
    """Filter to filenames containing the ?q= substring (case-insensitive)."""
    q = (request.args.get("q") or "").strip().lower()
    if q:
        return [f for f in entries if q in f.lower()]
    return entries


def _page_slice(entries):
    """Apply ?page=N&size=S to a list; return the whole list if not paged."""
    page = request.args.get("page", type=int)
    size = request.args.get("size", type=int)
    if page is not None and size is not None and size > 0:
        return entries[page * size:(page + 1) * size]
    return entries


def _cat_entries():
    """Category list with the optional search filter applied."""
    return _apply_query(list_cat(_req_cat()))


@app.route("/count")
def count():
    """Total games in a category — lets the client know how many pages exist."""
    return Response(str(len(_cat_entries())), mimetype="text/plain")


@app.route("/catalog.txt")
def catalog_txt():
    cat = _req_cat()
    sub = cat_dir(cat)
    lines = []
    for filename in _page_slice(_cat_entries()):
        path = os.path.join(ROMS_DIR, sub, filename)
        if is_theme_cat(cat):
            # a theme is a folder: its size is the total of its files
            size = sum(s for _, s in _theme_files(path))
            name = filename.replace("|", "-")
        else:
            size = os.path.getsize(path)
            name = os.path.splitext(filename)[0].replace("|", "-")
        lines.append(f"{name}|{filename}|{size}|")
    return Response("\n".join(lines), mimetype="text/plain")


@app.route("/icons.bin")
def icons_bin():
    """Icons concatenated (544 bytes each), in catalog order. Paged via ?page&size."""
    cat = _req_cat()
    sub = cat_dir(cat)
    out = bytearray()
    for filename in _page_slice(_cat_entries()):
        # Only NDS files carry a banner icon; VC ROMs get a blank block.
        if filename.lower().endswith((".nds", ".dsi")):
            out += read_banner_icon(os.path.join(ROMS_DIR, sub, filename))
        else:
            out += bytes(ICON_BYTES)
    return Response(bytes(out), mimetype="application/octet-stream")


def _theme_dir():
    """Folder of the theme named by ?cat=&name=, or abort 404."""
    cat = request.args.get("cat", "")
    name = request.args.get("name", "")
    if not is_theme_cat(cat) or not name or "/" in name or name.startswith("."):
        abort(404)
    d = os.path.join(ROMS_DIR, THEME_CATEGORIES[cat], name)
    if not os.path.isdir(d):
        abort(404)
    return d


@app.route("/theme_files")
def theme_files():
    """Every file of one theme: 'relative/path|size' per line. The DS downloads
    them one by one from /roms/<themes dir>/<name>/<path>."""
    lines = [f"{rel}|{size}" for rel, size in _theme_files(_theme_dir())]
    return Response("\n".join(lines), mimetype="text/plain")


_preview_cache = {}


@app.route("/preview.bin")
def preview_bin():
    """A 128x96 preview of a theme (its top-screen background), as raw
    little-endian BGR555 pixels with bit 15 set, ready to draw on the DS."""
    d = _theme_dir()
    if Image is None:
        abort(404)
    src = os.path.join(d, "background", "top.png")
    if not os.path.isfile(src):
        pngs = sorted(f for f in os.listdir(os.path.join(d, "background"))
                      if f.lower().endswith(".png")) if os.path.isdir(os.path.join(d, "background")) else []
        if not pngs:
            abort(404)
        src = os.path.join(d, "background", pngs[0])
    key = (src, os.path.getmtime(src))
    if key not in _preview_cache:
        im = Image.open(src).convert("RGB").resize((PREVIEW_W, PREVIEW_H), Image.LANCZOS)
        out = bytearray()
        data = im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()
        for r, g, b in data:
            out += struct.pack("<H", 0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3))
        _preview_cache[key] = bytes(out)
    return Response(_preview_cache[key], mimetype="application/octet-stream")


@app.route("/roms/<path:filename>")
def serve_rom(filename):
    # Reject path traversal, but allow ".." inside a name (e.g. "Bros..nds").
    # send_from_directory also safely blocks escapes via safe_join.
    if filename.startswith("/") or ".." in filename.split("/") or filename.startswith(".ds-shop-"):
        abort(400)
    return send_from_directory(ROMS_DIR, filename)


@app.route("/update_status")
def update_status():
    client_version = (request.args.get("version") or "").strip().lstrip("vV")
    with UPDATE_STATUS_LOCK:
        latest_version = LATEST_RELEASE_VERSION
    available = bool(client_version and latest_version and client_version != latest_version)
    return Response("1" if available else "0", mimetype="text/plain")


@app.route("/health")
def health():
    return "OK"


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 8888))
    host = os.environ.get("HOST", "0.0.0.0")   # e.g. just a hotspot's address
    print(f"Serving ROMs from: {ROMS_DIR}")
    print(f"Listening on {host}:{port}")
    start_github_update_checker()
    app.run(host=host, port=port)
