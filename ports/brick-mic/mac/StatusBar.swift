import AppKit

// Presentation contains no app/window queries or polling. A disconnected peer's
// last battery reading is never presented as its current remaining charge.
struct MicStatusBarPresentation:Equatable {
    enum Phase:CaseIterable {case disconnected,ready,recording,processing}
    let phase:Phase
    let percent:Int?
    let charging:Bool
    init(connected:Bool,recording:Bool,processing:Bool,percent:Int?,charging:Bool) {
        phase = !connected ? .disconnected:recording ? .recording:processing ? .processing:.ready
        self.percent = connected ? percent.flatMap{(0...100).contains($0) ? $0:nil}:nil
        self.charging = connected && charging
    }
    var stateLabel:String {
        switch phase {case .disconnected:return "未连接掌机";case .ready:return "已连接掌机";case .recording:return "正在说话";case .processing:return "正在识别"}
    }
    var itemWidth:CGFloat {phase == .disconnected ? 40:104}
    var title:String {
        guard phase != .disconnected else{return ""}
        // The item width already reserves space. Padding here would split the
        // battery and its value into separate visual groups.
        return percent.map{String($0)+"%"} ?? "—%"
    }
    var accessibilityLabel:String {
        let battery=phase == .disconnected ? "":"，掌机电量"+(percent.map{String($0)+"%"} ?? "未知")+(charging ? "，正在充电":"")
        return "Brick Mic，"+stateLabel+battery
    }
}

enum MicStatusBarIcons {
    // Lucide icons selected through Iconify. Source SVGs and license are vendored
    // in assets/lucide; these paths use the same 24×24 coordinate system.
    static func image(_ presentation:MicStatusBarPresentation)->NSImage {
        let connected=presentation.phase != .disconnected
        let image=NSImage(size:NSSize(width:connected ? 55:30,height:18),flipped:false){_ in
            NSColor.black.setStroke();NSColor.black.setFill()
            MicBrand.drawMenuMark(in:NSRect(x:0,y:0,width:18,height:18))
            // Auxiliary Lucide badges change; the Brick Mic mark never does.
            drawIcon(at:NSPoint(x:18,y:-3),size:12) {
                switch presentation.phase {
                case .disconnected:
                    polyline([(18,6),(6,18)]);polyline([(6,6),(18,18)])
                case .ready:
                    stroke(NSBezierPath(ovalIn:NSRect(x:2,y:2,width:20,height:20)))
                    polyline([(16,9),(10.5,14.5),(8,12)])
                case .recording:
                    let lines:[[(CGFloat,CGFloat)]]=[[(2,10),(2,13)],[(6,6),(6,17)],[(10,3),(10,21)],[(14,8),(14,15)],[(18,5),(18,18)],[(22,10),(22,13)]]
                    for line in lines{polyline(line)}
                case .processing:
                    for x:CGFloat in [5,12,19]{stroke(NSBezierPath(ovalIn:NSRect(x:x-1,y:11,width:2,height:2)))}
                }
            }
            if connected {
                drawIcon(at:NSPoint(x:36,y:0),size:19) {
                    polyline([(22,14),(22,10)])
                    if presentation.charging {
                        // Lucide battery-charging's open outline keeps its bolt legible.
                        polyline([(11,7),(8,12),(12,12),(9,17)])
                        let right=NSBezierPath();right.move(to:NSPoint(x:14.856,y:6));right.line(to:NSPoint(x:16,y:6))
                        right.curve(to:NSPoint(x:18,y:8),controlPoint1:NSPoint(x:17.105,y:6),controlPoint2:NSPoint(x:18,y:6.895))
                        right.line(to:NSPoint(x:18,y:16));right.curve(to:NSPoint(x:16,y:18),controlPoint1:NSPoint(x:18,y:17.105),controlPoint2:NSPoint(x:17.105,y:18));right.line(to:NSPoint(x:13.065,y:18));stroke(right)
                        let left=NSBezierPath();left.move(to:NSPoint(x:5.14,y:18));left.line(to:NSPoint(x:4,y:18))
                        left.curve(to:NSPoint(x:2,y:16),controlPoint1:NSPoint(x:2.895,y:18),controlPoint2:NSPoint(x:2,y:17.105))
                        left.line(to:NSPoint(x:2,y:8));left.curve(to:NSPoint(x:4,y:6),controlPoint1:NSPoint(x:2,y:6.895),controlPoint2:NSPoint(x:2.895,y:6));left.line(to:NSPoint(x:6.936,y:6));stroke(left)
                    } else {
                        stroke(NSBezierPath(roundedRect:NSRect(x:2,y:6,width:16,height:12),xRadius:2,yRadius:2))
                        if let percent=presentation.percent,percent>0 {
                            NSBezierPath(roundedRect:NSRect(x:4,y:8,width:12*CGFloat(percent)/100,height:8),xRadius:0.6,yRadius:0.6).fill()
                        }
                    }
                }
            }
            return true
        }
        image.isTemplate=true;image.accessibilityDescription=presentation.accessibilityLabel
        return image
    }
    private static func drawIcon(at origin:NSPoint,size:CGFloat,draw:()->Void) {
        NSGraphicsContext.saveGraphicsState();defer{NSGraphicsContext.restoreGraphicsState()}
        let transform=NSAffineTransform();transform.translateX(by:origin.x,yBy:origin.y+18)
        transform.scaleX(by:size/24,yBy:-size/24);transform.concat();draw()
    }
    private static func stroke(_ path:NSBezierPath) {
        path.lineWidth=2;path.lineCapStyle = .round;path.lineJoinStyle = .round;path.stroke()
    }
    private static func polyline(_ points:[(CGFloat,CGFloat)]) {
        guard let first=points.first else{return}
        let path=NSBezierPath();path.move(to:NSPoint(x:first.0,y:first.1))
        for p in points.dropFirst(){path.line(to:NSPoint(x:p.0,y:p.1))};stroke(path)
    }
}
