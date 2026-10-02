import AppKit

// App icon drawn at its destination resolution. Menu bar states live in StatusBar.swift.
enum MicBrand {
    // The menu bar keeps the app's Brick + microphone silhouette in every state.
    static func drawMenuMark(in rect:NSRect) {
        NSGraphicsContext.saveGraphicsState();defer{NSGraphicsContext.restoreGraphicsState()}
        let transform=NSAffineTransform();transform.translateX(by:rect.minX,yBy:rect.minY)
        transform.scaleX(by:rect.width/18,yBy:rect.height/18);transform.concat()
        NSColor.black.setStroke();NSColor.black.setFill()
        let body=NSBezierPath(roundedRect:NSRect(x:2,y:0.75,width:11.5,height:16.5),xRadius:2.2,yRadius:2.2)
        body.lineWidth=1.35;body.stroke()
        let screen=NSBezierPath(roundedRect:NSRect(x:3.8,y:7.5,width:7.9,height:8.1),xRadius:1,yRadius:1)
        screen.lineWidth=0.8;screen.stroke()
        NSBezierPath(roundedRect:NSRect(x:6.85,y:11.4,width:1.8,height:2.6),xRadius:0.9,yRadius:0.9).fill()
        let mic=NSBezierPath();mic.move(to:NSPoint(x:5.6,y:11.3));mic.line(to:NSPoint(x:5.6,y:10.7))
        mic.curve(to:NSPoint(x:9.9,y:10.7),controlPoint1:NSPoint(x:5.6,y:8.7),controlPoint2:NSPoint(x:9.9,y:8.7));mic.line(to:NSPoint(x:9.9,y:11.3))
        mic.move(to:NSPoint(x:7.75,y:9.25));mic.line(to:NSPoint(x:7.75,y:8.5));mic.lineWidth=1;mic.lineCapStyle = .round;mic.stroke()
        NSBezierPath(roundedRect:NSRect(x:4,y:4,width:3.4,height:1),xRadius:0.2,yRadius:0.2).fill()
        NSBezierPath(roundedRect:NSRect(x:5.2,y:2.8,width:1,height:3.4),xRadius:0.2,yRadius:0.2).fill()
        NSBezierPath(ovalIn:NSRect(x:9.05,y:3.1,width:1.35,height:1.35)).fill()
        NSBezierPath(ovalIn:NSRect(x:10.9,y:4.75,width:1.35,height:1.35)).fill()
    }
    static func drawIcon(in rect:NSRect) {
        NSGraphicsContext.saveGraphicsState();defer{NSGraphicsContext.restoreGraphicsState()}
        let transform=NSAffineTransform();transform.translateX(by:rect.minX,yBy:rect.minY)
        transform.scaleX(by:rect.width/1024,yBy:rect.height/1024);transform.concat()
        let ivory=NSColor(srgbRed:0.95,green:0.93,blue:0.87,alpha:1)
        let green=NSColor(srgbRed:0.09,green:0.26,blue:0.22,alpha:1)
        let screen=NSColor(srgbRed:0.17,green:0.36,blue:0.30,alpha:1)
        let shadow=NSShadow();shadow.shadowColor=green.withAlphaComponent(0.16);shadow.shadowBlurRadius=26;shadow.shadowOffset=NSSize(width:0,height:-10);shadow.set()
        ivory.setFill();NSBezierPath(roundedRect:NSRect(x:64,y:64,width:896,height:896),xRadius:204,yRadius:204).fill()
        NSShadow().set()
        green.setFill();NSBezierPath(roundedRect:NSRect(x:290,y:183,width:444,height:658),xRadius:62,yRadius:62).fill()
        screen.setFill();NSBezierPath(roundedRect:NSRect(x:327,y:439,width:370,height:357),xRadius:28,yRadius:28).fill()
        ivory.setFill();NSBezierPath(roundedRect:NSRect(x:463,y:573,width:98,height:154),xRadius:49,yRadius:49).fill()
        ivory.setStroke()
        let mic=NSBezierPath();mic.move(to:NSPoint(x:420,y:630));mic.line(to:NSPoint(x:420,y:590))
        mic.curve(to:NSPoint(x:604,y:590),controlPoint1:NSPoint(x:420,y:464),controlPoint2:NSPoint(x:604,y:464))
        mic.line(to:NSPoint(x:604,y:630));mic.lineWidth=25;mic.lineCapStyle = .round;mic.stroke()
        let stem=NSBezierPath();stem.move(to:NSPoint(x:512,y:503));stem.line(to:NSPoint(x:512,y:476));stem.move(to:NSPoint(x:478,y:471));stem.line(to:NSPoint(x:546,y:471));stem.lineWidth=25;stem.lineCapStyle = .round;stem.stroke()
        ivory.setFill()
        NSBezierPath(roundedRect:NSRect(x:357,y:304,width:112,height:35),xRadius:8,yRadius:8).fill()
        NSBezierPath(roundedRect:NSRect(x:395.5,y:266,width:35,height:112),xRadius:8,yRadius:8).fill()
        NSBezierPath(ovalIn:NSRect(x:571,y:287,width:45,height:45)).fill()
        NSBezierPath(ovalIn:NSRect(x:637,y:333,width:45,height:45)).fill()
        ivory.withAlphaComponent(0.55).setFill()
        NSBezierPath(roundedRect:NSRect(x:484,y:224,width:56,height:8),xRadius:4,yRadius:4).fill()
    }
}
