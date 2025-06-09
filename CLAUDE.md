# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Nemo is the official file manager for the Cinnamon desktop environment, forked from GNOME Files (Nautilus) v3.4. It's written in C with GTK+3 and also manages the Cinnamon desktop. The project supports extensibility through Actions and search helpers.

## Build System

Nemo uses Meson as its build system (requires >=0.56.0).

### Essential Commands

```bash
# Setup build directory
meson setup build/

# Compile
meson compile -C build/

# Run tests
meson test -C build/

# Install
meson install -C build/
```

### Build Options

Configure builds using `meson configure` or `-D` flags:
- `deprecated_warnings=true/false` - Show deprecated feature warnings
- `exif=true/false` - EXIF parsing support (requires libexif)
- `xmp=true/false` - XMP support (requires Exempi)
- `gtk_doc=true/false` - Generate API reference
- `selinux=true/false` - SELinux support
- `tracker=auto/true/false` - Tracker search support

## Architecture

### Main Components

The codebase is organized into several key components:

1. **Main Application** (`src/`)
   - Entry point: `nemo-main.c`
   - Application class: `nemo-application.h/c`
   - Window management: `nemo-window.h/c`
   - View implementations: `nemo-icon-view.c`, `nemo-list-view.c`

2. **Desktop Manager** (`src/`)
   - Entry point: `nemo-desktop-main.c`
   - Desktop management: `nemo-desktop-manager.h/c`
   - Desktop windows: `nemo-desktop-window.h/c`

3. **Extension Framework** (`libnemo-extension/`)
   - Plugin API for external extensions
   - Column providers, menu providers, property page providers
   - Location widget providers, info providers

4. **Private Libraries** (`libnemo-private/`)
   - Core file operations: `nemo-file.h/c`, `nemo-directory.h/c`
   - Icon handling: `nemo-icon-info.h/c`, `nemo-icon-container.h/c`
   - Search engine: `nemo-search-engine.h/c`
   - Action system: `nemo-action.h/c`, `nemo-action-manager.h/c`

5. **Utility Libraries** (`eel/`)
   - Legacy Extended Eazel Libraries
   - Graphics utilities, GTK extensions, string utilities

### Executables Built

- `nemo` - Main file manager
- `nemo-desktop` - Desktop management component
- `nemo-autorun-software` - Autorun handler
- `nemo-connect-server` - Server connection dialog
- `nemo-open-with` - Open-with dialog

## Development Workflow

### Debugging

```bash
# Kill existing instances before testing
nemo --quit

# Debug with specific subsystems
NEMO_DEBUG=Actions nemo --debug
NEMO_DEBUG=Search nemo --debug
```

### Testing

Tests are located in `test/` and include:
- Search engine functionality
- Directory async operations  
- File copy operations

Run individual tests:
```bash
meson test -C build/ "Search Engine test"
meson test -C build/ "Directory Async test"
meson test -C build/ "Copy test"
```

## Extension Development

### Nemo Actions
- Custom context menu entries defined in `.nemo_action` files
- Located in `data/nemo-actions/` and user directories
- Action configuration widget: `nemo-action-config-widget.h/c`

### Search Helpers
- Content search plugins in `.nemo_search_helper` format
- Located in `search-helpers/`
- Support various file types (PDF, Office documents, EPUB, etc.)

### C Extensions
- Use `libnemo-extension` API
- Implement provider interfaces for different extension types
- See extension headers in `libnemo-extension/`

## Key Dependencies

- GTK+ 3.10.0+
- GLib 2.45.7+
- Cinnamon Desktop 4.8.0+
- XApp 2.0.0+
- JSON-GLib 1.6+
- Optional: libexif, Exempi, Tracker, libselinux