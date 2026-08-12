# Task 1 report

Implemented the shared post-`EndPaint` table frame path.

- Added `TableFrameOwner::ParentAfterChild` and updated its resolver contract.
- Added `DrawDarkListViewFrame(HWND parent, HWND listView)` using the shared list palette and corner radius.
- Routed blacklist and running-picker table frames through the helper after `EndPaint`.
- Removed the blacklist-only inline `DrawRoundedPanel` path.
- Moved blacklist ListView font setup before `ApplyDarkListViewHeader`.

Verification:

- RED: `TextMagicInfoWindowLayoutTests` failed to compile because `ParentAfterChild` was absent.
- GREEN: built `TextMagicInfoWindowLayoutTests`, `TextMagicRunningApplicationTests`, and `TextMagic`.
- GREEN: focused CTests passed for `InfoWindowLayout` and `RunningApplication`.
- `rtk git diff --check` passed.
