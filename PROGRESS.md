# progress

## done
- repo created, .gitignore blocks game folders and audio/data file types
- engine source (PC/SDL2 version) added under engine/

## findings
- engine uses desktop OpenGL (GLEW), pthreads and sockets for online play
- game data is supplied by the player, never stored in this repo

## next
- read main loop and renderer, then first emscripten build

## update
- first emscripten build compiles and links with no warnings (build.sh)
- fixed two mismatched function declarations that would crash in wasm
- not yet run in a browser: needs a page with a file picker for the game files

## update 2
- page shell with folder picker, play button, credits and console square font (web/shell.html)
- local test: game starts in the browser and reaches the select mode screen
- not yet tested: racing, sound, controls, saves, speed

## update 3
- music works: page loads adx tracks from any folder named music (v0.2)
- next: lan/online play (needs a browser-friendly network layer)

## update 4
- lan works in two tabs on one pc: join, lobby, race start (v0.3)
- web network layer: net_transport_web.c + web/net.js (peerjs, room from #name in the link)
- network.c patched: no receive thread, usleep becomes emscripten_sleep
- not yet tested: two real devices over the internet
- next: host/guest pages from the mockups, player dots, names

## update 5
- multiplayer screen with invite link and hosting indicator (v0.5)
- profile box on the main page when the address has a #room, name passed to the game with --username
- names show over racers in lan races
- next: saved game files and saves in the browser, clear saved files button, guest page, player dots

## update 6
- game files, saves and settings now persist in the browser (store.js, indexeddb)
- clear saved files button under play
- autosave to slot 1 and autoload at start (engine patch in init.c, screen_misc.c, screens.c) (v0.7)
- next: guest page from the mockup, player dots, bug sweep

## update 7
- v0.8: hidden fps and frame time readout (add ?fps to the address) to measure performance on low end devices
- next: pick performance fixes from the numbers, then guest page and player dots

## update 8
- v0.9: readout also shows drawing ms and polygons per frame (add ?fps)
- next: if drawing time dominates, batch polygons into fewer draw calls
