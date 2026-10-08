import {Navigation, Router, staticClasses, findModuleChild} from "@decky/ui";
import {addEventListener, removeEventListener, callable, definePlugin, routerHook, toaster} from "@decky/api";
import {useEffect, useRef, useState} from "react";
import {BsTranslate} from "react-icons/bs";
import {drawTranslations} from "./renderer";
import {ControlPanel} from "./panel";
import {Frame, Result, Settings, View} from "./types";

const getSettings = callable<[], Settings>("get_settings");
const saveSettings = callable<[Partial<Settings>], Settings>("save_settings");
const setKey = callable<[string, string], boolean>("set_api_key");
const keyStatus = callable<[string], boolean>("get_key_status");
const capture = callable<[], Frame>("capture");
const translate = callable<[string, string, string, string], Result>("translate");
const clearMemory = callable<[], void>("clear_memory");
const armDismiss = callable<[boolean], void>("arm_dismiss");
const panelBackend = {getSettings, saveSettings, setKey, keyStatus, clearMemory};

let view: View = {stage: "idle", original: false};
let settings: Settings | undefined;
let generation = 0, inFlight = false, alive = true;
let hideTimer: ReturnType<typeof setTimeout> | undefined;
const subscribers = new Set<() => void>();
const update = (next: View) => {view = next; subscribers.forEach(listener => listener());};
function useView() {
  const [state, setState] = useState(view);
  useEffect(() => {const listener = () => setState(view); subscribers.add(listener); return () => {subscribers.delete(listener);};}, []);
  return state;
}
function dismiss() {
  generation++; clearTimeout(hideTimer);
  void armDismiss(false).catch(() => {});
  update({...view, stage: "idle", original: false});
}
function notify(error: unknown) {
  toaster.toast({title: "Brick Translate", body: errorText(error)});
}
function errorText(error: unknown): string {
  if (error instanceof Error) {
    if (error.message) return error.message;
    const trace = (error as Error & {pythonTraceback?: string}).pythonTraceback;
    return trace?.trim().split("\n").pop()?.replace(/^\w+: /, "") ?? "翻译请求失败";
  }
  return String(error);
}

async function modelImage(frame: Frame, width: number, jpeg = false): Promise<string> {
  const image = new Image(); image.src = frame.image;
  await image.decode();
  const canvas = document.createElement("canvas");
  canvas.width = Math.min(width, image.naturalWidth);
  canvas.height = Math.round(image.naturalHeight * canvas.width / image.naturalWidth);
  canvas.getContext("2d")!.drawImage(image, 0, 0, canvas.width, canvas.height);
  const png = jpeg ? "" : canvas.toDataURL("image/png");
  const encoded = png && png.length <= 3 * 1024 * 1024 ? png : canvas.toDataURL("image/jpeg", jpeg ? 0.88 : 0.9);
  if (encoded.length > 3 * 1024 * 1024) throw new Error("截图过大，请降低送图宽度");
  return encoded;
}

async function start() {
  if (inFlight) return;
  inFlight = true;
  const current = ++generation;
  clearTimeout(hideTimer);
  try {
    settings = await getSettings();
    if (!alive || current !== generation) return;
    if (!settings.has_key) throw new Error("请先设置当前服务的 API Key");
    if (settings.pipeline === "ocr" && !settings.has_ocr_key) throw new Error("请先设置 OCR 服务的百炼 API Key");
    Navigation.CloseSideMenus();
    update({stage: "capture", original: false});
    // Wait until the QAM and our previous overlay leave the compositor.
    await new Promise(resolve => setTimeout(resolve, 500));
    if (!alive || current !== generation) return;
    const frame = await capture();
    if (!alive || current !== generation) return;
    const image = await modelImage(frame, settings.pipeline === "ocr" ? settings.ocr_width || settings.image_width : settings.image_width, settings.pipeline === "ocr");
    update({stage: "translate", frame, original: false});
    const app = Router.MainRunningApp;
    const result = await translate(image, app ? String(app.appid) : "", app?.display_name ?? "", frame.token);
    if (!alive || current !== generation) return;
    // A key can arrive before the arm RPC returns. Make the result dismissible first.
    update({stage: "done", frame, result, original: false});
    await armDismiss(true);
    if (!alive || current !== generation) {void armDismiss(false).catch(() => {}); return;}
    if (settings.hold_seconds > 0) hideTimer = setTimeout(dismiss, settings.hold_seconds * 1000);
  } catch (error) {
    if (alive && current === generation) {
      update({...view, stage: "error", error: errorText(error)});
      hideTimer = setTimeout(dismiss, 5000);
      notify(error);
    }
  } finally {inFlight = false; if (alive) update({...view});}
}

// Steam requires a minimum composition request for UI to appear above games.
const useComposition = findModuleChild((module: Record<string, unknown>) => {
  if (!module || typeof module !== "object") return undefined;
  return Object.values(module).find(value => typeof value === "function" &&
    ["AddMinimumCompositionStateRequest", "ChangeMinimumCompositionStateRequest", "RemoveMinimumCompositionStateRequest"].every(name => String(value).includes(name)) &&
    !String(value).includes("m_mapCompositionStateRequests"));
}) as ((level: number) => void) | undefined;
function Composition() {if (useComposition) useComposition(1); return null;}

function Overlay() {
  const state = useView(), canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    if (state.stage !== "done") return;
    const target = canvas.current?.ownerDocument.defaultView;
    const keydown = (event: KeyboardEvent) => {if (!event.repeat && view.stage === "done") dismiss();};
    target?.addEventListener("keydown", keydown, true);
    return () => target?.removeEventListener("keydown", keydown, true);
  }, [state.stage]);
  useEffect(() => {
    if (canvas.current && state.result && !state.original) drawTranslations(canvas.current, state.result.items, settings?.font_scale ?? 1);
  }, [state]);
  const frame = state.frame;
  if (!frame || state.stage === "idle" || state.stage === "capture") return null;
  return <><Composition /><div id="brick-translate-overlay" style={{position: "fixed", inset: 0, zIndex: 9999, pointerEvents: "none", background: "#111"}}>
    <div style={{position: "absolute", inset: 0, display: "flex", alignItems: "center", justifyContent: "center"}}>
      <div style={{position: "relative", width: `min(100vw, ${100 * frame.width / frame.height}vh)`, aspectRatio: `${frame.width}/${frame.height}`}}>
        <img src={frame.image} alt="" style={{display: "block", width: "100%", height: "100%"}} />
        {!state.original && <canvas ref={canvas} width={frame.width} height={frame.height} style={{position: "absolute", inset: 0, width: "100%", height: "100%"}} />}
      </div>
    </div>
    {state.stage !== "done" && <div role="status" style={{position: "absolute", top: 18, left: 18, maxWidth: "calc(100% - 36px)", padding: "8px 12px", background: "#15191eee", color: "#f4f6f8", fontSize: 16, borderRadius: 4}}>
      {state.stage === "translate" ? settings?.pipeline === "ocr" ? "正在定位并翻译…" : "正在翻译…" : state.error}
    </div>}
  </div></>;
}

function Content() {
  const state = useView();
  return <ControlPanel state={state} busy={inFlight || state.stage === "capture" || state.stage === "translate"}
    backend={panelBackend} onStart={() => void start()} onDismiss={dismiss} onCompare={() => update({...view, original: !view.original})}
    onSettings={value => {settings = value;}} onError={notify} />;
}

export default definePlugin(() => {
  alive = true;
  routerHook.addGlobalComponent("BrickTranslateOverlay", Overlay);
  const listener = addEventListener<[]>("toggle_translation", () => {
    if (view.stage !== "idle") dismiss(); else void start();
  });
  const dismissListener = addEventListener<[]>("dismiss_translation", () => {if (view.stage === "done") dismiss();});
  getSettings().then(value => {settings = value;}).catch(notify);
  return {
    name: "Brick Translate", titleView: <div className={staticClasses.Title}>Brick Translate</div>,
    icon: <BsTranslate />, content: <Content />,
    onDismount() {alive = false; dismiss(); removeEventListener("toggle_translation", listener); removeEventListener("dismiss_translation", dismissListener); routerHook.removeGlobalComponent("BrickTranslateOverlay"); subscribers.clear();},
  };
});
