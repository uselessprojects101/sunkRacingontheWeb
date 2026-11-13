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
