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

### Phase 1.5 ✅ **COMPLETED**
**UI Toggle Implementation**
- [x] Find appropriate UI location for preview pane toggle
  - ✅ Implemented in View menu with F4 keyboard shortcut
- [x] Implement menu item or toolbar button for preview pane toggle
  - ✅ "_Preview Pane" menu item added to View menu
- [x] Connect toggle UI to existing show/hide API functions
  - ✅ `action_show_hide_preview_pane_callback` connects menu to API
- [x] Test preview pane visibility and positioning in window layout
  - ✅ Preview pane toggles correctly in `content_preview_paned` layout
- [x] Verify dummy "No file selected" label appears correctly
  - ✅ Default label displays when no file is selected

### Phase 2 ✅ **COMPLETED**
**File Type Detection and Basic Preview**
- [x] Implement file type detection system
  - ✅ MIME type-based detection for text and image files
- [x] Add basic preview renderers for common file types:
  - ✅ Text files (.txt, .md, .log, JSON, XML, scripts)
  - ✅ Image files (.jpg, .png, .gif, etc.) with scaling
  - ❌ Basic document formats (deferred to Phase 4)
- [x] Create preview content management system
  - ✅ State management for loading, error, and content display
- [x] Handle file loading and error states
  - ✅ Proper error handling with user-friendly messages
- [x] Add loading indicators and error messages
  - ✅ Loading state, error messages, and file size limits

### Phase 3 ✅ **COMPLETED**
**Selection Handling and File Content Display**
- [x] Connect preview pane to file selection events
  - ✅ Connected view `selection-changed` signal to window callback
- [x] Implement `nemo_preview_pane_set_file()` function
  - ✅ Full implementation with file type detection and rendering
- [x] Add file metadata display (size, dates, permissions)
  - ✅ Shows filename, file size, MIME type, and modification date
- [x] Handle multiple file selection states
  - ✅ Single file preview, multiple selection clears preview
- [x] Optimize preview loading for performance
  - ✅ Only updates when preview pane is visible
- [x] Add preview refresh mechanisms
  - ✅ Auto-refreshes when pane is shown or selection changes

### Phase 3.5 ✅ **COMPLETED**
**UI Polish and User Experience**
- [x] Fix preview pane initial width - make it wider when first shown
  - ✅ Implemented intelligent initial positioning with 350px default width
  - ✅ Preview pane now opens with proper width instead of being shallow
- [x] Make preview images responsive to pane width
  - ✅ Modified create_image_preview() to accept available_width parameter
  - ✅ Added get_available_preview_width() helper function
  - ✅ Images now scale dynamically from 200px to available pane width
- [x] Implement preview pane width persistence
  - ⏸️ **DEFERRED**: Requires system-wide schema installation
  - 💾 Implementation saved in `preview-pane-persistence.diff`
  - 🚫 Temporarily rolled back to avoid crashes with missing schema key

### Phase 4 📋 **IN PROGRESS**
**Advanced Preview Features**
- [x] Implement thumbnail generation and caching
  - ✅ Integrated with Nemo's existing thumbnail system using `nemo_file_get_icon()`
  - ✅ Added `NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS` support for automatic thumbnail generation
  - ✅ Enhanced image preview to use cached thumbnails when available
  - ✅ Added support for video file thumbnails
  - ✅ Intelligent fallback to direct image loading if thumbnail generation fails
- [x] Dynamic preview content resizing
  - ✅ **Immediate Response**: 1px width changes trigger instant visual feedback
  - ✅ **Dual-Phase Rendering**: Immediate rescaling + async high-quality re-render
  - ✅ **Smart Cancellation**: Abandons async renders when user resizes again
  - ✅ **Non-Blocking UI**: Async rendering prevents UI freezing during complex operations
  - ✅ **Comprehensive Cleanup**: Proper disposal of async operations and resources
  - ✅ **Professional UX**: Seamless resize experience with immediate + perfected rendering
- [x] Add support for more file types:
  - ✅ PDF documents - First page thumbnail preview using thumbnail system
  - [ ] Audio files (metadata display)  
  - ✅ Video files - Thumbnail preview using thumbnail system
  - Archive files (content listing)
- [ ] Add preview customization options
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

---

# CRITICAL: Preview Pane Resize Performance Investigation

## Problem Statement
Preview pane resizing to smaller widths is sluggish and does not provide perfect cursor tracking. The resize operation should be instant and follow the cursor precisely.

## Investigation Findings

### Root Cause Analysis Attempts

#### Attempt 1: Motion Event Processing
- **Hypothesis**: Motion events not being processed quickly enough
- **Implementation**: Added paned motion tracking with microsecond precision
- **Result**: FAILED - Motion events were captured but still had 300ms-1200ms gaps
- **Learning**: The issue is NOT in motion event capture frequency

#### Attempt 2: GTK Widget Allocation Conflicts  
- **Hypothesis**: GTK paned widget overriding direct widget allocations
- **Implementation**: Tried to bypass GTK by directly setting paned positions
- **Result**: FAILED - Created circular feedback loops causing even more delays
- **Learning**: Fighting GTK's natural allocation system creates performance problems

#### Attempt 3: UI Processing Loop Optimization
- **Hypothesis**: `gtk_main_iteration_do()` loops causing delays
- **Implementation**: Removed expensive UI processing during resize
- **Result**: PARTIAL - Reduced some overhead but fundamental issue remains
- **Learning**: Removing processing loops helps but doesn't solve the core problem

### Critical Debug Data Captured

From comprehensive debug logs, the issue pattern shows:
- Motion events have microsecond precision when captured
- Size-allocate events have massive 300ms-1200ms gaps between calls
- During shrinking operations: consistent 8-pixel steps with huge time delays
- Time gaps between resize events:
  - 19:18:34.250 → 19:18:34.584 = **334ms gap**  
  - 19:18:34.584 → 19:18:35.767 = **1183ms gap**
  - 19:18:35.767 → 19:18:36.150 = **383ms gap**

### Key Insight: The Real Problem
The issue is NOT in our resize handling code. The massive delays occur BEFORE our size-allocate handler is even called. This suggests the problem is in GTK's internal paned widget resize mechanism itself.

## Next Investigation Steps

### Required: Cursor Position vs Preview Pane Position Tracking
Need to implement comprehensive tracking that compares:
1. **Expected cursor position** during drag operations
2. **Actual preview pane position** after GTK processes the resize
3. **Time delta** between cursor movement and visual update
4. **Position deviation** (how far off the preview pane is from cursor expectations)

### Implementation Requirements
- Track cursor position during every motion event
- Calculate expected preview pane width based on cursor position
- Compare actual allocated width vs expected width
- Measure time between cursor movement and visual update
- Log position deviation in pixels

### Debug Output Format
```
CURSOR_TRACKING: cursor_x=123.4, expected_width=456, actual_width=450, deviation=6px, time_delta=234ms
```

## Previous Failed Approaches - DO NOT RETRY

1. ❌ **Calling `gtk_paned_set_position()` during motion events** - Creates circular feedback
2. ❌ **Using `gtk_main_iteration_do()` loops in size-allocate** - Blocks event processing  
3. ❌ **Direct widget allocation manipulation** - GTK overrides it anyway
4. ❌ **Complex drag state management** - Adds overhead without solving core issue

## Current Status
- Issue remains unresolved
- Need cursor position tracking implementation
- Must identify if GTK paned widget itself has inherent performance limitations
- May need alternative approach if GTK paned widget cannot provide smooth resize performance