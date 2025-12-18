# Darktable AI Assistant (MCP Server)

A robust, AI-powered extension for Darktable that adds intelligent features like Auto-Tagging, Smart Culling, AI Masking, Generative Edit, and Cloud Sync. This system uses a decoupled **Lua-to-Python Bridge** architecture to ensure stability and performance.

## Features

### 1. AI Masking ("Select Subject")
*   **Engine**: `rembg` (U-2-Net based background removal).
*   **Function**: Generates high-quality raster masks (PNG) for selected images.
*   **Usage**: Select image -> Click "Generate Mask" -> Import generated PNG as Raster Mask in Darkroom.
*   **Fallback**: Features a geometric fallback (Center-Weighted) if AI models are missing.

### 2. Generative Edit ("Heal")
*   **Engine**: OpenCV Inpainting (Telea Algorithm).
*   **Function**: Interactive GUI to remove unwanted objects or defects.
*   **Usage**: Select image -> Click "Generative Edit" -> Paint Mask Red -> Press ENTER.

### 3. Auto-Develop
*   **Engine**: Statistical Brightness Analysis.
*   **Function**: Automatically calculates optimal Exposure Bias (EV) for underexposed images.
*   **Mechanism**: Generates a dynamic `.dtstyle` file and applies it via Lua.

### 4. Smart Culling & Tagging
*   **Culling**: Analyzes image sharpness (Laplacian Variance) to score and color-code images (Green=Sharp, Red=Blurry).
*   **Tagging**: Analyzes image content (brightness/dynamic range) to auto-tag "Low Light", "High Key", etc.

### 5. Cloud Sync
*   **Engine**: Wrapper around `rclone`.
*   **Function**: background upload to configured remotes (Google Photos, Nextcloud, S3).

## Architecture

The system consists of two components communicating via **Atomic File Operations** (JSON payloads):

1.  **Lua Frontend** (`dt_mcp_bridge.lua`):
    *   Embedded in Darktable.
    *   Handles UI (AI Assistant Panel).
    *   Sends commands via `~/.config/darktable/mcp/gui_request_TIMESTAMP.json`.
    *   Polls status from `~/.config/darktable/mcp/gui_status.json`.

2.  **Python Backend** (`dt_mcp_server.py`):
    *   Runs as a background service.
    *   Watches for request files.
    *   Executes heavy Computer Vision tasks (OpenCV/PyTorch).
    *   Writes status updates.

This decoupled design ensures that heavy AI processing **never freezes** the Darktable user interface.

## Installation

### Automatic (Recommended)
Run the included installer script:
```bash
bash install.sh
```
This will:
1.  Install Python dependencies (`rembg`, `opencv-python-headless`, etc.).
2.  Deploy scripts to `~/.config/darktable/`.
3.  Register the `dt-mcp.service` with Systemd for auto-start.

### Manual
1.  Install requirements: `pip install -r requirements.txt`
2.  Copy `dt_mcp_bridge.lua` to `~/.config/darktable/lua/`.
3.  Add `require "dt_mcp_bridge"` to `~/.config/darktable/luarc`.
4.  Copy `*.py` to `~/.config/darktable/mcp/`.
5.  Run server: `python3 ~/.config/darktable/mcp/dt_mcp_server.py`.

## Troubleshooting

*   **"Server Offline" in UI**: Ensure the python script is running (`systemctl --user status dt-mcp`).
*   **AI Models Downloading**: The first run of Masking/Tagging may take time to download `rembg` models (~100MB). Check terminal output if running manually.
*   **Logs**: Check `journalctl --user -u dt-mcp -f` for server logs.
