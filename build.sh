set -e
cd "$(dirname "$0")"
SRC=engine/source/sdl/src
rm -rf dist
mkdir -p dist/classic dist/wide
FAST=$(find "$SRC" -name '*.c' ! -path "$SRC/net/bearssl/*" ! -name 'net_transport.c' ! -name 'r_gl_backend.c')
emcc -O2 -w -std=gnu11 -D_GNU_SOURCE -DSONICR_SDL -DSONICR_GLES2 -I"$SRC" $FAST -o dist/index.html \
  -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sASYNCIFY \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=5242880 \
  -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0 -lidbfs.js \
  -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS --shell-file web/shell.html
CLASSIC=$(find "$SRC" -name '*.c' ! -path "$SRC/net/bearssl/*" ! -path "$SRC/gles2/*" ! -name 'net_transport.c')
emcc -O2 -w -std=gnu11 -D_GNU_SOURCE -DSONICR_SDL -I"$SRC" $CLASSIC -o dist/classic/index.html \
  -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -lGLEW -sLEGACY_GL_EMULATION=1 -sASYNCIFY \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=5242880 \
  -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0 -lidbfs.js \
  -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS --shell-file web/shell.html
emcc -O2 -w -std=gnu11 -D_GNU_SOURCE -DSONICR_SDL -DSONICR_GLES2 -I"$SRC" $FAST -o dist/wide/index.html \
  -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sASYNCIFY \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=5242880 \
  -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0 -lidbfs.js \
  -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS --shell-file web/shell.html
mkdir -p dist/fonts dist/classic/fonts dist/wide/fonts
cp web/fonts/ConsoleSquare-Regular.otf dist/fonts/
cp web/fonts/ConsoleSquare-Regular.otf dist/classic/fonts/
cp web/fonts/ConsoleSquare-Regular.otf dist/wide/fonts/
cp web/peerjs.min.js web/net.js web/store.js dist/
cp web/peerjs.min.js web/net.js web/store.js dist/classic/
cp web/peerjs.min.js web/net.js web/store.js dist/wide/
mkdir -p docs
cp -r dist/. docs/
touch docs/.nojekyll
