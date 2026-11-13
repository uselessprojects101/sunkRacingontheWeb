set -e
cd "$(dirname "$0")"
SRC=engine/source/sdl/src
mkdir -p dist
FILES=$(find "$SRC" -name '*.c' ! -path "$SRC/net/bearssl/*" ! -path "$SRC/gles2/*")
emcc -O2 -w -std=gnu11 -D_GNU_SOURCE -DSONICR_SDL -I"$SRC" $FILES -o dist/index.html \
  -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -lGLEW -sLEGACY_GL_EMULATION=1 -sASYNCIFY \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=5242880 \
  -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0 -lidbfs.js \
  -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS --shell-file web/shell.html
mkdir -p dist/fonts
cp web/fonts/ConsoleSquare-Regular.otf dist/fonts/
