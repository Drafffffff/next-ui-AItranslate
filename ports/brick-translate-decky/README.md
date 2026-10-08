# Brick Translate for Decky

Independent Decky plugin for Bazzite/SteamOS Gamescope game mode, using Brick-style translation
memory, local Chinese line wrapping and outlined text. Default pipeline:
clean screenshot -> resized JPEG -> Bailian Qwen OCR line detection -> DeepSeek Flash text translation -> overlay.
OCR uses `qwen-vl-ocr-2025-11-20` and the native DashScope `advanced_recognition` task. Four-point
pixel coordinates are normalized against the OCR input dimensions, never estimated by DeepSeek.
Normalized coordinates map back to the original-resolution overlay without changing aspect ratio.
OCR input defaults to 1280px width; 768/960/1920px and original-resolution modes are selectable.
Translations reference OCR line IDs; paragraph boxes are the union of those source boxes.
Source line height controls overlay font size. Original pixels and original text are preserved.
The OCR Positioning toggle saves immediately. Turn it off for a single visual-translation request:
the configured model reads the screenshot directly and proposes approximate overlay boxes, without
calling Bailian OCR or requiring its key. OCR model/key/resolution settings are retained when disabled.
The toggle changes only the pipeline, preserving other saved settings and unsaved field edits.

Default model: `deepseek-flash`. Default endpoint: `https://api.deepseek.com/chat/completions`.
Thinking is disabled, just as in Brick. A provider selector also offers Bailian `qwen3-vl-plus`
and a custom HTTPS endpoint. Change Bailian's endpoint to match your key region/workspace if needed.
One manual request per activation; no background screenshot uploads.

Open Brick Translate in Decky, set your API key, and select Translate Current Screen.
Hold L3+R3 together for 0.7 seconds to translate. Once the translated result appears, any new
controller button or keyboard key press dismisses it; held buttons, key repeats and releases do not.
The L3/R3 buttons that started the request must be released before they can dismiss the result.
The input watcher is armed only for the completed result, not while capturing or translating.
The completed overlay is published before input is armed, so presses during the arm request
close it immediately. Button state and d-pad/trigger axes are also polled as a fallback
when Steam Input consumes an event stream.
Controls are observed without
grabbing or injecting input, so the game also receives these buttons. Set another Steam Input
mapping if the combination conflicts with your game. A normal Decky panel button is always available.

The overlay shows a frozen screenshot while translating and reading. It does NOT pause the game.
Close it to return to the running game. Hold duration 0 means manual dismissal.
Dense screens may move translations away from their original boxes to avoid overlap. OCR recognition
can still miss or misread stylized game fonts; coordinate availability does not guarantee accuracy.
Text that cannot fit without overlapping is available in the full translation list in Decky's panel.
No original-text erasure, background plates, continuous translation, or automatic play.

Per-game and per-language context/terminology is held in RAM for up to ten minutes, and can be cleared.
API keys are stored only on the device in Decky's plugin settings directory (`api-key-deepseek`,
`api-key-bailian`, or an endpoint-specific key file, mode 0600),
never returned to the frontend. Screenshot files are removed after capture; the latest frame remains
in RAM for OCR, usable for 120 seconds and replaced by the next capture. No screenshot archive is kept.
OCR normally sends a JPEG at the configured width; original-resolution mode sends the captured PNG
to Bailian (up to 10 MiB). DeepSeek receives only detected text and
normalized boxes. Both services require their own keys. Region/workspace endpoints must match keys.
Up to four identical-frame results are cached in RAM for 60 seconds, scoped by game, language and
settings. Clear Translation Memory also clears this cache. Cache hits do not call either model.

## Latency

The original full-4K OCR pipeline measured 20.8-22.9 seconds. The send-width setting previously affected
only visual translation; OCR now actually resizes and uses JPEG. Panel timing separates OCR and translation.
Same-frame tests on this network measured the dated Qwen model at about 6.4 seconds for 1280px OCR,
8.8 seconds for 1920px OCR, and 12.2 seconds end-to-end at 960px. Reducing to 768px did not help
(13.3 seconds), and the newer `qwen3.5-ocr` varied from 11.8 to 17.4 seconds end-to-end at 1280px.
These are individual requests, not percentile measurements or an accuracy benchmark. Lower resolutions
can lose small text or introduce false detections. The default model remains the dated Qwen model.
An optimized live device request measured 13.8 seconds (11.2 seconds OCR, 2.6 seconds DeepSeek).
These backend timings exclude the menu-close delay, capture, resizing and rendering.
Cloud OCR plus cloud translation has not achieved a five-second uncached budget. Identical-frame cache
hits avoid model calls, but do not make the first request faster. Local OCR is a candidate for further
latency work; it is not implemented or bundled here.

## Panel

Four controller-focusable views replace the long settings form:

- Translation: request action, readiness, target language and immediately saved OCR toggle.
- Display: font scale, reading duration, active pipeline's input size, and controller activation.
- Services: provider selection, endpoint-specific credentials and collapsible model/endpoint controls.
- History: latest screenshot/translation preview, original comparison, timings and focusable transcript.

Already-provisioned keys stay hidden behind Update Key controls and are never read back. Model/endpoint
edits remain drafts across view changes; saving or translating commits them. The unsaved-change action
can discard drafts. The OCR switch only saves its own pipeline setting, preserving other drafts.
Readiness means a credential is saved, not a successful network health check.
Styling is scoped to the plugin panel and uses Steam/Decky's native buttons, fields and focus navigation.
`tools/verify-panel.mjs` checks all views at 320px/400px, draft isolation and keyboard tab navigation on
the actual device. `tools/verify-controls.mjs` checks immediate OCR switching and DOM-only key dismissal.

## Build

```sh
npm ci
npm run check
npm run build
npm run package
npm test
```

Install the resulting `out/Brick.Translate-0.1.0.zip` through Decky's developer ZIP installer.
Uses Decky API v1, Linux evdev, GStreamer `pipewiresrc`, `videoconvert`, `pngenc`, system curl, Python standard library,
and Steam's UI composition hook. No root plugin flag or system package installation is required.

## Device Verification (2026-10-07)

Installed on `bazzite.local`, Bazzite Deck 43, Decky Loader 3.2.9. Backend and frontend loaded.
PipeWire captured real 3840x2160 frames. The OCR-first pipeline returned 8 translations on a real
Caves of Qud gameplay frame in 22.9 seconds. The earlier visual-only pipeline returned 20 translations
in 4.7 seconds on a different frame; these are not a controlled accuracy comparison.
Canvas pixel checks confirmed painted text and
matching screenshot/canvas bounds. Local desktop/mobile renderer checks cover short labels, long Latin
words, paragraph wrapping, bounds, and non-overlapping placements.

An external controller is detected through evdev; physical L3+R3 activation is not yet tested.
The overlay does not freeze the underlying game's simulation. PipeWire capture on this device exposes
the game base plane, not Steam's translated overlay. The CEF UI canvas was verified; visibility on the
physical display still needs user confirmation. Only this plugin's own overlay and QAM are hidden
before capture. Screenshots are sent to the selected model service only on explicit activation.

`tools/provision-brick-key.py` copies the existing DeepSeek key from Brick's public configuration to
this plugin's private settings without printing it. `tools/provision-key.mjs` can provision the existing
local `DASHSCOPE_API_KEY` as the Bailian fallback, without printing it. These are optional setup tools.
`tools/cdp.mjs` and verification tools use Steam's existing debugger through an SSH tunnel; normal
plugin operation does not need the tunnel or debugger.

## Steam Deck input fix (2026-10-08)

Fixed the completed-result dismissal race and added evdev state polling. Backend regression
tests cover held activation buttons, new presses without events, repeats, releases and d-pad
input. A frontend regression checks a press arriving during the arm RPC. On SteamOS with Decky
3.2.10, a temporary keyboard verified the backend dismissal event and early overlay closure;
the temporary device was removed afterwards. Physical PS5 button confirmation is still pending.
The overlay continues to show a screenshot; it does not pause the game's simulation.
