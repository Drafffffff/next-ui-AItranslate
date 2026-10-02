import AppKit

final class ConnectionDot:NSView {
    var color:NSColor = .tertiaryLabelColor {didSet{needsDisplay=true}}
    override func draw(_ dirtyRect:NSRect) {color.setFill();NSBezierPath(ovalIn:bounds.insetBy(dx:1,dy:1)).fill()}
    override var intrinsicContentSize:NSSize {NSSize(width:9,height:9)}
}

extension AppDelegate {
    func label(_ text:String,size:CGFloat=13,color:NSColor = .secondaryLabelColor)->NSTextField {
        let l=NSTextField(labelWithString:text);l.font = .systemFont(ofSize:size);l.textColor=color
        l.maximumNumberOfLines=0;l.lineBreakMode = .byWordWrapping;return l
    }
    func button(_ text:String,_ selector:Selector)->NSButton {
        let b=NSButton(title:text,target:self,action:selector);b.bezelStyle = .rounded;return b
    }
    func symbolButton(_ symbol:String,_ title:String,_ selector:Selector)->NSButton {
        let b=button("",selector);b.image=NSImage(systemSymbolName:symbol,accessibilityDescription:title)
        b.isBordered=false;b.toolTip=title;b.setAccessibilityLabel(title)
        b.widthAnchor.constraint(equalToConstant:28).isActive=true
        b.heightAnchor.constraint(equalToConstant:28).isActive=true;return b
    }
    func row(_ views:[NSView],spacing:CGFloat=10)->NSStackView {
        let s=NSStackView(views:views);s.orientation = .horizontal;s.alignment = .centerY;s.spacing=spacing;return s
    }
    func column(_ views:[NSView],spacing:CGFloat=12)->NSStackView {
        let s=NSStackView(views:views);s.orientation = .vertical;s.alignment = .leading;s.spacing=spacing;return s
    }
    func fullWidth(_ view:NSView,in stack:NSStackView) {
        view.translatesAutoresizingMaskIntoConstraints=false;view.widthAnchor.constraint(equalTo:stack.widthAnchor).isActive=true
    }
    func divider()->NSBox {let line=NSBox();line.boxType = .separator;return line}
    func attach(_ stack:NSStackView,to content:NSView,inset:CGFloat=24) {
        stack.translatesAutoresizingMaskIntoConstraints=false;content.addSubview(stack)
        NSLayoutConstraint.activate([stack.leadingAnchor.constraint(equalTo:content.leadingAnchor,constant:inset),stack.trailingAnchor.constraint(equalTo:content.trailingAnchor,constant:-inset),stack.topAnchor.constraint(equalTo:content.topAnchor,constant:inset),stack.bottomAnchor.constraint(equalTo:content.bottomAnchor,constant:-inset)])
    }
    func buildUI() {
        statusItem=NSStatusBar.system.statusItem(withLength:NSStatusItem.variableLength)
        statusItem.button?.font=NSFont.monospacedDigitSystemFont(ofSize:12,weight:.medium);statusItem.button?.imagePosition = .imageLeading;updateStatusBar();statusItem.button?.target=self;statusItem.button?.action=#selector(statusClicked)
        statusItem.button?.sendAction(on:[.leftMouseUp,.rightMouseUp]);statusItem.button?.setAccessibilityLabel("Brick Mic")
        let menu=NSMenu()
        for (title,selector,shortcut) in [("打开 Brick Mic",#selector(show),""),("设置…",#selector(showSettings),","),("重新连接掌机",#selector(reconnect),"")] {
            let item=NSMenuItem(title:title,action:selector,keyEquivalent:shortcut);item.target=self;menu.addItem(item)
        }
        menu.addItem(.separator());let quitItem=NSMenuItem(title:"退出 Brick Mic",action:#selector(quit),keyEquivalent:"q");quitItem.target=self;menu.addItem(quitItem)
        statusMenu=menu
        let appMenu=NSMenu();let root=NSMenuItem();root.submenu=menu;appMenu.addItem(root);NSApp.mainMenu=appMenu

        window=NSWindow(contentRect:NSRect(x:0,y:0,width:416,height:390),styleMask:[.titled,.closable,.miniaturizable,.resizable],backing:.buffered,defer:false)
        window.title="Brick Mic";window.titlebarAppearsTransparent=true;window.isReleasedWhenClosed=false
        window.minSize=NSSize(width:376,height:360);window.center()
        let content=NSView();window.contentView=content
        status.font = .systemFont(ofSize:14,weight:.medium);status.maximumNumberOfLines=2;status.lineBreakMode = .byWordWrapping
        status.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
        let spacer=NSView();spacer.setContentHuggingPriority(.defaultLow,for:.horizontal)
        let header=row([connectionDot,status,spacer,symbolButton("gearshape","设置",#selector(showSettings)),symbolButton("ellipsis","更多操作",#selector(showMore))],spacing:8)
        detail.font = .monospacedDigitSystemFont(ofSize:12,weight:.regular);detail.textColor = .secondaryLabelColor
        meter.levelIndicatorStyle = .continuousCapacity;meter.minValue=0;meter.maxValue=1
        meter.warningValue=1;meter.criticalValue=1;meter.fillColor = .controlAccentColor
        deviceStatus.font = .systemFont(ofSize:12);deviceStatus.textColor = .secondaryLabelColor
        activityRow=row([meter,detail]);activityRow.isHidden=true;meter.widthAnchor.constraint(equalToConstant:120).isActive=true

        transcript.isEditable=false;transcript.isSelectable=true;transcript.font = .systemFont(ofSize:19,weight:.regular)
        transcript.textColor = .labelColor;transcript.drawsBackground=false
        transcript.textContainerInset=NSSize(width:0,height:8);transcript.autoresizingMask=[.width];transcript.isVerticallyResizable=true
        transcript.textContainer?.widthTracksTextView=true;transcript.textContainer?.lineFragmentPadding=0
        transcript.setAccessibilityLabel("识别文字")
        let paragraph=NSMutableParagraphStyle();paragraph.lineSpacing=6
        transcript.defaultParagraphStyle=paragraph
        resultScroll.documentView=transcript;resultScroll.hasVerticalScroller=true;resultScroll.autohidesScrollers=true
        resultScroll.drawsBackground=false;resultScroll.borderType = .noBorder
        resultScroll.translatesAutoresizingMaskIntoConstraints=false
        let result=NSView();result.translatesAutoresizingMaskIntoConstraints=false;result.addSubview(resultScroll)
        NSLayoutConstraint.activate([result.heightAnchor.constraint(greaterThanOrEqualToConstant:195),resultScroll.leadingAnchor.constraint(equalTo:result.leadingAnchor),resultScroll.trailingAnchor.constraint(equalTo:result.trailingAnchor),resultScroll.topAnchor.constraint(equalTo:result.topAnchor),resultScroll.bottomAnchor.constraint(equalTo:result.bottomAnchor)])
        emptyIcon.image=NSImage(systemSymbolName:"mic",accessibilityDescription:nil);emptyIcon.contentTintColor = .tertiaryLabelColor;emptyIcon.setAccessibilityElement(false)
        emptyIcon.symbolConfiguration=NSImage.SymbolConfiguration(pointSize:42,weight:.light)
        emptyIcon.heightAnchor.constraint(equalToConstant:56).isActive=true
        emptyTitle.font = .systemFont(ofSize:17,weight:.medium);emptyTitle.textColor = .labelColor
        emptyHint.font = .systemFont(ofSize:12);emptyHint.textColor = .secondaryLabelColor
        progress.style = .spinning;progress.controlSize = .regular;progress.isDisplayedWhenStopped=false
        progress.heightAnchor.constraint(equalToConstant:56).isActive=true
        progress.widthAnchor.constraint(equalToConstant:32).isActive=true;progress.isHidden=true
        emptyState=column([emptyIcon,progress,emptyTitle,emptyHint],spacing:10);emptyState.alignment = .centerX;emptyState.translatesAutoresizingMaskIntoConstraints=false
        result.addSubview(emptyState)
        NSLayoutConstraint.activate([emptyState.centerXAnchor.constraint(equalTo:result.centerXAnchor),emptyState.centerYAnchor.constraint(equalTo:result.centerYAnchor),emptyState.widthAnchor.constraint(lessThanOrEqualTo:result.widthAnchor)])
        automatic.title="自动输入";automatic.target=self;automatic.action=#selector(toggleAuto)
        automatic.toolTip="识别结束后填入录音开始时的前台应用"
        copyButton=button("复制文字",#selector(copyText));copyButton.isEnabled=false
        let flexible=NSView();flexible.setContentHuggingPriority(.defaultLow,for:.horizontal)
        let actions=row([automatic,flexible,copyButton])
        permissionButton.title="设置输入权限…";permissionButton.target=self;permissionButton.action=#selector(permissions)
        permissionButton.bezelStyle = .rounded;permissionButton.controlSize = .small
        permissionRow=row([label("需要辅助功能权限",size:12),permissionButton]);permissionRow.isHidden=true
        let line=divider()
        let stack=column([header,deviceStatus,activityRow,result,line,actions,permissionRow],spacing:16)
        attach(stack,to:content)
        for v in [header,deviceStatus,activityRow!,result,line,actions,permissionRow!] as [NSView] {fullWidth(v,in:stack)}
        buildSettings()
        updateResult()
        if probeOut==nil {window.orderFront(nil)}
    }
    func buildSettings() {
        settingsWindow=NSWindow(contentRect:NSRect(x:0,y:0,width:440,height:320),styleMask:[.titled,.closable],backing:.buffered,defer:false)
        settingsWindow.title="Brick Mic 设置";settingsWindow.isReleasedWhenClosed=false;settingsWindow.titlebarAppearsTransparent=true;settingsWindow.center()
        let content=NSView();settingsWindow.contentView=content
        let apiTitle=label("百炼 API Key",size:14,color:.labelColor);apiTitle.font = .systemFont(ofSize:14,weight:.semibold)
        key.placeholderString="填入百炼 API Key";key.font = .systemFont(ofSize:13);key.bezelStyle = .roundedBezel
        key.setAccessibilityLabel("百炼 API Key")
        key.heightAnchor.constraint(equalToConstant:30).isActive=true
        keyHint.font = .systemFont(ofSize:11);keyHint.textColor = .secondaryLabelColor;keyHint.maximumNumberOfLines=2
        let api=column([apiTitle,key,keyHint],spacing:8)
        let permissionTitle=label("输入到其他应用",size:14,color:.labelColor);permissionTitle.font = .systemFont(ofSize:14,weight:.semibold)
        permissionState.font = .systemFont(ofSize:12);permissionState.textColor = .secondaryLabelColor
        settingsPermissionButton=button("设置输入权限…",#selector(permissions))
        inputTestButton=button("测试输入",#selector(testInput));inputTestButton.toolTip="3 秒后输入测试文字，请先点击目标文本框"
        remoteControl.target=self;remoteControl.action=#selector(toggleRemote);remoteControl.isEnabled=false;remoteControl.font = .systemFont(ofSize:12)
        let input=column([permissionTitle,permissionState,row([settingsPermissionButton,inputTestButton]),remoteControl],spacing:8)
        advancedButton=NSButton(title:"高级设置",target:self,action:#selector(toggleAdvanced));advancedButton.isBordered=false;advancedButton.image=NSImage(systemSymbolName:"chevron.right",accessibilityDescription:nil);advancedButton.imagePosition = .imageLeading;advancedButton.setButtonType(.pushOnPushOff)
        advancedButton.setAccessibilityLabel("展开高级设置")
        model.placeholderString=ASRConfiguration.model;endpoint.placeholderString="wss:// 服务域名/api-ws/v1/inference"
        model.setAccessibilityLabel("语音识别模型");endpoint.setAccessibilityLabel("百炼所在地域的服务地址")
        advanced=column([label("识别模型",size:11),model,label("服务地址",size:11),endpoint],spacing:6);advanced.isHidden=true
        for v in [model,endpoint] {fullWidth(v,in:advanced)}
        let flexible=NSView();flexible.setContentHuggingPriority(.defaultLow,for:.horizontal)
        saveFeedback.font = .systemFont(ofSize:12);saveFeedback.maximumNumberOfLines=2;saveFeedback.lineBreakMode = .byWordWrapping
        saveFeedback.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
        let saveButton=button("保存",#selector(save));saveButton.keyEquivalent="\r"
        let footer=row([saveFeedback,flexible,saveButton])
        let gap=NSView();gap.setContentHuggingPriority(.defaultLow,for:.vertical);gap.heightAnchor.constraint(greaterThanOrEqualToConstant:0).isActive=true
        let line=divider();let stack=column([api,line,input,advancedButton,advanced,gap,footer],spacing:18)
        attach(stack,to:content)
        for v in [api,key,line,input,advanced!,footer] as [NSView] {fullWidth(v,in:v===key ? api:stack)}
    }
    func updateResult() {
        let processing=currentSession != 0 && stopAt != nil
        emptyIcon.isHidden=processing;emptyTitle.isHidden=currentSession != 0;emptyHint.isHidden=processing;progress.isHidden = !processing
        if processing {progress.startAnimation(nil)} else {progress.stopAnimation(nil)}
        let hasText = !transcript.string.isEmpty
        emptyState.isHidden=hasText;resultScroll.isHidden = !hasText;copyButton.isEnabled=hasText
        if currentSession != 0 {
            emptyTitle.stringValue=stopAt==nil ? "正在听你说":"正在整理文字"
            emptyHint.stringValue=stopAt==nil ? "松开 A 完成，按 B 取消":"稍等片刻"
        } else {
            if !connected && (status.stringValue.contains("蓝牙授权") || status.stringValue.contains("允许 Brick Mic 使用蓝牙")) {
                emptyTitle.stringValue="允许蓝牙连接"
                emptyHint.stringValue="请确认 macOS 的蓝牙授权提示"
            } else if !connected {
                emptyTitle.stringValue="打开掌机上的 Brick Mic"
                emptyHint.stringValue="连接后，按住 A 开始说话"
            } else if key.stringValue.isEmpty && preview==nil {
                emptyTitle.stringValue="先配置百炼 API Key"
                emptyHint.stringValue="点击右上角设置，填入 Key"
            } else {
                emptyTitle.stringValue="按住 A，说点什么"
                emptyHint.stringValue=automatic.state == .on ? "松开后，文字会填入当前应用":"松开后，文字会显示在这里"
            }
        }
    }
    func updateStatusBar() {
        guard let button=statusItem?.button else{return}
        let active=connected && currentSession != 0 && !broken
        let value=MicStatusBarPresentation(connected:connected,recording:active && stopAt==nil,processing:active && stopAt != nil,percent:batteryPercent,charging:batteryCharging)
        if value != lastStatusBarPresentation {
            lastStatusBarPresentation=value;statusItem.length=value.itemWidth;button.image=MicStatusBarIcons.image(value);button.title=value.title
            button.setAccessibilityLabel(value.accessibilityLabel)
        }
        button.toolTip=value.accessibilityLabel+(status.stringValue.isEmpty ? "":"\n"+status.stringValue)
    }
    func setRecording(_ active:Bool) {
        let recording=active && stopAt==nil
        activityRow.isHidden = !recording;updateStatusBar()
        connectionDot.color=recording ? .systemRed:(active ? .controlAccentColor:(connected ? .systemGreen:.tertiaryLabelColor))
        updateResult();updateWindowMinimum()
    }
    func updateWindowMinimum() {
        // Recording and permission guidance must fit even after the user shrinks the window.
        let minimum:CGFloat=370+(activityRow.isHidden ? 0:32)+(permissionRow.isHidden ? 0:40)
        window.minSize=NSSize(width:376,height:minimum)
        if window.frame.height<minimum {
            var frame=window.frame;frame.origin.y -= minimum-frame.height;frame.size.height=minimum
            window.setFrame(frame,display:true)
        }
    }
    func connectionChanged(_ message:String) {
        if message.hasPrefix("已连接") {
            connected=true
            if currentSession==0 {setStatus(key.stringValue.isEmpty ? "请先配置百炼 Key":"掌机已连接")}
        } else {
            connected=false
            if currentSession==0 {
                if message.contains("等待") || message.contains("寻找") {setStatus("等待掌机连接")}
                else if message.contains("正在连接") {setStatus("正在连接掌机…")}
                else {setStatus(message)}
            }
        }
        if currentSession==0 {connectionDot.color=connected ? .systemGreen:.tertiaryLabelColor;updateResult();updateWindowMinimum()}
    }
    func updateKeyHint() {keyHint.stringValue=key.stringValue.isEmpty ? "保存到 ~/.zshrc，后续启动自动读取":"已配置 · 保存位置：~/.zshrc"}
    @objc func toggleAdvanced() {
        let visible=advancedButton.state == .on;advanced.isHidden = !visible
        advancedButton.image=NSImage(systemSymbolName:visible ? "chevron.down":"chevron.right",accessibilityDescription:nil)
        advancedButton.setAccessibilityLabel(visible ? "收起高级设置":"展开高级设置")
        var frame=settingsWindow.frame;let desired:CGFloat=visible ? 460:320
        frame.origin.y += frame.height-(desired+settingsWindow.frame.height-settingsWindow.contentLayoutRect.height)
        frame.size.height=desired+settingsWindow.frame.height-settingsWindow.contentLayoutRect.height
        settingsWindow.setFrame(frame,display:true)
    }
    @objc func showMore(_ sender:NSButton) {statusMenu.popUp(positioning:nil,at:NSPoint(x:0,y:sender.bounds.minY),in:sender)}
    @objc func statusClicked() {
        if NSApp.currentEvent?.type == .rightMouseUp,let button=statusItem.button {statusMenu.popUp(positioning:nil,at:NSPoint(x:0,y:button.bounds.minY),in:button)}
        else if window.isVisible && NSApp.isActive {window.orderOut(nil)} else {show()}
    }
}
