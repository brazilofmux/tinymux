# Lemuria Framework

The foundational framework Atlantis builds on. Provides text display,
nested view management, and tab/split UI components.

Repository: [OpenAtlantis/Lemuria](https://github.com/OpenAtlantis/Lemuria)

## Text Handling

### RDTextView (extends NSTextView)
High-performance text display for MUD output:

- **Auto-scroll:** Scrolls to end on append when at bottom
- **Buffer limit:** `_rdMaxBufferLines` prevents memory bloat
- **Tooltips:** Custom hover detection via `NSTrackingRectTag`
- **Find:** `searchForString:` with last-search tracking
- **Focus redirect:** `_rdRedirectFocusOnKeyTo` IBOutlet sends
  keystrokes to another view (input field)
- **Batch edits:** `setUp` / `commit` group multiple appends as
  one undo step

Key methods:
```objc
- (void)appendString:(NSAttributedString *)string
- (void)clearTextView
- (void)performScrollToEnd
- (void)setMaxLines:(int)maxLines
- (void)setTooltipDelegate:(id)delegate
```

### RDLayoutManager (extends NSLayoutManager)
Custom text layout subclass. Hook point for per-RDTextView layout
tweaks (likely monospace optimization).

### RDScrollView (extends NSScrollView)
Custom scroll behavior:

- `RDScroller` subclass with auto-scroll detection
- `scrollWheel:` override for smooth scrolling
- `autoScroll` property—auto-tail when at bottom
- `recalculateAutoScroll` on layout changes

## Nested View System (Spawns/Tabs)

The core UI framework for multi-window, multi-pane layouts.

### RDNestedViewDescriptor Protocol
Contract for any spawnable view:
```objc
// Hierarchy
- (BOOL)isFolder
- (NSArray *)subviewDescriptors

// Metadata
- (NSString *)viewUID
- (NSString *)viewPath
- (NSString *)viewName
- (float)viewWeight
- (NSImage *)viewIcon

// Content
- (NSView *)view

// Lifecycle
- (void)viewWasFocused
- (void)viewWasUnfocused
- (void)close

// Tree manipulation
- (void)addSubview:(id<RDNestedViewDescriptor>)subview
- (void)removeSubview:(id<RDNestedViewDescriptor>)subview
- (void)sortSubviews

// Activity
- (BOOL)isLive
- (BOOL)isLiveSelf
- (NSString *)closeInfoString
```

### RDNestedViewManager (singleton)
Central controller for all spawned views:

**State:**

- `_rdAllViews` — all spawned views
- `_rdActiveViews` — focused chain
- `_rdWindows` — per-window metadata
- `_rdDisplayStyle` — pluggable layout renderer

**View Management:**

- `addView:` / `removeView:` — spawn/close
- `selectView:` / `selectNextActiveView` — focus
- `viewByPath:` / `viewByUid:` — lookup
- `currentFocusedView` — active view

**Activity Tracking:**

- `hasActivity:` / `activityCount:` — unread lines per view
- `view:hasActivity:` — set activity flag
- `viewReceivedFocus:` — clear activity on focus

**Window Management:**

- `windowForUID:` — lookup
- `renameWindow:` — change title
- `removeWindow:` — close

**Drag and Drop:**

- `beginDraggingView:onEvent:` — start drag
- `isDragging` — state query
- `placeholderView:inWindow:` — temp view during drag

**Display Class:**

- `_rdDisplayStyle` — pluggable renderer (tabs, splits, etc.)
- `syncDisplayClass` / `setDisplayClass:` — swap renderer
- `RDNestedViewDisplay` protocol defines renderer interface

### RDNestedViewWindow
Window subclass representing a spawn window. Holds view tree + toolbar.

### RDNestedViewCollection
Container of RDNestedViewDescriptor objects.

### Tab Components

- `RDNestedTabBarView` — tab bar implementation
- `RDNestedTabView` — tab content area
- `RDTabViewItem` (extends NSTabViewItem)—tab with activity counter
  (`_rdActivityCount` shows unread count on label)

### Tree Components

- `RDNestedOutlineView` — tree view for nested hierarchy
- `RDNestedSourceView` — sidebar source list style
- `RDOutlineView` — base outline view
- `RDSourceListCell` — custom cell for source list appearance

### Split View

- `RBSplitView` / `RBSplitSubview` — splitter for pane division
  - Supports image-pattern backgrounds
  - Resize handles with customizable appearance

### PSMTabBarControl (vendored)
Third-party tab bar control (PSMTabBarControl by Positive Spin Media):

- Multiple styles: Aqua, Metal, Unified, Adium
- Overflow popup for many tabs
- Drag-to-reorder tabs
- Progress indicator per tab
- Rollover close buttons

## Chained List System

Scrollable list of collapsible items:

### RDChainedListView (extends NSView)

- `_rdItems` — list of items
- `_rdSpacing`, `_rdMargin` — layout params
- `_rdAutocollapse` — only one expanded at a time
- `addItem:` / `removeItem:` / `moveItem:toPosition:`
- `relayoutFromItem:` — recalculate after expand/collapse

### RDChainedListItem (extends NSView)
Container for one collapsible item:

- Header button (RDChainedListButton)
- Content area (RDChainedListItemContent protocol)
- Expand/collapse state

## Relevance for SwiftUI Port

Lemuria solves problems that SwiftUI handles natively:

| Lemuria Component | SwiftUI Equivalent |
|-------------------|--------------------|
| RDTextView | ScrollView + Text (with AttributedString) |
| RDScrollView auto-scroll | ScrollViewReader + onChange |
| RDNestedViewManager | NavigationSplitView + TabView |
| RDNestedTabBarView | TabView with.tabViewStyle |
| PSMTabBarControl | TabView (built-in) |
| RBSplitView | HSplitView / VSplitView |
| RDChainedListView | List with DisclosureGroup |
| RDSourceListCell | Label with.listRowStyle |

The spawn routing logic (pattern matching—tab assignment) would
need to be reimplemented, but the UI rendering is entirely replaced
by SwiftUI primitives.
