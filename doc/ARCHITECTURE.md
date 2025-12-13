# Darktable Architecture Overview

This document provides a high-level overview of the Darktable codebase structure to assist developers in navigating and understanding the system.

## Directory Structure

The core source code is located in the `src` directory. Key subdirectories include:

- **`src/cli`**: Contains the command-line interface implementation (`main.c`). This is the entry point for `darktable-cli` and handles argument parsing and headless operations.
- **`src/gui`**: Contains the GTK+ based Graphical User Interface code. This includes main window management (`gtk.c`), panels, styles, and preferences dialogs.
- **`src/common`**: Holds shared core logic and data structures used by both the CLI and GUI, such as image management, database interactions, and utility functions.
- **`src/lua`**: Implements the Lua scripting interface. It handles the integration between C code and Lua scripts, allowing for plugins and automation. Key files include `call.c` (calling Lua from C), `api.c` (exposing C API to Lua), and `events.c`.
- **`src/develop`**: detailed image processing pipeline code. This is where the "pixel pipe" logic resides, managing how modules apply effects to images.
- **`src/iop`**: Image Operation Modules. Each RAW processing module (e.g., exposure, white balance) is implemented here.
- **`src/dtgtk`**: Custom GTK widgets specific to Darktable.
- **`src/libs`**: Library modules, often corresponding to lighttable view modules.
- **`src/views`**: Implementation of different views (Lighttable, Darkroom, Tethering, Map, etc.).

## Key Components

### The Pixel Pipe
The pixel pipe is the heart of Darktable's image processing. It manages the chain of operations (IOPs) applied to an image. It is designed to be non-destructive and supports OpenCL for GPU acceleration.

### Lua Scripting
Darktable features a powerful Lua API that allows users to interact with the database, UI, and image export processes. Threading is handled carefully to ensure thread safety between the Lua state and the main GTK loop.

### Database
Darktable uses SQLite for its library database (`library.db`) and data database (`data.db`). The `src/common/database.c` module manages these connections.

## Building and Testing

Darktable uses CMake for its build system. 
- **Build**: `./build.sh` (wrapper around cmake)
- **Test**: `ctest` is used for running the test suite.

## Contributing

Please refer to `CONTRIBUTING.md` for guidelines on coding style, submission processes, and communication channels.
