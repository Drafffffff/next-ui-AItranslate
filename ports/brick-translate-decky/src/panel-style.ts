export const PANEL_STYLE = `
.bt-panel{--bt-text:#f1f0eb;--bt-muted:#aeb4b8;--bt-line:#ffffff16;--bt-accent:#c5da8c;--bt-surface:#24292d;color:var(--bt-text);font-family:inherit;font-size:14px;letter-spacing:0;min-width:0;padding-bottom:20px;container-type:inline-size}
.bt-panel *{box-sizing:border-box;letter-spacing:0}
.bt-panel svg{flex-shrink:0;width:17px;height:17px;vertical-align:middle}
.bt-panel .bt-masthead{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:0 16px 16px;font-size:12px;color:var(--bt-muted)}
.bt-model{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:600;max-width:70%}
.bt-ready{display:inline-flex;align-items:center;gap:5px;white-space:nowrap;color:var(--bt-accent)}
.bt-ready[data-ready=false]{color:#efbc78}
.bt-tabs{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:2px;margin:0 16px 16px;border-bottom:1px solid var(--bt-line)}
.bt-tab{height:42px;display:flex;align-items:center;justify-content:center;gap:5px;font-size:14px;color:var(--bt-muted);cursor:pointer;border-bottom:2px solid transparent;outline:none;white-space:nowrap;min-width:0}
.bt-tab[aria-selected=true]{color:var(--bt-text);border-bottom-color:var(--bt-accent)}
.bt-tab:focus-visible,.bt-tab.gpfocus{background:#c5da8c18;color:var(--bt-text);outline:2px solid var(--bt-accent);outline-offset:-2px;border-radius:3px}
.bt-panel .bt-section{margin:0 16px 18px!important;padding:0!important;border:0!important}
.bt-panel .bt-section-title{font-size:12px!important;font-weight:600!important;color:var(--bt-muted)!important;padding:0 0 7px!important;margin:0!important;line-height:20px!important;text-transform:none!important}
.bt-panel .bt-section-row{padding:0!important;margin:0!important;border:0!important;min-width:0}
.bt-panel .bt-section-row>div{margin-left:0!important;margin-right:0!important;min-width:0}
.bt-panel .bt-section-row input{font-size:14px!important;line-height:22px!important;min-width:0!important;max-width:100%!important;border-radius:4px!important}
.bt-panel button,.bt-panel [role=checkbox],.bt-panel [role=slider]{outline-offset:2px}
.bt-panel .bt-primary button{background:var(--bt-accent)!important;color:#202821!important;border-radius:5px!important;font-weight:650!important;min-height:48px!important;font-size:16px!important}
.bt-panel .bt-primary button:disabled{opacity:.5}
.bt-panel .bt-command{display:inline-flex;align-items:center;justify-content:center;gap:8px;max-width:100%;line-height:22px;white-space:normal}
.bt-panel .bt-secondary button{border-radius:4px!important;font-size:14px!important;min-height:40px!important}
.bt-status{display:flex;align-items:center;gap:7px;padding:10px 0 13px;color:var(--bt-muted);font-size:12px;line-height:18px;overflow-wrap:anywhere}
.bt-status[data-error=true]{color:#ffb3b0}
.bt-facts{display:flex;align-items:center;justify-content:space-between;gap:10px;border-top:1px solid var(--bt-line);padding:10px 0;font-size:12px;color:var(--bt-muted)}
.bt-facts strong{font-weight:500;color:var(--bt-text)}
.bt-config-status{display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:12px;line-height:20px;color:var(--bt-muted);padding:4px 0 10px}
.bt-config-status[data-ready=true] span:last-child{color:var(--bt-accent)}
.bt-config-status[data-ready=false] span:last-child{color:#efbc78}
.bt-config-status span:first-child{min-width:0;overflow-wrap:anywhere}
.bt-config-status span:last-child{flex-shrink:0;white-space:nowrap}
.bt-advanced{margin-top:8px;border-top:1px solid var(--bt-line);padding-top:4px}
.bt-disclosure{display:flex;justify-content:space-between;align-items:center;gap:8px;font-size:13px;line-height:20px;min-height:40px;padding:6px 3px;color:var(--bt-muted);cursor:pointer;outline:none;border-radius:3px}
.bt-disclosure:focus-visible,.bt-disclosure.gpfocus{outline:2px solid var(--bt-accent);color:var(--bt-text)}
.bt-receipt{display:grid;grid-template-columns:1fr 1fr;gap:0;border-top:1px solid var(--bt-line);margin:0 16px 14px}
.bt-receipt>div{padding:12px 0;display:flex;flex-direction:column;gap:5px;min-width:0}
.bt-receipt>div:nth-child(2n){padding-left:16px}
.bt-receipt span{font-size:12px;color:var(--bt-muted)}
.bt-receipt strong{font-size:18px;font-weight:600;line-height:24px;color:var(--bt-text)}
.bt-preview{margin:0 16px 12px;aspect-ratio:16/9;background:var(--bt-surface);overflow:hidden;border-radius:4px;position:relative}
.bt-preview img{display:block;width:100%;height:100%;object-fit:contain}
.bt-preview canvas{position:absolute;inset:0;width:100%;height:100%;object-fit:contain}
.bt-transcript{margin:0 16px;list-style:none;padding:0;counter-reset:translation}
.bt-transcript li{display:grid;grid-template-columns:24px 1fr;gap:10px;border-top:1px solid var(--bt-line);padding:12px 0;counter-increment:translation}
.bt-transcript li:before{content:counter(translation,decimal-leading-zero);font-size:11px;color:var(--bt-muted);line-height:23px}
.bt-transcript p{margin:0;font-size:15px;line-height:1.65;overflow-wrap:anywhere;color:var(--bt-text)}
.bt-transcript .bt-original{font-size:12px;color:var(--bt-muted);margin-top:6px}
.bt-entry{min-width:0;outline:none;border-radius:3px}
.bt-entry:focus-visible,.bt-entry.gpfocus{outline:2px solid var(--bt-accent);outline-offset:3px}
.bt-empty{margin:18px 16px 24px;padding:24px 0;border-top:1px solid var(--bt-line);border-bottom:1px solid var(--bt-line);display:flex;align-items:center;gap:10px;font-size:14px;color:var(--bt-muted)}
.bt-unsaved{margin:0 16px 16px;padding:12px 0;border-top:1px solid var(--bt-line)}
.bt-unsaved-label{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;color:#efbc78;font-size:12px}
.bt-panel .bt-footer{margin:16px 16px 0;padding-top:10px;border-top:1px solid var(--bt-line);display:flex;justify-content:space-between;gap:8px;color:var(--bt-muted);font-size:11px}
@container(max-width:300px){.bt-tab{gap:3px;font-size:13px}.bt-tab svg{width:14px;height:14px}.bt-tabs{margin-left:12px;margin-right:12px}.bt-panel .bt-section{margin-left:12px!important;margin-right:12px!important}}
@media(prefers-reduced-motion:no-preference){.bt-panel button,.bt-tab,.bt-disclosure{transition:background-color .14s ease-out,color .14s ease-out}.bt-status[data-busy=true] svg{animation:bt-spin 1.1s linear infinite}}
@keyframes bt-spin{to{transform:rotate(360deg)}}
`;
