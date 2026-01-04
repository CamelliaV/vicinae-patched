# Vicinae Patched

A fork of [Vicinae](https://github.com/vicinaehq/vicinae) with additional enhancements for clipboard and file management.

## Enhancements

- **Multi-paste**: Paste multiple clipboard items at once
- **Paste as text**: Strip formatting when pasting
- **Image preview**: Preview images in clipboard history and file search
- **Reveal in file explorer**: Open file location in your file manager

## Installation (Arch Linux)

```bash
git clone https://github.com/CamelliaV/vicinae-patched.git
cd vicinae-patched
makepkg -si
```

## Building

Requires: cmake, ninja, nodejs, npm, qt6, and other dependencies listed in PKGBUILD.

```bash
makepkg -si
```

## Original Project

See the [original Vicinae repository](https://github.com/vicinaehq/vicinae) for full documentation and features.
