Upstream: https://github.com/jeremy-prt/bloub
Commit: b4bb3c1b5f93c7b87a2e8d620f667c4093d97749
License: MIT, Copyright (c) 2026 Jérémy Perret

The eight animation/geometry files are unchanged. eyefit.ts is a PocketJS adapter for the default null silhouette; it returns the same zero offset as upstream and skips the expensive unused custom-shape calibration. The original solver is retained in eyefit.upstream.ts. PocketJS adaptation lives in avatar.ts and workshop-avatar.c. Vue, DOM, external clock, pointer tracking and the editor are not included. The renderer uses the system Main/Accent2/Background palette instead of upstream rainbow colours.
