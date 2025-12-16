import unittest
import os
import sys

# Add parent dir to path to import modules
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import dt_cv_utils

class TestCVUtils(unittest.TestCase):
    def setUp(self):
        # Create a dummy image file for testing
        self.test_img = "test_image.jpg"
        with open(self.test_img, "wb") as f:
            f.write(b'\x00' * 1024) # 1KB dummy file

    def tearDown(self):
        if os.path.exists(self.test_img):
            os.remove(self.test_img)

    def test_sharpness_calculation_fallback(self):
        """Test sharpness calculation (likely using fallback since PIL/CV might fail on dummy data)."""
        # Note: On dummy 0-byte/random data, image libs usually raise or return 0.
        # dt_cv_utils handles exceptions by returning 0.0 or file size heuristic.
        score = dt_cv_utils.calculate_sharpness(self.test_img)
        self.assertTrue(score >= 0.0, "Sharpness score should be non-negative")

    def test_classify_image_fallback(self):
        """Test image classification tags."""
        tags = dt_cv_utils.classify_image(self.test_img)
        self.assertIsInstance(tags, list)
        # Should likely return generic tags or empty if analysis fails
        
    def test_missing_file(self):
        """Test behavior on missing file."""
        score = dt_cv_utils.calculate_sharpness("non_existent.jpg")
        self.assertEqual(score, 0.0)

if __name__ == '__main__':
    unittest.main()
