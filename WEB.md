# Sonic R on the web (GitHub Pages)

Adds a WebAssembly build of the SDL2 port. The page asks each player for **their own**
Sonic R PC game folder, stores it in the browser (IndexedDB), and runs the engine.
No game assets are ever committed to the repo or served from github.io.

## Files in this kit (drop into the root of your repo, next to `source/`, `DATA/`, `docs/`)

| Path | Purpose |
|------|---------|
| `source/sdl/Makefile.emscripten` | emcc build of the existing SDL2 sources |
| `web/index.html` | page: engine loader, folder picker, Start button |
| `.github/workflows/pages.yml` | builds with Emscripten and deploys to Pages |

## Setup

1. Create your repo, commit the contents of `sonic-r-1.1.zip` (the source, `DATA/`, `docs/`, ...) plus this kit.
   **Do not commit** `sonicr.exe` or anything from `Sonic_R.7z`.
2. Repo **Settings -> Pages -> Source: GitHub Actions**.
3. Push to `main`. The workflow publishes `https://<you>.github.io/<repo>/`.

Local build (needs emsdk, `source emsdk_env.sh` first):

    cd source/sdl && emmake make -f Makefile.emscripten
    mkdir -p /tmp/site && cp build-wasm/sonicr.{js,wasm,data} /tmp/site/ && cp ../../web/index.html /tmp/site/
    python3 -m http.server -d /tmp/site 8000

## Notes / expected rough edges

- **Not compiled yet.** I had no Emscripten toolchain available, so expect to fix a few
  compile/link errors on the first CI run (typical: Win32 shims such as `timeGetTime`,
  SDL_mixer format flags, `getopt_long`). Paste the log and I'll patch.
- **Pages cannot send COOP/COEP headers**, so SharedArrayBuffer/pthreads are unavailable.
  The build therefore has no threads; the network receive thread simply won't start.
- **No netplay in the browser** (browsers have no raw UDP). The matchmaker/UPnP/BearSSL code is excluded.
- Rendering uses Emscripten's legacy-GL emulation (the code already has a `__EMSCRIPTEN__`
  texenv fallback). The overbright `GL_ADD_SIGNED` mode falls back to plain modulate, so some
  surfaces may look slightly different than native.
- Frame cap is 30 fps (existing `WaitForFrameCap` Emscripten path, via ASYNCIFY).
- Music: optional `MUSIC/trackN.*` files are copied if present in the picked folder.
  The folder picker keeps only the game folders the engine reads.
- Saves, options and keybindings persist in IndexedDB. "Clear stored data" wipes them.
- Keep the page file size in mind: GitHub Pages limits are 100 MB per file / ~1 GB per site,
  which is another reason game data must come from the player, not the repo.
