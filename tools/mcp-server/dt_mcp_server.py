import sys
import os
import json
import sqlite3
import time
from typing import Any, List, Optional
import argparse

# Check if mcp is installed, if not, we can't fully run, but we can scaffold the code.
# In a real environment we'd rely on `pip install mcp`
try:
    from mcp.server.fastmcp import FastMCP
except ImportError:
    # Dummy class for development/scaffolding if lib missing
    class FastMCP:
        def __init__(self, name): self.name = name
        def resource(self, uri): return lambda x: x
        def tool(self): return lambda x: x
        def run(self): print("MCP library not found. Install 'mcp' implementation.")

mcp = FastMCP("darktable")

# Configuration
DT_CONFIG_DIR = os.path.expanduser("~/.config/darktable")
DB_PATH = os.path.join(DT_CONFIG_DIR, "library.db")
MCP_DIR = os.path.join(DT_CONFIG_DIR, "mcp")
CMD_FILE = os.path.join(MCP_DIR, "cmd.json")
RESP_FILE = os.path.join(MCP_DIR, "response.json")

def get_db_connection():
    conn = sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    return conn

def send_lua_command(cmd_name: str, args: dict) -> dict:
    """Writes command to file and waits for response from Lua bridge."""
    if not os.path.exists(MCP_DIR):
        os.makedirs(MCP_DIR)
        
    # Simple serialization: CMD|json_content
    payload = f"{cmd_name}|{json.dumps(args)}"
    
    with open(CMD_FILE, "w") as f:
        f.write(payload)
        
    # Wait for response (timeout 5s)
    for _ in range(50):
        if os.path.exists(RESP_FILE):
             with open(RESP_FILE, "r") as f:
                 resp = f.read()
             if resp:
                 # Clear response file
                 open(RESP_FILE, 'w').close()
                 return json.loads(resp)
        time.sleep(0.1)
        
    return {"status": "error", "error": "Timeout waiting for Lua bridge"}

@mcp.resource("darktable://images")
def list_images() -> str:
    """List recent images from the library."""
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT id, filename, rating FROM images ORDER BY id DESC LIMIT 50")
    images = [dict(row) for row in cursor.fetchall()]
    conn.close()
    return json.dumps(images, indent=2)

@mcp.tool()
def get_image_details(img_id: int) -> str:
    """Get detailed metadata for a specific image ID."""
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM images WHERE id = ?", (img_id,))
    row = cursor.fetchone()
    conn.close()
    if row:
        return json.dumps(dict(row), indent=2)
    return "Image not found"

@mcp.tool()
def set_rating(img_id: int, rating: int) -> str:
    """Set the star rating for an image (0-5, or -1 for reject)."""
    if not (-1 <= rating <= 5):
        return "Error: Rating must be between -1 and 5"
        
    result = send_lua_command("set_rating", {"img_id": img_id, "rating": rating})
    return json.dumps(result)

@mcp.tool()
def attach_tag(img_id: int, tag_name: str) -> str:
    """Attach a tag to an image."""
    result = send_lua_command("attach_tag", {"img_id": img_id, "tag_name": tag_name})
    return json.dumps(result)

@mcp.tool()
def apply_style(img_id: int, style_name: str) -> str:
    """Apply a named style (preset) to an image."""
    result = send_lua_command("apply_style", {"img_id": img_id, "style_name": style_name})
    return json.dumps(result)

if __name__ == "__main__":
    mcp.run()
