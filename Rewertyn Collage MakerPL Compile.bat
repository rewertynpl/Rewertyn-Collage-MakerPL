@echo off
SETLOCAL EnableDelayedExpansion

REM Set code page to UTF-8 for correct handling of special characters in file/directory names
chcp 65001 > NUL

echo compiling

echo install first this
echo https://mirror.msys2.org/mingw/mingw64/mingw-w64-x86_64-libpng-1.6.50-1-any.pkg.tar.zst
echo https://mirror.msys2.org/mingw/mingw64/mingw-w64-x86_64-zlib-1.3.1-1-any.pkg.tar.zst
echo https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.1.2

g++ "Rewertyn Collage MakerPL.cpp" -o "Rewertyn Collage MakerPL.exe" -std=c++17 -D cimg_use_jpeg -D cimg_use_png -I C:/mingw64/include -L C:/mingw64/lib -mwindows -pthread -static -static-libgcc -static-libstdc++ -luser32 -lgdi32 -lcomctl32 -lshlwapi -lole32 -ljpeg -lpng -lz

pause