# Nemo Preview Pane Development Plan

## Overview
Implementation roadmap for adding a preview pane feature to Nemo file manager. The preview pane will display file previews, metadata, and other relevant information for selected files.

## Development Phases

### Phase 1 ✅ **COMPLETED**
**Foundation Infrastructure**
- [x] Create `NemoPreviewPane` widget class extending `GtkScrolledWindow`
- [x] Integrate preview pane into `NemoWindow` layout via `content_preview_paned`
- [x] Add preview pane management API functions:
  - `nemo_window_preview_pane_showing()`
  - `nemo_window_show_preview_pane()`
  - `nemo_window_hide_preview_pane()`
- [x] Update build system to include new source files
- [x] Maintain backward compatibility with existing layouts
- [x] Create basic widget structure with placeholder content

### Phase 1.5 🎯 **IN PROGRESS**
**UI Toggle Implementation**
- [ ] Find appropriate UI location for preview pane toggle
  - Options: View menu, toolbar button, keyboard shortcut, or context menu
- [ ] Implement menu item or toolbar button for preview pane toggle
- [ ] Connect toggle UI to existing show/hide API functions
- [ ] Test preview pane visibility and positioning in window layout
- [ ] Verify dummy "No file selected" label appears correctly

### Phase 2 📋 **PLANNED**
**File Type Detection and Basic Preview**
- [ ] Implement file type detection system
- [ ] Add basic preview renderers for common file types:
  - Text files (.txt, .md, .log, etc.)
  - Image files (.jpg, .png, .gif, etc.)
  - Basic document formats
- [ ] Create preview content management system
- [ ] Handle file loading and error states
- [ ] Add loading indicators and error messages

### Phase 3 📋 **PLANNED**
**Selection Handling and File Content Display**
- [ ] Connect preview pane to file selection events
- [ ] Implement `nemo_preview_pane_set_file()` function
- [ ] Add file metadata display (size, dates, permissions)
- [ ] Handle multiple file selection states
- [ ] Optimize preview loading for performance
- [ ] Add preview refresh mechanisms

### Phase 4 📋 **PLANNED**
**Advanced Preview Features**
- [ ] Implement thumbnail generation and caching
- [ ] Add support for more file types:
  - PDF documents
  - Audio files (metadata display)
  - Video files (thumbnails/metadata)
  - Archive files (content listing)
- [ ] Add preview customization options
- [ ] Implement preview pane width persistence
- [ ] Add accessibility features
- [ ] Performance optimizations for large files

## Technical Architecture

### Core Components
- **`NemoPreviewPane`**: Main preview widget class
- **`content_preview_paned`**: Container integrating preview with main content
- **Preview API**: Window-level functions for show/hide/status

### Window Layout Structure
```
NemoWindow
├── content_preview_paned (GtkPaned)
    ├── existing content area (notebook, views, etc.)
    └── preview_pane (NemoPreviewPane)
        └── content_box (GtkBox)
            ├── file info section
            ├── preview content area
            └── metadata section
```

### Integration Points
- File selection events from `NemoView` classes
- Window layout management in `nemo-window.c`
- Menu/toolbar integration for toggle controls
- Settings persistence for user preferences

## Implementation Notes

### Design Principles
- Maintain full backward compatibility
- Follow existing Nemo UI patterns and conventions
- Ensure performance doesn't degrade with preview pane disabled
- Support all existing window layouts (split view, sidebar combinations)

### File Support Priority
1. **High Priority**: Text, images, basic documents
2. **Medium Priority**: PDFs, archives, audio metadata
3. **Low Priority**: Video previews, specialized formats

### Performance Considerations
- Lazy loading of preview content
- Thumbnail caching system
- Configurable file size limits for previews
- Background loading to avoid UI blocking

## Testing Strategy
- Phase 1.5: UI toggle functionality and layout positioning
- Phase 2: Basic preview rendering for supported file types
- Phase 3: Selection event handling and file switching
- Phase 4: Advanced features and edge cases

## Future Enhancements
- Plugin system for custom preview providers
- Network file preview support
- Preview pane themes and customization
- Integration with external preview applications