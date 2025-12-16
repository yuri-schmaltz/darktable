---
title: "Darktable AI Assistant"
subtitle: "User Manual v1.0"
author: "Antigravity (AI Agent)"
date: "December 16, 2025"
geometry: "margin=2cm"
output: pdf_document
---

# Introduction

The **Darktable AI Assistant** extends Darktable with state-of-the-art Artificial Intelligence capabilities. This integration bridges the gap between open-source RAW editing and commercial AI tools, providing features like Subject Selection, Generative Fill, and Intelligent Automation directly within your workflow.

# Installation

## Automatic Installation

1.  Open your terminal.
2.  Run the installation script included in the toolset:
    ```bash
    bash tools/mcp-server/install.sh
    ```
3.  Restart Darktable.
4.  Look for the **"AI Assistant"** panel in the Lighttable view (left sidebar).

---

# Feature Guide

## 1. AI Masking (Select Subject)

Automatically creates precise masks for the main subject in your photo.

*   **How to use**:
    1.  Select an image in the Lighttable view.
    2.  Click the **"Generate Mask"** button in the AI Assistant panel.
    3.  Wait for the status indicator to show "Mask generated".
    4.  Switch to the Darkroom view.
    5.  Open any module (e.g., Exposure, Color Balance).
    6.  Click the **Raster Mask** icon and select the generated mask file (e.g., `image_name_mask.png`).

## 2. Generative Edit (Inpainting)

Removes unwanted objects by intelligently filling in the background.

*   **How to use**:
    1.  Select an image.
    2.  Click **"Generative Edit"**.
    3.  A dedicated editor window will open.
    4.  **Paint**: Use your mouse to paint red over the object you want to remove.
    5.  **Apply**: Press **ENTER** on your keyboard.
    6.  The result is saved as `image_name_inpainted.jpg` and imported back into Darktable.

## 3. Auto Develop

Instantly corrects exposure for underexposed RAW files using statistical analysis.

*   **How to use**:
    1.  Select one or more underexposed images.
    2.  Click **"Auto Develop"**.
    3.  The system analyzes the image brightness and applies an exposure compensation style automatically.

## 4. Smart Culling & Tagging

Organize your library automatically.

*   **Auto Tag**: Analyzes image content (Low Light, High Key, etc.) and adds Darktable tags.
*   **Smart Cull**: Analyzes sharpness and assigns color labels (Green for sharp, Red for blurry) to help you pick the best shot.

## 5. Cloud Sync

Automatically backs up your exported photos to the cloud.

*   **Setup**: Configure your cloud provider (Google Photos, Nextcloud) using `rclone config`.
*   **Usage**: In the **Export** module, select **"MCP Cloud Sync"** as your target storage.

---

# Troubleshooting

*   **"Server Offline"**: If the status says Offline, ensure the background service is running:
    ```bash
    systemctl --user status dt-mcp
    ```
*   **First Run Delay**: The first time you use AI Masking, the system downloads the neural network models (~100MB). This may take a minute.
*   **Logs**: To debug issues, check the logs:
    ```bash
    journalctl --user -u dt-mcp -f
    ```

---
*Created by Google Deepmind Advanced Agentic Coding.*
