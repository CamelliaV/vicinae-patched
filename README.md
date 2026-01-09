# Vicinae Patched

A fork of [Vicinae](https://github.com/vicinaehq/vicinae) with additional enhancements for clipboard management.

## Enhancements

### Clipboard Multi-Select
- **Range selection**: Shift+Click to select start item, Shift+Click again to select all items in range
- **Single item toggle**: Ctrl+Click to select/deselect individual items (for non-adjacent selection)
- **Multi-paste**: Enter to paste all selected items, Ctrl+Enter for reverse order
- **Multi-delete**: Ctrl+X to delete all selected items at once
- **Paste as text**: Ctrl+Shift+V to paste multiple items as combined text

### Navigation
- **Page scrolling**: PageUp/PageDown to scroll by visible page size
- **Jump to ends**: Home/End to jump to first/last item

### Clipboard Actions
- **Open link in browser**: Ctrl+Alt+R to open copied links in default browser
- **Auto path-to-URI toggle**: Option to auto-convert file paths to URIs (disabled by default)
- **Auto-recovery**: Automatically restarts clipboard monitoring after KDE crashes

### Build Optimizations
- Zen4 architecture optimizations (-march=znver4)
- LTO enabled for better performance

## Installation (Arch Linux)

```bash
git clone https://github.com/CamelliaV/vicinae-patched.git
cd vicinae-patched
makepkg -si
```

## Updating

```bash
./update.sh  # Checks upstream, updates PKGBUILD, rebuilds
```

## Original Project

See the [original Vicinae repository](https://github.com/vicinaehq/vicinae) for full documentation and features.
