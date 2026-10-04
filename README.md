# DS Shop

A DSi Shop–style game downloader for the Nintendo DS. A homebrew `.nds` app
connects over Wi-Fi to a small server on your PC and lets you browse and
download your own games, DSiWare, Virtual Console ROMs and TWiLight Menu++
themes straight to the SD card.

No games are included. The server only shares the files you give it.

## Features

- A touch-first graphical shop, laid out like the Wii Shop Channel. It also
  works with the buttons. Hold **SELECT** at boot for the plain text
  interface.
- Three sections:
  - **Nintendo DS & DSiWare:** popular titles, all DS titles, DSiWare, a
    random title, and search
  - **Virtual Console:** NES, Game Boy, Game Boy Color, Game Boy Advance
  - **TWiLight Menu Themes:** DSi, 3DS, R4 and Wood menu themes, with previews
- Game icons read from each ROM's banner.
- A download queue, so you can mark several games and fetch them in one go.
- Per-system download folders (`/roms/nds`, `/roms/nes`, `/roms/gba`, ...),
  where TWiLight Menu++ expects them.
- A backup server address, so one SD card works on two networks.

It runs on a DS or DS Lite with a flashcart, and on a DSi through TWiLight
Menu++ or other homebrew launchers.

## Setup

### 1. Run the server on your PC

The server needs Python 3 with Flask (and Pillow for theme previews):

```bash
pip install flask pillow
ROMS_DIR=/path/to/your/roms python server/server.py
```

It listens on port 8888 by default (set `PORT` to change it, and `HOST` to
listen on one address only). It reads the folders live, so there's no need to
restart it when you add games.

Lay out `ROMS_DIR` like this (every folder is optional):

```
ROMS_DIR/
  Some DS Game.nds            DS games go at the top level
  dsiware/  *.nds *.dsi
  vc/
    nes/  *.nes
    gb/   *.gb
    gbc/  *.gbc
    gba/  *.gba
  themes/                     one folder per theme, as on the SD card
    dsimenu/  <Theme Name>/theme.ini, background/, ...
    3dsmenu/  <Theme Name>/...
    r4menu/   <Theme Name>/...
    akmenu/   <Theme Name>/...
```

There's also a Docker setup: `ROMS_DIR=/path/to/your/roms docker compose up -d`
in `server/`.
GitHub ROM updates are disabled by default and the ROM volume is mounted
read-only. To opt in, set `ENABLE_GITHUB_UPDATES=true` and
`ROM_VOLUME_READ_ONLY=false` before starting Compose; releases are fetched from
`BwahFox/ds-shop`.

### 2. Set up the SD card

Copy the `sdcard/ds-shop/` folder to the root of your SD card, so the DS finds
`/ds-shop/config.ini`. Set `server=` to your PC's IP address on the network
the DS connects to:

```ini
server=192.168.1.10   # your PC's IP address
port=8888
server2=              # optional backup server
port2=8888
music=1               # 0 turns the music off
music_volume=70       # 0 to 100
ui=graphical          # or "text"
```

You can also change these later from **Settings** on the shop's home page.

The DS connects with the Wi-Fi settings saved in its own Nintendo WFC setup.
DS-mode Wi-Fi only supports open and WEP networks.

### 3. Get the app

Download the latest `DS-Shop-vX.Y.Z.zip` from the releases page and copy its
`SD card` folder's contents to the root of your SD card. The zip also has a
copy of the server. Or build the app yourself (see below). `ds-shop.nds` can
live anywhere on the card.

**Controls:** tap, or use the D-pad and A/B. **Y** searches the current list.
**SELECT** mutes the music. To quit, choose **Exit** on the home page, or press
START. On a DSi, you can also tap the power button.

## Optional extras

The release zip already has both files in its `ds-shop/` folder. They're made
from Nintendo's DSi Shop, so they aren't in the source code. To make them
yourself, put them in `/ds-shop/` on the SD card:

- **Music** (`music.bin`): converted from a recording of the DSi Shop theme.
  Needs ffmpeg and numpy.

  ```bash
  tools/make_music.py "DSi Shop theme.mp3" music.bin
  ```

- **Download animation** (`anim.bin`): the DSi Shop's Mario download animation
  and wait icon, taken from your DSi's copy of the DSi Shop. Unpack
  `layout/cmn/shop_progressbar.szs` and `layout/cmn/wait_icon.szs` (Yaz0 NARCs)
  into folders with those names, then run:

  ```bash
  tools/make_anim.py <folder containing them> anim.bin
  ```

Without these files, the shop plays no music and draws its own progress box
and spinner.

## Building

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the
`nds-dev` package group (devkitARM, libnds, calico, dswifi, libfat). Then run:

```bash
./build.sh
```

The output is `client/ds-shop.nds`. If melonDS is installed, `build.sh` also
copies the build to melonDS's SD card folder. Set `DEPLOY_DIR=` (empty) to skip
that step.

The UI's fonts and graphics are pre-built into `client/source/assets.c`.
`tools/gen_assets.py` regenerates them. It needs Pillow and the Noto Sans fonts.

## License

GPL-3.0. See [LICENSE](LICENSE). The bundled Noto Sans glyphs are under the SIL
Open Font License.

Not affiliated with Nintendo. Nintendo DS, DSi and the DSi Shop are trademarks
of Nintendo.