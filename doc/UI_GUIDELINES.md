# Darktable UI Guidelines

This document outlines the standards for designing and implementing User Interfaces in Darktable. Adhering to these guidelines ensures a consistent, professional, and maintainable user experience.

## 1. Architecture Overview

Darktable's UI is built on **GTK+ 3**, but employs a custom widget library called **Bauhaus** for technical controls (especially within Image Operation Modules).

- **GTK Widgets**: Use for general layout (Grids, Boxes, Panes), standard dialogs, preferences, and top-level navigation.
- **Bauhaus Widgets**: Use for **all** image processing parameters (Sliders, Comboboxes, Toggles) inside developed modules (`src/iop`). These widgets handle parameter binding, introspection, and specialized interaction (fine-tuning, keyboard input).

## 2. Color Palette

Darktable uses a defined set of semantic colors in `data/themes/darktable.css`. **Do not hardcode hex values in C code.** Use the defined GTK/CSS names.

### Core UI Colors
| Semantic Name | Description | Typical Usage |
| :--- | :--- | :--- |
| `@bg_color` | Main background | Default window background |
| `@fg_color` | Main foreground | Default text color |
| `@grey_40` | Lighttable BG | Background for the Lighttable view |
| `@grey_50` | Darkroom BG | Background for the Darkroom view (mid-grey for color perception) |

### Interactive Elements
| Semantic Name | Usage |
| :--- | :--- |
| `@button_bg` / `@button_hover_bg` | Standard Buttons |
| `@button_fg` | Button text |
| `@field_bg` / `@field_fg` | Text Entry fields |
| `@bauhaus_fg_selected` | Active/Selected state in Bauhaus widgets |

### Semantic Colors
| Name | RGB Value | Purpose |
| :--- | :--- | :--- |
| `@graph_red` | `rgb(237,30,20)` | Red channel graphs/overlays |
| `@graph_green` | `rgb(28,235,26)` | Green channel graphs/overlays |
| `@graph_blue` | `rgb(14,14,233)` | Blue channel graphs/overlays |

## 3. Widget Usage Guidelines

### When to use `dt_bauhaus`
Use `dt_bauhaus` widgets whenever you are exposing a parameter that affects image processing or requires the standard "Darktable look and feel" (minimalist, scalable).

- **Sliders**: `dt_bauhaus_slider_new_with_range`
- **Comboboxes**: `dt_bauhaus_combobox_new`
- **Toggles**: Use Bauhaus Quad Toggles for icon-based options in module headers.

### When to use Native GTK
Use standard `GtkWidget` (Button, Entry, Label) for:
- Dialog boxes (Import, Export, Preferences).
- Top/Bottom panels (Navigation).
- Views management (`src/views`).

## 4. Layout & Spacing
- **Margins**: Avoid pixel-perfect margins in C code. Use CSS classes where possible.
- **Alignment**: Use `GtkBox` and `GtkGrid` for layout. Avoid `GtkFixed` or manual positioning unless absolutely necessary (e.g., custom drawing in Lighttable).
- **CSS Classes**:
    - `.dt_module_btn`: For icon buttons in module headers.
    - `.dt_section_label`: For section sub-headers.

## 5. Accessibility (A11Y)
*Work in Progress*
- All custom widgets (`GtkDrawingArea` subclasses) must implement `AtkObject` logic to be visible to screen readers.
- Ensure all interactive elements have a tooltip.
