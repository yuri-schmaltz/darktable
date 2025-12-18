import unittest
import os
import sys
from unittest.mock import patch, MagicMock

# Add parent dir to path
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import dt_cloud

class TestCloudUtils(unittest.TestCase):

    def setUp(self):
        self.test_file = "dummy.jpg"
        with open(self.test_file, "w") as f:
            f.write("test")

    def tearDown(self):
        if os.path.exists(self.test_file):
            os.remove(self.test_file)

    @patch('shutil.which')
    def test_rclone_check_installed(self, mock_which):
        """Test behavior when rclone is installed."""
        mock_which.return_value = "/usr/bin/rclone" # Simulate installed
        self.assertTrue(dt_cloud.check_rclone())

    @patch('shutil.which')
    @patch('subprocess.run')
    def test_upload_rclone_success(self, mock_run, mock_which):
        """Test successful rclone upload."""
        mock_which.return_value = "/usr/bin/rclone"
        
        # Mock successful subprocess execution
        mock_proc = MagicMock()
        mock_proc.returncode = 0
        mock_run.return_value = mock_proc
        
        # We need to make sure check_rclone uses the mocked shutil.which
        # checking implementation: it calls shutil.which directly.
        
        success, msg = dt_cloud.upload_file(self.test_file)
        
        self.assertTrue(success)
        self.assertIn("Successfully uploaded", msg)
        mock_run.assert_called()

    @patch('shutil.which')
    def test_upload_mock_fallback(self, mock_which):
        """Test fallback to mock upload when rclone is missing."""
        mock_which.return_value = None # Simulate missing rclone
        
        success, msg = dt_cloud.upload_file(self.test_file)
        
        self.assertTrue(success)
        self.assertIn("[MOCK] Uploaded", msg)

if __name__ == '__main__':
    unittest.main()
