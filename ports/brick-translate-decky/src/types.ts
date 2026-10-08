import {Item} from "./renderer";

export interface Settings {
  endpoint: string; model: string; language: string; image_width: number; timeout: number;
  hold_seconds: number; font_scale: number; hotkey: boolean; has_key: boolean; input_devices: number;
  pipeline: "ocr" | "vision"; ocr_endpoint: string; ocr_model: string; has_ocr_key: boolean; ocr_width: number;
}
export interface Frame {image: string; width: number; height: number; token: string}
export interface Result {
  items: Item[]; note: string; seconds: number; ocr_seconds?: number; ocr_lines?: number;
  translate_seconds?: number; cache_hit?: boolean;
}
export type View = {
  stage: "idle" | "capture" | "translate" | "done" | "error";
  frame?: Frame; result?: Result; error?: string; original: boolean;
};
