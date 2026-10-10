/** Keep original project/model text; suppress emoji only at the display boundary.
 * Explicit ranges also work on QuickJS without Unicode property escape support. */
export function emojiCodepoint(cp:number){
 return cp>=0x1f000&&cp<=0x1faff||cp>=0x2600&&cp<=0x27bf||
  cp>=0xfe00&&cp<=0xfe0f||cp===0x200d||cp===0x20e3||cp===0x00a9||cp===0x00ae||
  cp===0x203c||cp===0x2049||cp===0x2122||cp===0x2139||cp===0x3030||cp===0x303d||cp===0x3297||cp===0x3299||
  cp>=0xe0020&&cp<=0xe007f;
}
// Native RegExp avoids per-codepoint arrays over every long dialogue body.
const emoji=/[\u2600-\u27bf\ufe00-\ufe0f\u200d\u20e3\u00a9\u00ae\u203c\u2049\u2122\u2139\u3030\u303d\u3297\u3299]|[\ud83c\ud83d][\udc00-\udfff]|\ud83e[\udc00-\udeff]|\udb40[\udc20-\udc7f]/g;
export function displayText(text:string){return text.replace(emoji,'');}
