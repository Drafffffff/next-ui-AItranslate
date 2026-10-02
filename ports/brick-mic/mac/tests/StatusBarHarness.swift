import AppKit

@main enum StatusBarHarness {
    static func main() throws {
        let offline=MicStatusBarPresentation(connected:false,recording:true,processing:true,percent:88,charging:true)
        precondition(offline.phase == .disconnected && offline.title.isEmpty && offline.percent==nil && !offline.charging)
        let idle=MicStatusBarPresentation(connected:true,recording:false,processing:false,percent:88,charging:false)
        let recording=MicStatusBarPresentation(connected:true,recording:true,processing:false,percent:88,charging:true)
        let processing=MicStatusBarPresentation(connected:true,recording:false,processing:true,percent:88,charging:true)
        precondition(idle.itemWidth==recording.itemWidth && recording.itemWidth==processing.itemWidth)
        precondition(idle.phase == .ready && recording.phase == .recording && processing.phase == .processing)
        for level in [0,9,99,100] {
            let p=MicStatusBarPresentation(connected:true,recording:false,processing:false,percent:level,charging:false)
            precondition(p.title==String(level)+"%")
        }
        for level in [nil,-1,101] as [Int?] {
            let p=MicStatusBarPresentation(connected:true,recording:false,processing:false,percent:level,charging:false)
            precondition(p.percent==nil && p.title.contains("—%") && p.accessibilityLabel.contains("未知"))
        }
        precondition(recording.accessibilityLabel.contains("正在说话") && recording.accessibilityLabel.contains("正在充电"))
        let examples=[offline,idle,recording,processing,
                      MicStatusBarPresentation(connected:true,recording:false,processing:false,percent:9,charging:false),
                      MicStatusBarPresentation(connected:true,recording:false,processing:false,percent:nil,charging:false)]
        let output=URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true)
        try FileManager.default.createDirectory(at:output,withIntermediateDirectories:true)
        for (index,p) in examples.enumerated() {
            let image=MicStatusBarIcons.image(p)
            precondition(image.isTemplate && image.size.height==18 && image.size.width==(index==0 ? 30:55))
            let rep=NSBitmapImageRep(data:image.tiffRepresentation!)!
            try rep.representation(using:.png,properties:[:])!.write(to:output.appendingPathComponent("state-\(index).png"))
        }
        // Render the same vector masks at 2× resolution as the system template tint.
        let rep=NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:1560,pixelsHigh:220,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!
        NSGraphicsContext.saveGraphicsState();defer{NSGraphicsContext.restoreGraphicsState()}
        NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:rep)
        let labels=["未连接","已连接","正在说话","正在识别","低电量","电量未知"]
        for dark in [false,true] {
            let y:CGFloat=dark ? 0:110
            (dark ? NSColor(calibratedWhite:0.12,alpha:1):NSColor(calibratedWhite:0.94,alpha:1)).setFill()
            NSRect(x:0,y:y,width:1560,height:110).fill()
            for (i,p) in examples.enumerated() {
                let origin=CGPoint(x:CGFloat(i)*260+24,y:y+26)
                let image=MicStatusBarIcons.image(p)
                let context=NSGraphicsContext.current!.cgContext
                context.saveGState()
                let rect=CGRect(x:origin.x,y:origin.y,width:image.size.width*2,height:36)
                context.beginTransparencyLayer(auxiliaryInfo:nil)
                image.draw(in:rect,from:.zero,operation:.sourceOver,fraction:1)
                context.setBlendMode(.sourceIn);context.setFillColor((dark ? NSColor.white:NSColor.black).cgColor);context.fill(rect)
                context.endTransparencyLayer();context.restoreGState()
                let color=dark ? NSColor.white:NSColor.black
                let title=p.title as NSString
                title.draw(at:CGPoint(x:origin.x+image.size.width*2+4,y:origin.y+2),withAttributes:[.font:NSFont.monospacedDigitSystemFont(ofSize:24,weight:.medium),.foregroundColor:color])
                (labels[i] as NSString).draw(at:CGPoint(x:origin.x,y:y+76),withAttributes:[.font:NSFont.systemFont(ofSize:18),.foregroundColor:color])
            }
        }
        try rep.representation(using:.png,properties:[:])!.write(to:output.appendingPathComponent("status-bar-states.png"))
        print("PASS: connection/recording/processing priority, real battery/unknown/charging, fixed item width, accessible labels and light/dark template icons")
    }
}
