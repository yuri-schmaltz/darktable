
import os
import shutil
import subprocess
import time

def check_rclone():
    """Checks if rclone is available in PATH."""
    return shutil.which("rclone") is not None

def upload_file(file_path, target_remote="remote:photos"):
    """
    Uploads a file to the cloud.
    
    Args:
        file_path (str): Path to the local file.
        target_remote (str): Rclone remote path (e.g., 'gphotos:album').
                             Defaults to 'remote:photos'.
    
    Returns:
        bool: True if successful, False otherwise.
        str: Log message.
    """
    if not os.path.exists(file_path):
        return False, f"File not found: {file_path}"

    if check_rclone():
        # Rclone implementation
        try:
            # rclone copy /path/to/file remote:path
            cmd = ["rclone", "copy", file_path, target_remote]
            result = subprocess.run(cmd, capture_output=True, text=True)
            
            if result.returncode == 0:
                return True, f"Successfully uploaded {os.path.basename(file_path)} to {target_remote}"
            else:
                return False, f"Rclone error: {result.stderr}"
        except Exception as e:
            return False, f"Execution error: {str(e)}"
    else:
        # Mock implementation
        print(f"[MOCK CLOUD] Simulating upload of {file_path}...")
        time.sleep(1) # Simulate network lag
        return True, f"[MOCK] Uploaded {os.path.basename(file_path)} (Rclone not found)"

if __name__ == "__main__":
    # Test
    print(upload_file("test.jpg"))
