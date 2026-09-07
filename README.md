# Ovni — Mega Drive

Port a Sega Mega Drive / Genesis del juego [ovni](../ovni) (js13k), construido con [SGDK](https://github.com/Stephane-D/SGDK).

Bootstrap generado a partir de la plantilla [mega-drive-hello-world](../mega-drive-hello-world). Por ahora solo arranca y muestra texto; la lógica del laberinto se portará en pasos siguientes.

## Requisitos

- [SGDK](https://github.com/Stephane-D/SGDK), con la variable `GDK` apuntando a la carpeta clonada (p. ej. en `~/.zshrc`: `export GDK="$HOME/dev/sgdk"`)
- `m68k-elf-gcc` / `m68k-elf-binutils` (Homebrew)
- Java (para las herramientas `rescomp`/`sizebnd` de SGDK)
- Make
- [BlastEm](https://www.retrodev.com/blastem/) para probar el ROM

## Build

```bash
make        # genera out/rom.bin
make clean  # limpia out/
```

## Ejecutar en emulador

```bash
blastem out/rom.bin
```

## VS Code

Tareas en `.vscode/tasks.json`: **SGDK: build**, **SGDK: clean**, **SGDK: run in BlastEm**.
