import {readFile, mkdir} from "node:fs/promises";
const {chromium} = await import(process.env.BRICK_PLAYWRIGHT_MODULE);
const source = await readFile("out/renderer.js", "utf8");
const browser = await chromium.launch({headless: true, executablePath: process.env.BRICK_CHROME_EXECUTABLE});
try {
  const page = await browser.newPage();
  await mkdir("out", {recursive: true});
  for (const [width, height] of [[1280, 800], [390, 844]]) {
    await page.setViewportSize({width, height});
    await page.setContent('<body style="margin:0;background:#151b1f"><canvas id="canvas" width="1280" height="720" style="width:min(100vw,177.7778vh);aspect-ratio:16/9;display:block"></canvas></body>');
    const metrics = await page.evaluate(async code => {
      const mod = await import(URL.createObjectURL(new Blob([code], {type: "text/javascript"})));
      const canvas = document.querySelector("canvas");
      const items = [
        {box:[0.1,0.1,0.2,0.14],zh:"继续游戏",kind:"menu",color:"yellow",align:"left"},
        {box:[0.1,0.15,0.13,0.18],zh:"游戏时间",kind:"label",color:"white",align:"left"},
        {box:[0.1,0.2,0.2,0.23],zh:"DualSenseController",kind:"label",color:"cyan",align:"left"},
        {box:[0.2,0.65,0.7,0.75],zh:"你终于来了！我们必须在黎明之前穿过盐沼，别忘了带上那盏提灯。",kind:"dialogue",color:"white",align:"left"},
        {box:[0.3,0.7,0.5,0.75],zh:"这段字幕和另一段发生重叠，也必须完整可读。",kind:"dialogue",color:"green",align:"left"},
        {box:[0.75,0.15,0.9,0.18],zh:"继续",kind:"menu",color:"white",align:"left",source_height:0.025},
      ];
      const placed = mod.drawTranslations(canvas, items, 1);
      for (let i=0;i<placed.length;i++) for(let j=i+1;j<placed.length;j++) {
        const a=placed[i],b=placed[j];
        if(a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y) throw new Error("Translation boxes overlap");
      }
      if(placed.length!==items.length) throw new Error("Expected translations missing");
      for(const p of placed) if(p.x<0||p.y<0||p.x+p.w>canvas.width||p.y+p.h>canvas.height) throw new Error("Text outside canvas");
      if(placed.find(p=>p.item.kind==="menu").lines.length!==1) throw new Error("Short menu label stacked vertically");
      if(placed.find(p=>p.item.source_height).size>20) throw new Error("OCR text is larger than source line height");
      const data=canvas.getContext("2d").getImageData(0,0,1280,720).data;
      let painted=0;for(let i=3;i<data.length;i+=4) if(data[i])painted++;
      if(painted<1000) throw new Error("Translation canvas is blank");
      const rect=canvas.getBoundingClientRect();
      if(rect.right>innerWidth+1) throw new Error("Mobile canvas exceeds viewport");
      return {placed:placed.length,painted,displayWidth:rect.width,displayHeight:rect.height};
    }, source);
    await page.screenshot({path:`out/renderer-${width}.png`});
    console.log(JSON.stringify({width,height,...metrics}));
  }
} finally {await browser.close();}
