
import sys
import os

# Try importing OpenCV for robust processing
try:
    import cv2
    import numpy as np
    HAS_OPENCV = True
except ImportError:
    HAS_OPENCV = False

# Try importing PIL as fallback
try:
    from PIL import Image, ImageStat
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

def calculate_sharpness(image_path):
    """
    Calculates the sharpness of an image.
    Returns a float score (higher is sharper).
    """
    if not os.path.exists(image_path):
        return 0.0

    if HAS_OPENCV:
        try:
            img = cv2.imread(image_path)
            if img is None: return 0.0
            gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
            # Variance of Laplacian is a standard measure for focus
            return cv2.Laplacian(gray, cv2.CV_64F).var()
        except Exception as e:
            print(f"OpenCV error on {image_path}: {e}")
            return 0.0
            
    if HAS_PIL:
        try:
            im = Image.open(image_path).convert('L')
            stat = ImageStat.Stat(im)
            # Standard deviation can be a rough proxy for contrast/sharpness in a pinch, 
            # though less accurate than Laplacian.
            return stat.stddev[0]
        except Exception:
            return 0.0

    # Fallback: File size (heuristic: detailed images often compress less)
    try:
        return float(os.path.getsize(image_path))
    except:
        return 0.0

def classify_image(image_path):
    """
    Returns a list of suggested tags based on image properties.
    """
    tags = ["Autotagged"]
    if not os.path.exists(image_path):
        return tags

    # Basic Heuristics
    try:
        width = 0
        height = 0
        
        if HAS_OPENCV:
            img = cv2.imread(image_path)
            if img is not None:
                height, width, _ = img.shape
                # Simple brightness check
                hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
                brightness = hsv[...,2].mean()
                if brightness < 40: tags.append("Low Light")
                if brightness > 200: tags.append("High Key")
        elif HAS_PIL:
            im = Image.open(image_path)
            width, height = im.size
            
        # Orientation
        if width > height: tags.append("Landscape")
        elif height > width: tags.append("Portrait")
        else: tags.append("Square")

    except Exception as e:
        print(f"Classification error: {e}")
        
    return tags

def generate_mask(image_path):
    """
    Generates a foreground mask for the image using rembg.
    Saves as [basename]_mask.png.
    Returns the path to the mask file or None if failed.
    """
    if not os.path.exists(image_path):
        return None, "File not found"

    # Define mask path
    base, ext = os.path.splitext(image_path)
    mask_path = f"{base}_mask.png"
    
    # Try importing rembg
    try:
        from rembg import remove
        
        # Read image
        if HAS_OPENCV:
            img = cv2.imread(image_path)
            # rembg expects PIL or bytes, but newer versions work well with adapters.
            # Easiest is to convert to bytes or use PIL
            success, encoded_img = cv2.imencode(ext, img)
            if success:
                input_data = encoded_img.tobytes()
                output_data = remove(input_data)
                with open(mask_path, 'wb') as o:
                    o.write(output_data)
                return mask_path, "Mask generated with rembg"
        elif HAS_PIL:
            with open(image_path, 'rb') as i:
                input_data = i.read()
                output_data = remove(input_data)
                with open(mask_path, 'wb') as o:
                    o.write(output_data)
                return mask_path, "Mask generated with rembg"
                
    except ImportError:
        pass # Fallback below
    except Exception as e:
        print(f"Rembg failed: {e}")
        # Continue to fallback
    
    # Fallback: Center Circle Mask (if rembg not installed)
    try:
        if HAS_OPENCV:
            img = cv2.imread(image_path)
            if img is not None:
                h, w = img.shape[:2]
                mask = np.zeros((h, w), dtype=np.uint8)
                center = (w // 2, h // 2)
                radius = min(w, h) // 3
                cv2.circle(mask, center, radius, (255), -1)
                
                # Save as transparent PNG? 
                # Raster masks in DT can be just B&W images.
                cv2.imwrite(mask_path, mask)
                return mask_path, "Mask generated (Fallback: Center Circle)"
                
        if HAS_PIL:
             # PIL Fallback
            from PIL import ImageDraw
            im = Image.new('L', (500, 500), 0) # We might not know size if we didn't read original
            # Let's try to read original first
            orig = Image.open(image_path)
            w, h = orig.size
            im = Image.new('L', (w, h), 0)
            draw = ImageDraw.Draw(im)
            draw.ellipse((w//4, h//4, 3*w//4, 3*h//4), fill=255)
            im.save(mask_path)
            return mask_path, "Mask generated (Fallback: PIL Center Circle)"

    except Exception as e:
        return None, f"Mask generation failed: {e}"

    return None, "No suitable libraries found"

# --- Generative Edit / Inpainting ---

class MaskEditor:
    def __init__(self, image, win_name="Generative Edit - Paint Mask (Red)"):
        self.image = image
        self.mask = np.zeros(image.shape[:2], dtype=np.uint8)
        self.drawing = False
        self.brush_size = 20
        self.win_name = win_name
        self.display_image = image.copy()
        
    def mouse_callback(self, event, x, y, flags, param):
        if event == cv2.EVENT_LBUTTONDOWN:
            self.drawing = True
            cv2.circle(self.mask, (x, y), self.brush_size, 255, -1)
            cv2.circle(self.display_image, (x, y), self.brush_size, (0, 0, 255), -1)
            
        elif event == cv2.EVENT_MOUSEMOVE:
            if self.drawing:
                cv2.circle(self.mask, (x, y), self.brush_size, 255, -1)
                cv2.circle(self.display_image, (x, y), self.brush_size, (0, 0, 255), -1)
                
        elif event == cv2.EVENT_LBUTTONUP:
            self.drawing = False
            
    def run(self):
        cv2.namedWindow(self.win_name)
        cv2.setMouseCallback(self.win_name, self.mouse_callback)
        
        print("Draw mask with mouse. Press ENTER to apply, 'r' to reset, 'q' to quit.")
        
        while True:
            cv2.imshow(self.win_name, self.display_image)
            key = cv2.waitKey(20) & 0xFF
            
            if key == 13: # Enter
                cv2.destroyWindow(self.win_name)
                return self.mask
            elif key == ord('r'):
                self.mask[:] = 0
                self.display_image = self.image.copy()
            elif key == ord('q'):
                cv2.destroyWindow(self.win_name)
                return None
        return None

def open_inpainting_editor(image_path):
    """
    Opens an OpenCV window to paint a mask.
    Returns: (output_path, status)
    """
    if not HAS_OPENCV:
        return None, "OpenCV not available"
        
    if not os.path.exists(image_path):
        return None, "File not found"
        
    try:
        img = cv2.imread(image_path)
        if img is None: return None, "Failed to read image"
        
        editor = MaskEditor(img)
        mask = editor.run()
        
        if mask is not None:
            # Apply Inpainting
            # Telea algorithm is good for small defects
            radius = 3
            inpainted = cv2.inpaint(img, mask, radius, cv2.INPAINT_TELEA)
            
            base, ext = os.path.splitext(image_path)
            out_path = f"{base}_inpainted.jpg"
            cv2.imwrite(out_path, inpainted)
            
            return out_path, "Inpainting successful"
        else:
            return None, "Edit cancelled"
            
    except Exception as e:
        return None, f"Editor failed: {e}"
