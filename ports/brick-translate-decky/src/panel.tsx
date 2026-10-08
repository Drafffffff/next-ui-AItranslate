import {ButtonItem, DropdownItem, Focusable, PanelSectionRow, SliderField, TextField, ToggleField} from "@decky/ui";
import {ReactNode, useEffect, useRef, useState} from "react";
import {BsArrowCounterclockwise, BsArrowRepeat, BsCheck2, BsCheckCircle, BsChevronDown, BsChevronRight,
  BsClockHistory, BsExclamationCircle, BsEye, BsGear, BsHddNetwork, BsKey, BsSliders2, BsTranslate, BsX} from "react-icons/bs";
import {drawTranslations} from "./renderer";
import {Settings, View} from "./types";
import {PANEL_STYLE} from "./panel-style";

export interface PanelBackend {
  getSettings(): Promise<Settings>; saveSettings(values: Partial<Settings>): Promise<Settings>;
  setKey(key: string, endpoint: string): Promise<boolean>; keyStatus(endpoint: string): Promise<boolean>;
  clearMemory(): Promise<void>;
}
interface Props {
  state: View; busy: boolean; backend: PanelBackend; onStart(): void; onDismiss(): void;
  onCompare(): void; onSettings(value: Settings): void; onError(error: unknown): void;
}
const providers = {
  deepseek: {endpoint: "https://api.deepseek.com/chat/completions", model: "deepseek-flash"},
  bailian: {endpoint: "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions", model: "qwen3-vl-plus"},
};
const providerFor = (endpoint: string) => endpoint === providers.deepseek.endpoint ? "deepseek" : endpoint === providers.bailian.endpoint ? "bailian" : "custom";
const Section = ({title, children}: {title?: string; children: ReactNode}) => <section className="bt-section">{title && <h2 className="bt-section-title">{title}</h2>}{children}</section>;
const Row = ({children, className = ""}: {children: ReactNode; className?: string}) => <div className={`bt-section-row ${className}`}><PanelSectionRow>{children}</PanelSectionRow></div>;
function Disclosure({label, open, onClick}: {label: string; open: boolean; onClick(): void}) {
  return <Focusable className="bt-disclosure" role="button" aria-expanded={open} onActivate={onClick} onClick={onClick}>
    <span>{label}</span>{open ? <BsChevronDown /> : <BsChevronRight />}
  </Focusable>;
}

function History({state, scale}: {state: View; scale: number}) {
  const [original, setOriginal] = useState(false);
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    if (canvas.current && state.result) drawTranslations(canvas.current, state.result.items, scale);
  }, [state.result, scale, original]);
  if (!state.result) return <div className="bt-empty"><BsClockHistory /> 暂无翻译记录</div>;
  const result = state.result;
  return <>
    <div className="bt-receipt">
      <div><span>译文</span><strong>{result.items.length} 段</strong></div>
      <div><span>总耗时</span><strong>{result.cache_hit ? "缓存命中" : `${result.seconds} 秒`}</strong></div>
      {result.ocr_seconds !== undefined && !result.cache_hit && <>
        <div><span>OCR 定位</span><strong>{result.ocr_seconds} 秒</strong></div>
        <div><span>文字翻译</span><strong>{result.translate_seconds ?? 0} 秒</strong></div>
      </>}
    </div>
    {state.frame && <>
      <div className="bt-preview">
        <img src={state.frame.image} alt="最近翻译的游戏画面" />
        {!original && <canvas ref={canvas} width={960} height={Math.round(960 * state.frame.height / state.frame.width)} />}
      </div>
      <Section><Row className="bt-secondary"><ButtonItem layout="below" onClick={() => setOriginal(!original)}><span className="bt-command"><BsEye />{original ? "显示译图" : "查看原图"}</span></ButtonItem></Row></Section>
    </>}
    {result.items.length ? <ol className="bt-transcript">{result.items.map((item, index) => <li key={index}><Focusable className="bt-entry" role="group" aria-label={`译文 ${index + 1}`} tabIndex={0}><p>{item.zh}</p>{original && <p className="bt-original">{item.orig}</p>}</Focusable></li>)}</ol> : <div className="bt-empty"><BsCheckCircle /> 未发现需要翻译的文字</div>}
  </>;
}

export function ControlPanel({state, busy, backend, onStart, onDismiss, onCompare, onSettings, onError}: Props) {
  const root = useRef<HTMLDivElement>(null);
  const [saved, setSaved] = useState<Settings>();
  const [draft, setDraft] = useState<Partial<Settings>>({});
  const [tab, setTab] = useState("translate");
  const [saving, setSaving] = useState(false);
  const [key, setKey] = useState(""), [ocrKey, setOcrKey] = useState("");
  const [editingKey, setEditingKey] = useState(false), [editingOcrKey, setEditingOcrKey] = useState(false);
  const [advanced, setAdvanced] = useState(false), [ocrAdvanced, setOcrAdvanced] = useState(false);
  const [custom, setCustom] = useState(false);
  const [credentials, setCredentials] = useState({endpoint: "", ready: false});
  const [ocrCredentials, setOcrCredentials] = useState({endpoint: "", ready: false});
  const [loadError, setLoadError] = useState(false);
  const [memoryCleared, setMemoryCleared] = useState(false), [clearing, setClearing] = useState(false);
  const config = saved ? {...saved, ...draft} : undefined;
  const dirty = Object.keys(draft).length > 0;
  const load = async () => {
    setLoadError(false);
    try {const value = await backend.getSettings(); setSaved(value); setAdvanced(providerFor(value.endpoint) === "custom"); onSettings(value);}
    catch (error) {setLoadError(true); onError(error);}
  };
  useEffect(() => {void load();}, []);
  useEffect(() => {
    const node = root.current;
    const order = ["translate", "display", "services", "history"];
    const focusedTab = (event: Event) => (event.target as HTMLElement)?.closest?.<HTMLElement>(".bt-tab");
    const focus = (event: Event) => {
      const target = focusedTab(event);
      if (target) setTab(target.id.replace("bt-tab-", ""));
    };
    const keydown = (event: KeyboardEvent) => {
      const target = focusedTab(event);
      if (!target) return;
      const current = order.indexOf(target.id.replace("bt-tab-", ""));
      const next = event.key === "ArrowRight" ? (current + 1) % 4 : event.key === "ArrowLeft" ? (current + 3) % 4 : event.key === "Home" ? 0 : event.key === "End" ? 3 : -1;
      if (next >= 0) {event.preventDefault(); setTab(order[next]); node?.querySelector<HTMLElement>(`#bt-tab-${order[next]}`)?.focus();}
    };
    node?.addEventListener("focusin", focus);
    node?.addEventListener("keydown", keydown, true);
    return () => {node?.removeEventListener("focusin", focus); node?.removeEventListener("keydown", keydown, true);};
  }, []);
  useEffect(() => {setMemoryCleared(false);}, [state.result]);
  useEffect(() => {
    let alive = true;
    const endpoint = config?.endpoint;
    if (endpoint) {
      setCredentials({endpoint, ready: endpoint === saved?.endpoint && !!saved?.has_key});
      if (endpoint.startsWith("https://")) backend.keyStatus(endpoint).then(ready => {if (alive) setCredentials({endpoint, ready});}).catch(() => {});
    }
    return () => {alive = false;};
  }, [config?.endpoint, saved?.has_key]);
  useEffect(() => {
    let alive = true;
    const endpoint = config?.ocr_endpoint;
    if (endpoint) {
      setOcrCredentials({endpoint, ready: endpoint === saved?.ocr_endpoint && !!saved?.has_ocr_key});
      if (endpoint.startsWith("https://")) backend.keyStatus(endpoint).then(ready => {if (alive) setOcrCredentials({endpoint, ready});}).catch(() => {});
    }
    return () => {alive = false;};
  }, [config?.ocr_endpoint, saved?.has_ocr_key]);
  const change = (values: Partial<Settings>) => setDraft(previous => {
    const next = {...previous, ...values};
    for (const name of Object.keys(next) as (keyof Settings)[]) if (next[name] === saved?.[name]) delete next[name];
    return next;
  });
  const commit = async () => {
    if (!config || saving) return false;
    setSaving(true);
    const submitted = draft;
    try {
      const value = await backend.saveSettings(submitted); setSaved(value);
      setDraft(previous => {
        const remaining = {...previous};
        for (const name of Object.keys(submitted) as (keyof Settings)[]) if (remaining[name] === submitted[name]) delete remaining[name];
        return remaining;
      });
      onSettings(value); return true;
    }
    catch (error) {onError(error); return false;} finally {setSaving(false);}
  };
  const saveKey = async (ocr: boolean) => {
    if (!config || saving) return;
    setSaving(true);
    const endpoint = ocr ? config.ocr_endpoint : config.endpoint;
    try {
      const ready = await backend.setKey(ocr ? ocrKey : key, endpoint);
      if (ocr) {setOcrKey(""); setEditingOcrKey(false); setOcrCredentials({endpoint, ready});} else {setKey(""); setEditingKey(false); setCredentials({endpoint, ready});}
      const value = await backend.getSettings(); setSaved(value); onSettings(value);
    } catch (error) {onError(error);} finally {setSaving(false);}
  };
  const ready = !!config && credentials.endpoint === config.endpoint && credentials.ready &&
    (config.pipeline !== "ocr" || ocrCredentials.endpoint === config.ocr_endpoint && ocrCredentials.ready);
  const status = state.stage === "error" ? state.error : state.stage === "capture" ? "正在抓取画面" : state.stage === "translate" ?
    config?.pipeline === "ocr" ? "正在定位并翻译" : "正在翻译" : state.result ?
      `${state.result.items.length} 段译文 · ${state.result.cache_hit ? "缓存命中" : `${state.result.seconds} 秒`}` :
      loadError ? "加载失败" : !config ? "加载中" : ready ? "已就绪" : "服务尚未配置";
  const provider = config ? providerFor(config.endpoint) : "deepseek";
  return <div ref={root} className="bt-panel" id="brick-translate-panel">
    <style>{PANEL_STYLE}</style>
    <div className="bt-masthead"><span className="bt-model">{provider === "deepseek" ? "DEEPSEEK FLASH" : provider === "bailian" ? "BAILIAN QWEN" : "CUSTOM API"}</span>
      <span className="bt-ready" data-ready={ready}>{ready ? <BsCheckCircle /> : <BsExclamationCircle />}{ready ? "已就绪" : "待配置"}</span>
    </div>
    <Focusable className="bt-tabs" role="tablist" aria-label="插件视图" flow-children="horizontal">
      {[{id: "translate", label: "翻译", icon: <BsTranslate />}, {id: "display", label: "显示", icon: <BsSliders2 />},
        {id: "services", label: "服务", icon: <BsHddNetwork />}, {id: "history", label: "记录", icon: <BsClockHistory />}].map(value =>
        <Focusable key={value.id} id={`bt-tab-${value.id}`} className="bt-tab" role="tab" aria-selected={tab === value.id} aria-controls="bt-tab-content"
          tabIndex={tab === value.id ? 0 : -1}
          onActivate={() => setTab(value.id)} onClick={() => setTab(value.id)}>{value.icon}{value.label}</Focusable>)}
    </Focusable>
    <div id="bt-tab-content" role="tabpanel" aria-labelledby={`bt-tab-${tab}`}>
      {tab === "translate" && <>
        <Section>
          <Row className="bt-primary"><ButtonItem layout="below" disabled={busy || saving || !ready} onClick={async () => {if (!dirty || await commit()) onStart();}}>
            <span className="bt-command"><BsTranslate />{busy ? "翻译中" : "翻译当前画面"}</span>
          </ButtonItem></Row>
          <div className="bt-status" role="status" data-error={state.stage === "error"} data-busy={busy}>
            {busy ? <BsArrowRepeat /> : state.stage === "error" ? <BsExclamationCircle /> : <BsCheck2 />}{status}
          </div>
          {state.frame && state.stage !== "idle" && <Row className="bt-secondary"><ButtonItem layout="below" onClick={onDismiss}><span className="bt-command"><BsX />关闭译文</span></ButtonItem></Row>}
          {state.result && state.stage === "done" && <Row className="bt-secondary"><ButtonItem layout="below" onClick={onCompare}><span className="bt-command"><BsEye />{state.original ? "显示译文" : "查看原图"}</span></ButtonItem></Row>}
          {(!ready || loadError) && <Row className="bt-secondary"><ButtonItem layout="below" onClick={() => loadError ? void load() : setTab("services")}><span className="bt-command"><BsGear />{loadError ? "重新加载" : "配置服务"}</span></ButtonItem></Row>}
        </Section>
        {config && <Section title="翻译选项">
          <Row><DropdownItem label="目标语言" rgOptions={["简体中文", "繁體中文", "English", "日本語", "한국어"].map(value => ({data: value, label: value}))} selectedOption={config.language} onChange={option => change({language: option.data})} /></Row>
          <Row><ToggleField label="OCR 定位" checked={config.pipeline === "ocr"} disabled={saving || busy} onChange={async enabled => {
            setSaving(true);
            try {const value = await backend.saveSettings({pipeline: enabled ? "ocr" : "vision"}); setSaved(value); onSettings(value);}
            catch (error) {onError(error);} finally {setSaving(false);}
          }} /></Row>
          <div className="bt-facts"><span>翻译链路</span><strong>{config.pipeline === "ocr" ? "Qwen OCR → 翻译" : "视觉直译"}</strong></div>
          <div className="bt-facts"><span>手柄</span><strong>{config.input_devices ? `${config.input_devices} 个已连接` : "未检测到"}</strong></div>
        </Section>}
      </>}
      {tab === "display" && config && <>
        <Section title="译文显示">
          <Row><SliderField label="字号" min={0.75} max={1.5} step={0.05} value={config.font_scale} showValue onChange={value => change({font_scale: value})} /></Row>
          <Row><SliderField label="译文停留" min={0} max={60} step={5} value={config.hold_seconds} showValue valueSuffix={config.hold_seconds === 0 ? " · 手动关闭" : " 秒"} onChange={value => change({hold_seconds: value})} /></Row>
        </Section>
        <Section title="画面采样">
          {config.pipeline === "ocr" ? <Row><DropdownItem label="OCR 送图宽度" rgOptions={[768, 960, 1280, 1920, 0].map(value => ({data: value, label: value ? `${value}px` : "原始分辨率"}))} selectedOption={config.ocr_width} onChange={option => change({ocr_width: option.data})} /></Row> :
            <Row><DropdownItem label="视觉送图宽度" rgOptions={[768, 960, 1280, 1920].map(value => ({data: value, label: `${value}px`}))} selectedOption={config.image_width} onChange={option => change({image_width: option.data})} /></Row>}
        </Section>
        <Section title="手柄操作"><Row><ToggleField label="L3 + R3 长按" checked={config.hotkey} onChange={value => change({hotkey: value})} /></Row></Section>
      </>}
      {tab === "services" && config && <>
        <Section title="翻译服务">
          <Row><DropdownItem label="服务商" rgOptions={[{data: "deepseek", label: "DeepSeek"}, {data: "bailian", label: "百炼 Qwen"}, {data: "custom", label: "自定义"}]}
            selectedOption={custom ? "custom" : provider} onChange={option => {
              setCustom(option.data === "custom"); setAdvanced(option.data === "custom"); setKey(""); setEditingKey(false);
              if (option.data !== "custom") change(providers[option.data as keyof typeof providers]);
            }} /></Row>
          <div className="bt-config-status" data-ready={credentials.ready}><span>{config.model}</span><span>{credentials.ready ? "Key 已保存" : "缺少 Key"}</span></div>
          {credentials.ready && <Disclosure label="更新 API Key" open={editingKey} onClick={() => {setEditingKey(!editingKey); setKey("");}} />}
          {(!credentials.ready || editingKey) && <>
            <Row><TextField label="API Key" value={key} bIsPassword onChange={event => setKey(event.target.value)} /></Row>
            {!!key && <Row className="bt-secondary"><ButtonItem layout="below" disabled={!key.trim() || saving} onClick={() => void saveKey(false)}><span className="bt-command"><BsKey />保存 Key</span></ButtonItem></Row>}
          </>}
          <div className="bt-advanced"><Disclosure label="接口与模型" open={advanced} onClick={() => setAdvanced(!advanced)} />
            {advanced && <>
              <Row><TextField label="接口地址" value={config.endpoint} onChange={event => {change({endpoint: event.target.value}); setKey("");}} /></Row>
              <Row><TextField label="翻译模型" value={config.model} onChange={event => change({model: event.target.value})} /></Row>
              <Row><SliderField label="请求超时" min={10} max={90} step={5} value={config.timeout} showValue valueSuffix=" 秒" onChange={value => change({timeout: value})} /></Row>
            </>}
          </div>
        </Section>
        <Section title="百炼 OCR">
          <div className="bt-config-status" data-ready={ocrCredentials.ready}><span>{config.pipeline === "ocr" ? "定位已开启" : "定位已关闭"}</span><span>{ocrCredentials.ready ? "Key 已保存" : "缺少 Key"}</span></div>
          {ocrCredentials.ready && <Disclosure label="更新 OCR Key" open={editingOcrKey} onClick={() => {setEditingOcrKey(!editingOcrKey); setOcrKey("");}} />}
          {(!ocrCredentials.ready || editingOcrKey) && <>
            <Row><TextField label="百炼 OCR Key" value={ocrKey} bIsPassword onChange={event => setOcrKey(event.target.value)} /></Row>
            {!!ocrKey && <Row className="bt-secondary"><ButtonItem layout="below" disabled={!ocrKey.trim() || saving} onClick={() => void saveKey(true)}><span className="bt-command"><BsKey />保存 OCR Key</span></ButtonItem></Row>}
          </>}
          <div className="bt-advanced"><Disclosure label="OCR 接口与模型" open={ocrAdvanced} onClick={() => setOcrAdvanced(!ocrAdvanced)} />
            {ocrAdvanced && <>
              <Row><TextField label="OCR 接口" value={config.ocr_endpoint} onChange={event => {change({ocr_endpoint: event.target.value}); setOcrKey("");}} /></Row>
              <Row><TextField label="OCR 模型" value={config.ocr_model} onChange={event => change({ocr_model: event.target.value})} /></Row>
            </>}
          </div>
        </Section>
      </>}
      {tab === "history" && <>
        <History state={state} scale={config?.font_scale ?? 1} />
        <Section><Row className="bt-secondary"><ButtonItem layout="below" disabled={clearing || memoryCleared} onClick={async () => {
          setClearing(true); try {await backend.clearMemory(); setMemoryCleared(true);} catch (error) {onError(error);} finally {setClearing(false);}
        }}><span className="bt-command">{memoryCleared ? <BsCheck2 /> : <BsArrowCounterclockwise />}{memoryCleared ? "记忆已清空" : "清空翻译记忆"}</span></ButtonItem></Row></Section>
      </>}
    </div>
    {dirty && <div className="bt-unsaved">
      <div className="bt-unsaved-label"><span>有未保存的更改</span><Focusable role="button" aria-label="撤销更改" title="撤销更改" onActivate={() => setDraft({})} onClick={() => setDraft({})}><BsArrowCounterclockwise /></Focusable></div>
      <Row className="bt-primary"><ButtonItem layout="below" disabled={saving || busy} onClick={() => void commit()}><span className="bt-command"><BsCheck2 />{saving ? "保存中" : "保存设置"}</span></ButtonItem></Row>
    </div>}
    <div className="bt-footer"><span>v0.1.0</span><span>{config?.pipeline === "ocr" ? "OCR + Translation" : "Visual Translation"}</span></div>
  </div>;
}
