# Release

`ConUI-win-x64.zip` is a ready-to-run Windows build of the playground: the
showcase of every ConUI widget, the theme editor and the themes.

## Running it

1. Unpack the archive anywhere. It contains one folder, `ConUI`.
2. Start `ConUI\ConUI.exe`.

Nothing is installed. Settings (`data.bin`) and themes (`themes\`) are kept
in the same folder, so the folder can be moved or copied with its settings.
To remove the application, delete the folder.

Requirements: Windows 11 x64 with OpenGL 3.3 graphics drivers. Windows 10
is expected to work but has not been tested.

## Contents

```
ConUI\
├── ConUI.exe              the launcher: start this one
├── bin\
│   ├── ConUIApp.exe       the application
│   ├── glfw3.dll          windowing and input (GLFW)
│   └── libwinpthread-1.dll
├── resources.dat          the font and the icons
├── themes\                phosphor, paper, midnight, onyx
├── LICENSE.txt            ConUI: MIT
└── licenses\              JetBrains Mono (OFL 1.1), GLFW (zlib), winpthreads (MIT, BSD)
```

## Building the archive

From the repository root, in the MSYS2 UCRT64 shell:

```bash
./build.sh package
```

It builds the playground, runs the self-test, stages `dist/win/` and packs it
into `release/ConUI-win-x64.zip`. `CONUI_APP=skeleton ./build.sh package`
packs the skeleton into `release/ConUI-win-skeleton-x64.zip`.
