# AtlasLight

A tiny GTK 3 web browser for old netbooks, built for the Packard Bell Dot (Atom N2600, GMA 3600, 2 GB RAM).
32-bit friendly, CPU-only drawing, no GPU needed.

**Status: first version, never run on real hardware yet. Expect to fix things.**

## What it has

- **HTML + CSS rendering** with [litehtml](https://github.com/litehtml/litehtml), a small engine (BSD licence).
- **Dark mode** for the window and for web pages (Ctrl+D, remembered between runs).
- **Rounded window** with a modern header bar (needs a compositing window manager such as Sway; on IceWM without a compositor the corners look square).
- **Built-in ad blocker**: domain rules, hosts-file lines, `||domain^` rules, URL patterns, allow-list (`@@domain`). Starter list in `data/blocklist.txt`; add your own in `~/.config/atlaslight/blocklist.txt`.
- **Browser scripts** with a bundled [QuickJS](https://github.com/quickjs-ng/quickjs) engine: small `.js` files in `~/.config/atlaslight/scripts/` can add block rules and CSS. See `data/example-script.js`. Scripts are limited to 16 MB of memory and 2 seconds each, so a bad script cannot freeze the browser.
- TLS certificates are **verified** (the upstream litehtml sample disabled this; we turned it on).

## What it does NOT have (read this)

- **No JavaScript for web pages.** litehtml has no JavaScript or DOM bindings, so scripts inside pages never run. Sites that need JS (Gmail, YouTube, most web apps, many login pages) will look empty or broken. The scripting feature above customises the browser, it does not run page scripts.
- Because pages cannot run JS, the ad blocker mostly stops **tracking pixels, ad images and ad CSS**, not script-based ads (those never load anyway).
- No tabs, bookmarks, downloads, forms/text inputs inside pages, or Chrome/Firefox extensions.
- Newer CSS (complex layouts) may render imperfectly. Text-heavy sites (Wikipedia, news "lite" sites, docs, blogs) work best.

## Keys

Ctrl+L address bar · F5 reload · Alt+Left / Alt+Right back / forward · Ctrl+D dark/light · Ctrl+Q quit

## Build with GitHub (no Linux needed)

1. Upload this whole folder to a GitHub repo (keep the `.github/workflows` folder).
2. Open **Actions → Build AtlasLight (32-bit) → Run workflow**.
3. When it is green, open the run and download **atlaslight-i386** from *Artifacts*. It contains `atlaslight` (the program) and a `.deb` package.

The workflow builds inside a 32-bit Debian 12 container, runs the unit tests, and packages the result.

## Build on Debian 12 (i386 or amd64)

```bash
sudo apt install build-essential cmake pkg-config git libgtk-3-dev libcurl4-openssl-dev libpango1.0-dev libcairo2-dev
git clone https://github.com/litehtml/litehtml third_party/litehtml
git -C third_party/litehtml checkout ebb9d32b28cb8c0f6dff2257c1ace676da0dc4b1
cmake -S . -B build && cmake --build build -j2
./build/atlaslight
```

QuickJS is bundled in `third_party/quickjs`, so nothing extra is needed. Turn scripting off with `-DATLAS_ENABLE_JS=OFF`.

## Install the .deb on the Dot

```bash
sudo apt install ./atlaslight_0.1.0_i386.deb
atlaslight
```

To bake it into your DOT-OS ISO, put the `.deb` in `config/packages.chroot/` of the live-build tree.

## Speed tips for 2 GB machines

- Keep the default 4 download threads (set in `src/main.cpp`, `open_page`).
- Large images (over ~16 megapixels) are skipped on purpose.
- If drawing feels slow, make the window smaller. Everything is drawn by the CPU.

## Code map

| File | Job |
|---|---|
| `src/main.cpp` | the only GTK code: window, theme, input, navigation |
| `src/adblock.*` | ad blocker (no dependencies, unit-tested) |
| `src/js/js_engine.*` | browser scripting on the bundled QuickJS |
| `third_party/quickjs/` | QuickJS-ng source (MIT), only the 4 library files + headers |
| `src/net/*` | page loading, from litehtml `support/webpage` (see `NOTICE.md` for our changes) |

Keeping GTK in one file makes a later move to GTK 4 a small job.

## Licence

AtlasLight code: BSD 3-clause (see `LICENSE`). litehtml and the vendored `src/net` files: BSD 3-clause, Copyright (c) 2013 Yuri Kobets (tordex) (see `LICENSE-litehtml`). QuickJS: MIT, Copyright (c) 2017-2026 Fabrice Bellard and the QuickJS-ng authors (see `third_party/quickjs/LICENSE`).
