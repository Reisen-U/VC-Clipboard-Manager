windres resource.rc -o resource.o
g++ main.cpp resource.o -o ClipboardManager.exe -mwindows -municode -static -lgdi32 -lshell32 -lole32 -lgdiplus -lwinmm -ladvapi32
