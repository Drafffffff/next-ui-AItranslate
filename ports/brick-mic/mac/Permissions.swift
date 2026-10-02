import AppKit
import ApplicationServices

struct InputPermissionState {
    let accessibility:Bool
    let posting:Bool
    var allowed:Bool {accessibility && posting}
    var message:String {
        if allowed{return "已允许 · 可以自动输入和控制光标"}
        if accessibility{return "辅助功能已开启 · 输入授权尚未生效"}
        return "尚未授权 · 添加应用后，请开启旁边的开关"
    }
}

enum PermissionApplication {
    static func resolve(running:URL?,bundle:URL)->URL? {
        for candidate in [running,bundle].compactMap({$0}) where candidate.isFileURL && candidate.pathExtension.lowercased()=="app" {
            return candidate.standardizedFileURL
        }
        return nil
    }
    static var current:URL? {resolve(running:NSRunningApplication.current.bundleURL,bundle:Bundle.main.bundleURL)}
}

// A real file URL, so System Settings receives the app bundle, not an alias or executable.
final class PermissionAppCard:NSView,NSDraggingSource {
    let applicationURL:URL
    private let icon:NSImage
    private let draggable:Bool
    private var downEvent:NSEvent?
    init(applicationURL:URL,draggable:Bool=true) {
        self.applicationURL=applicationURL;self.draggable=draggable
        icon=NSWorkspace.shared.icon(forFile:applicationURL.path)
        super.init(frame:.zero)
        translatesAutoresizingMaskIntoConstraints=false
        let image=NSImageView();image.image=icon;image.imageScaling = .scaleProportionallyUpOrDown
        let name=NSTextField(labelWithString:applicationURL.lastPathComponent);name.font = .systemFont(ofSize:16,weight:.semibold)
        let path=NSTextField(wrappingLabelWithString:applicationURL.path);path.font = .systemFont(ofSize:11);path.textColor = .secondaryLabelColor;path.maximumNumberOfLines=2
        let hint=NSTextField(labelWithString:draggable ? "拖动这个应用到权限列表":"预览图标 · 正式安装后可拖动");hint.font = .systemFont(ofSize:12);hint.textColor = .secondaryLabelColor
        let text=NSStackView(views:[name,path,hint]);text.orientation = .vertical;text.alignment = .leading;text.spacing=6
        for view in [image,text] as [NSView]{view.translatesAutoresizingMaskIntoConstraints=false;addSubview(view)}
        NSLayoutConstraint.activate([heightAnchor.constraint(greaterThanOrEqualToConstant:112),image.leadingAnchor.constraint(equalTo:leadingAnchor,constant:16),image.centerYAnchor.constraint(equalTo:centerYAnchor),image.widthAnchor.constraint(equalToConstant:64),image.heightAnchor.constraint(equalToConstant:64),text.leadingAnchor.constraint(equalTo:image.trailingAnchor,constant:14),text.trailingAnchor.constraint(equalTo:trailingAnchor,constant:-16),text.topAnchor.constraint(equalTo:topAnchor,constant:16),text.bottomAnchor.constraint(equalTo:bottomAnchor,constant:-16)])
        setAccessibilityRole(.group);setAccessibilityLabel("\(applicationURL.lastPathComponent)，拖动到辅助功能权限列表")
        setAccessibilityHelp("也可使用在 Finder 中显示应用按钮")
        toolTip=applicationURL.path
    }
    required init?(coder:NSCoder){fatalError("init(coder:) is not used")}
    override func draw(_ dirtyRect:NSRect){
        super.draw(dirtyRect)
        let path=NSBezierPath(roundedRect:bounds.insetBy(dx:1,dy:1),xRadius:12,yRadius:12)
        NSColor.controlBackgroundColor.setFill();path.fill();NSColor.separatorColor.setStroke();path.lineWidth=1;path.stroke()
    }
    override func hitTest(_ point:NSPoint)->NSView? {super.hitTest(point)==nil ? nil:self}
    override func resetCursorRects(){addCursorRect(bounds,cursor:draggable ? .openHand:.arrow)}
    override func mouseDown(with event:NSEvent){downEvent=event}
    override func mouseDragged(with event:NSEvent){
        guard draggable,let down=downEvent,hypot(event.locationInWindow.x-down.locationInWindow.x,event.locationInWindow.y-down.locationInWindow.y)>3 else{return}
        downEvent=nil
        let item=NSDraggingItem(pasteboardWriter:applicationURL as NSURL)
        let point=convert(event.locationInWindow,from:nil)
        item.setDraggingFrame(NSRect(x:point.x-32,y:point.y-32,width:64,height:64),contents:icon)
        beginDraggingSession(with:[item],event:down,source:self)
    }
    override func mouseUp(with event:NSEvent){downEvent=nil}
    func draggingSession(_ session:NSDraggingSession,sourceOperationMaskFor context:NSDraggingContext)->NSDragOperation{.copy}
}

final class InputPermissionGuide:NSWindowController {
    let applicationURL:URL
    private let preview:Bool
    let stateLabel=NSTextField(wrappingLabelWithString:"")
    let requestButton=NSButton(title:"系统授权提示…",target:nil,action:nil)
    let check:()->InputPermissionState
    let request:()->Void
    init(applicationURL:URL,preview:Bool,check:@escaping()->InputPermissionState,request:@escaping()->Void) {
        self.applicationURL=applicationURL;self.preview=preview;self.check=check;self.request=request
        let panel=NSPanel(contentRect:NSRect(x:0,y:0,width:490,height:390),styleMask:[.titled,.closable,.nonactivatingPanel],backing:.buffered,defer:false)
        panel.title="允许 Brick Mic 输入";panel.isReleasedWhenClosed=false;panel.hidesOnDeactivate=false;panel.level = .floating
        super.init(window:panel)
        let title=NSTextField(labelWithString:"把应用拖入辅助功能列表");title.font = .systemFont(ofSize:18,weight:.semibold)
        let steps=NSTextField(wrappingLabelWithString:"1. 打开「隐私与安全 → 辅助功能」。\n2. 将下方应用拖入列表，并开启它的开关。");steps.font = .systemFont(ofSize:13);steps.textColor = .secondaryLabelColor
        let card=PermissionAppCard(applicationURL:applicationURL,draggable:!preview)
        let open=makeButton("打开辅助功能设置",#selector(openSettings));open.keyEquivalent="\r"
        let reveal=makeButton("在 Finder 中显示应用",#selector(revealApplication))
        let actions=NSStackView(views:[open,reveal]);actions.spacing=10
        let fallback=NSTextField(wrappingLabelWithString:"不能拖入时，点权限列表的「＋」，选择这个应用。");fallback.font = .systemFont(ofSize:11);fallback.textColor = .secondaryLabelColor
        stateLabel.font = .systemFont(ofSize:12);stateLabel.maximumNumberOfLines=2
        requestButton.target=self;requestButton.action=#selector(requestAuthorization);requestButton.bezelStyle = .rounded
        let refresh=makeButton("刷新状态",#selector(refreshAction))
        let footer=NSStackView(views:[refresh,requestButton]);footer.spacing=10
        let stack=NSStackView(views:[title,steps,card,actions,fallback,stateLabel,footer]);stack.orientation = .vertical;stack.alignment = .leading;stack.spacing=14
        let content=NSView();panel.contentView=content;stack.translatesAutoresizingMaskIntoConstraints=false;content.addSubview(stack)
        NSLayoutConstraint.activate([stack.leadingAnchor.constraint(equalTo:content.leadingAnchor,constant:24),stack.trailingAnchor.constraint(equalTo:content.trailingAnchor,constant:-24),stack.topAnchor.constraint(equalTo:content.topAnchor,constant:24),stack.bottomAnchor.constraint(lessThanOrEqualTo:content.bottomAnchor,constant:-24)])
        for view in [steps,card,fallback,stateLabel] as [NSView]{view.widthAnchor.constraint(equalTo:stack.widthAnchor).isActive=true}
        refreshState();placeBesideSettings()
    }
    required init?(coder:NSCoder){fatalError("init(coder:) is not used")}
    private func makeButton(_ title:String,_ action:Selector)->NSButton{let button=NSButton(title:title,target:self,action:action);button.bezelStyle = .rounded;return button}
    func refreshState(){
        let state=check();stateLabel.stringValue=state.message;stateLabel.textColor=state.allowed ? .systemGreen:.secondaryLabelColor
        requestButton.isHidden=state.allowed
        requestButton.title=state.accessibility ? "申请输入授权…":"系统授权提示…"
    }
    private func placeBesideSettings(){
        guard let window=window,let screen=window.screen ?? NSScreen.main else{return}
        let frame=screen.visibleFrame
        window.setFrameOrigin(NSPoint(x:max(frame.minX,frame.maxX-window.frame.width-24),y:max(frame.minY,frame.maxY-window.frame.height-70)))
    }
    @objc func openSettings(){
        guard !preview else{stateLabel.stringValue="预览模式不会打开系统设置";return}
        placeBesideSettings()
        if let url=URL(string:"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") {NSWorkspace.shared.open(url)}
    }
    @objc func revealApplication(){
        guard !preview else{stateLabel.stringValue="预览模式不会打开 Finder";return}
        NSWorkspace.shared.activateFileViewerSelecting([applicationURL])
    }
    @objc func requestAuthorization(){guard !preview else{return};request();refreshState()}
    @objc func refreshAction(){refreshState()}
}
