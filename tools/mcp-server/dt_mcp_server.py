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

import dt_cv_utils

@mcp.tool()
def apply_style(img_id: int, style_name: str) -> str:
    """Apply a named style (preset) to an image."""
    result = send_lua_command("apply_style", {"img_id": img_id, "style_name": style_name})
    return json.dumps(result)

@mcp.tool()
def auto_tag_selection(args: dict = {}) -> str:
    """Automatically analyzes selected images and adds descriptive tags. Args: {'ai_sensitivity': 'Strict'|'Normal'} """
    # 1. Get selection from Lua
    resp = send_lua_command("get_selection", {})
    if resp.get("status") != "ok":
        return "Failed to get selection: " + resp.get("error", "Unknown error")
    
    selection = resp.get("data", [])
    if not selection:
        return "No images selected."

    results = []
    
    # 2. Analyze and Tag
    for img in selection:
        img_id = img["id"]
        path = img["path"]
        
        tags = dt_cv_utils.classify_image(path)
        
        for tag in tags:
            send_lua_command("attach_tag", {"img_id": img_id, "tag_name": tag})
        
        results.append(f"Image {img['id']}: Added {', '.join(tags)}")
        
    return "\n".join(results)

@mcp.tool()
def cull_selection(args: dict = {}) -> str:
    """Analyzes sharpness of selected images. Args: {'ai_sensitivity': 'Strict'|'Normal'}"""
    # 1. Get selection
    # 1. Get selection
    resp = send_lua_command("get_selection", {})
    if resp.get("status") != "ok":
         return "Failed to get selection: " + resp.get("error")
         
    selection = resp.get("data", [])
    if not selection:
        return "No images selected."
        
    if len(selection) < 2:
        return "Select at least 2 images to compare."

    # 2. Score images
    scores = []
    for img in selection:
        score = dt_cv_utils.calculate_sharpness(img["path"])
        scores.append((score, img))
        
    # 3. Sort by score desc
    scores.sort(key=lambda x: x[0], reverse=True)
    
    best_img = scores[0][1]
    best_score = scores[0][0]
    
    # 4. Apply labels
    # Best -> Green (color: "green")
    send_lua_command("set_color_label", {"img_id": best_img["id"], "color": "green"})
    
    # Others -> Red ("red")
    for i in range(1, len(scores)):
        img = scores[i][1]
        send_lua_command("set_color_label", {"img_id": img["id"], "color": "red"})
        
    return f"Culled {len(selection)} images. Winner: {best_img['filename']} (Score: {best_score:.2f})"


@mcp.tool()
def generate_mask(args: dict = {}) -> str:
    """Generates a foreground mask for selected images. Args: {'ai_sensitivity': 'Strict'|'Normal'}"""
    # 1. Get selection
    resp = send_lua_command("get_selection", {})
    if resp.get("status") != "ok":
         return "Failed to get selection: " + resp.get("error")
         
    selection = resp.get("data", [])
    if not selection:
        return "No images selected."

    results = []
    for img in selection:
        path = img["path"]
        write_gui_status(f"Generating mask for {os.path.basename(path)}...")
        
        mask_path, status = dt_cv_utils.generate_mask(path)
        if mask_path:
            results.append(f"Generated: {os.path.basename(mask_path)} ({status})")
        else:
            results.append(f"Failed: {os.path.basename(path)} ({status})")
            
    return "\n".join(results)

@mcp.tool()
def generative_edit(args: dict = {}) -> str:
    """Opens an editor to heal/remove objects from selected images."""
    # 1. Get selection
    resp = send_lua_command("get_selection", {})
    if resp.get("status") != "ok":
         return "Failed to get selection: " + resp.get("error")
         
    selection = resp.get("data", [])
    if not selection:
        return "No images selected."

    results = []
    for img in selection:
        path = img["path"]
        write_gui_status(f"Opening editor for {os.path.basename(path)}...")
        
        # This will block until editor is closed
        out_path, status = dt_cv_utils.open_inpainting_editor(path)
        
        if out_path:
            results.append(f"Inpainted: {os.path.basename(out_path)} ({status})")
        else:
            results.append(f"Cancelled: {os.path.basename(path)} ({status})")
            
    return "\n".join(results)


import dt_cloud
import glob
import time



STATUS_FILE = os.path.join(MCP_DIR, "gui_status.json")

def write_gui_status(message: str, is_error: bool = False):
    """Writes a status message for the Lua GUI to consume."""
    try:
        data = {
            "message": message,
            "error": is_error,
            "timestamp": time.time()
        }
        # Atomic write
        tmp = STATUS_FILE + ".tmp"
        with open(tmp, 'w') as f:
            json.dump(data, f)
        os.rename(tmp, STATUS_FILE)
    except Exception as e:
        print(f"Failed to write GUI status: {e}")

def monitor_uploads():
    """Background thread to monitor and process upload and GUI requests."""
    print("[Upload Monitor] Started monitoring for requests...")
    write_gui_status("MCP Server Ready")
    
    # We map tool names from the GUI request to local functions
    TOOL_MAP = {
        "auto_tag_selection": auto_tag_selection,
        "cull_selection": cull_selection,
        "generate_mask": generate_mask,
        "generative_edit": generative_edit
    }
    
    while True:
        try:
            # 1. Check for UPLOAD requests
            pattern_up = os.path.join(MCP_DIR, "upload_*.json")
            files_up = glob.glob(pattern_up)
            
            for f in files_up:
                if f.endswith(".tmp"): continue
                try:
                    write_gui_status("Starting upload...")
                    with open(f, 'r') as json_file:
                        data = json.load(json_file)
                    
                    source = data.get("source_file")
                    target_remote = data.get("target_remote", "remote:photos")
                    
                    if source and os.path.exists(source):
                        print(f"[Upload Monitor] Uploading {os.path.basename(source)} to {target_remote}...")
                        success, msg = dt_cloud.upload_file(source, target_remote)
                        print(f"[Upload Monitor] Upload Result: {msg}")
                        write_gui_status(f"Upload: {msg}", is_error=not success)
                    else:
                        print(f"[Upload Monitor] Invalid upload request: {source}")
                        write_gui_status("Upload Error: File not found", is_error=True)
                except Exception as e:
                    print(f"[Upload Monitor] Error processing upload {f}: {e}")
                    write_gui_status(f"Upload Exception: {str(e)}", is_error=True)
                
                try:
                    os.remove(f)
                except: pass
            
            # 2. Check for GUI requests (AI Tools)
            pattern_gui = os.path.join(MCP_DIR, "gui_request_*.json")
            files_gui = glob.glob(pattern_gui)
            
            for f in files_gui:
                if f.endswith(".tmp"): continue
                try:
                    with open(f, 'r') as json_file:
                        req = json.load(json_file)
                    
                    tool_name = req.get("tool")
                    args = req.get("args", {})
                    
                    print(f"[GUI Monitor] Detected request: {tool_name} with args {args}")
                    write_gui_status(f"Running {tool_name}...")
                    
                    if tool_name in TOOL_MAP:
                        # Execute the tool
                        func = TOOL_MAP[tool_name]
                        # Pass args if tool accepts them (we will update tools to accept **kwargs)
                        # For now, just pass args as a dict
                        result = func(args) 
                        print(f"[GUI Monitor] Tool Output: {result}")
                        
                        # Write success status
                        # Truncate long results for UI label
                        short_res = (result[:40] + '..') if len(result) > 40 else result
                        write_gui_status(short_res)
                    else:
                        print(f"[GUI Monitor] Unknown tool: {tool_name}")
                        write_gui_status(f"Unknown tool: {tool_name}", is_error=True)
                        
                except Exception as e:
                    print(f"[GUI Monitor] Error processing GUI request {f}: {e}")
                    write_gui_status(f"Error: {str(e)}", is_error=True)
                
                try:
                    os.remove(f)
                except: pass


            time.sleep(1) # Poll interval
        except Exception as e:
            print(f"[Upload Monitor] Fatal error: {e}")
            time.sleep(5)

if __name__ == "__main__":
    # Start upload monitor in background
    import threading
    t = threading.Thread(target=monitor_uploads, daemon=True)
    t.start()
    
    mcp.run()
