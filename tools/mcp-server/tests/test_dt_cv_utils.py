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

    @unittest.mock.patch('dt_cv_utils.cv2')
    def test_generate_mask_opencv_fallback(self, mock_cv2):
        """Test mask generation using OpenCV fallback (Circle)."""
        # Simulate rembg missing
        with unittest.mock.patch.dict(sys.modules, {'rembg': None}):
            # Mock cv2.imread to return a valid dummy image (100x100)
            mock_img = unittest.mock.MagicMock()
            mock_img.shape = (100, 100, 3)
            mock_cv2.imread.return_value = mock_img
            
            # Mock imwrite to just succeed
            mock_cv2.imwrite.return_value = True
            
            # Force HAS_OPENCV to True for this test context if possible, 
            # but dt_cv_utils imports it at top level. 
            # We assume the environment HAS_OPENCV is set based on import.
            # If not, this test might skip logic.
            # We can force the variable in the module
            dt_cv_utils.HAS_OPENCV = True
            
            mask_path, status = dt_cv_utils.generate_mask(self.test_img)
            
            self.assertIsNotNone(mask_path)
            self.assertTrue(mask_path.endswith("_mask.png"))
            self.assertIn("Fallback", status)

if __name__ == '__main__':
    unittest.main()
