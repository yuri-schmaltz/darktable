import sys
import os
import json
import sqlite3
import time
import threading
from typing import Any, List, Optional
import shutil

# Check if mcp is installed
try:
    from mcp.server.fastmcp import FastMCP
except ImportError:
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

# Files
CMD_TMP_FILE = os.path.join(MCP_DIR, "cmd.json.tmp")
CMD_FILE = os.path.join(MCP_DIR, "cmd.json")
RESP_FILE = os.path.join(MCP_DIR, "response.json")

# Concurrency Lock
cmd_lock = threading.Lock()

def get_db_connection():
    # Retry logic for locked DB
    attempts = 0
    while attempts < 3:
        try:
            conn = sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True)
            conn.row_factory = sqlite3.Row
            return conn
        except sqlite3.OperationalError:
            time.sleep(0.1)
            attempts += 1
    raise Exception("Database locked or inaccessible")

def send_lua_command(cmd_name: str, args: dict) -> dict:
    """Thread-safe, atomic command sending."""
    with cmd_lock:
        if not os.path.exists(MCP_DIR):
            os.makedirs(MCP_DIR)
        
        # Clean up any stale response file
        if os.path.exists(RESP_FILE):
             os.remove(RESP_FILE)

        # JSON payload
        payload = json.dumps({"cmd": cmd_name, "args": args})
        
        # Atomic Write: Write to tmp then rename
        with open(CMD_TMP_FILE, "w") as f:
            f.write(payload)
            
        os.rename(CMD_TMP_FILE, CMD_FILE)
            
        # Wait for response (timeout 5s)
        for _ in range(50):
            if os.path.exists(RESP_FILE):
                 # Give a tiny buffer for the atomic move to finish on file system (though rename is atomic)
                 try:
                     with open(RESP_FILE, "r") as f:
                         resp_content = f.read()
                     
                     if resp_content:
                         os.remove(RESP_FILE)
                         try:
                             return json.loads(resp_content)
                         except json.JSONDecodeError:
                             return {"status": "error", "error": "Invalid JSON response from bridge"}
                 except FileNotFoundError:
                     pass # Race condition check
            time.sleep(0.1)
            
        return {"status": "error", "error": "Timeout waiting for Lua bridge"}

@mcp.resource("darktable://images")
def list_images() -> str:
    """List recent images from the library."""
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        cursor.execute("SELECT id, filename, rating FROM images ORDER BY id DESC LIMIT 50")
        images = [dict(row) for row in cursor.fetchall()]
        conn.close()
        return json.dumps(images, indent=2)
    except Exception as e:
        return json.dumps({"error": str(e)})

@mcp.tool()
def get_image_details(img_id: int) -> str:
    """Get detailed metadata for a specific image ID."""
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM images WHERE id = ?", (img_id,))
        row = cursor.fetchone()
        conn.close()
        if row:
            return json.dumps(dict(row), indent=2)
        return "Image not found"
    except Exception as e:
         return str(e)

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
