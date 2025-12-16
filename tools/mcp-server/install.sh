#!/bin/bash

# Configuration
DT_CONFIG_DIR="$HOME/.config/darktable"
MCP_DIR="$DT_CONFIG_DIR/mcp"
LUA_DIR="$DT_CONFIG_DIR/lua"
SERVICE_DIR="$HOME/.config/systemd/user"

echo "=== Darktable AI Tools Installer ==="

# 1. Check Python
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 could not be found."
    exit 1
fi

# 2. Setup Directories
echo "[*] Creating configuration directories..."
mkdir -p "$MCP_DIR"
mkdir -p "$LUA_DIR"
mkdir -p "$SERVICE_DIR"

# 3. Copy Files
echo "[*] Copying Python Server files..."
cp dt_mcp_server.py dt_clound.py dt_cv_utils.py requirements.txt "$MCP_DIR/" 2>/dev/null || cp *.py requirements.txt "$MCP_DIR/"

echo "[*] Copying Lua Bridge files..."
cp dt_mcp_bridge.lua "$LUA_DIR/"

# 4. Install Dependencies
echo "[*] Installing Python dependencies (pip)..."
pip3 install -r "$MCP_DIR/requirements.txt" --user

# 5. Configure Lua
LUARC="$DT_CONFIG_DIR/luarc"
if [ ! -f "$LUARC" ]; then
    echo "[*] Creating luarc..."
    touch "$LUARC"
fi

if ! grep -q "dt_mcp_bridge" "$LUARC"; then
    echo "[*] Activating Lua script in luarc..."
    echo 'require "dt_mcp_bridge"' >> "$LUARC"
else
    echo "[*] Lua script already active in luarc."
fi

# 6. Install Service
echo "[*] Installing Systemd Service..."
cp dt-mcp.service "$SERVICE_DIR/"
systemctl --user daemon-reload
systemctl --user enable dt-mcp.service
systemctl --user restart dt-mcp.service

echo "=== Installation Complete ==="
echo "The AI Server is running in the background."
echo "Please restart Darktable to see the new AI Assistant panel."
