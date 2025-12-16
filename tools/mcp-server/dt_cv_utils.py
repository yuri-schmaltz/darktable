
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
