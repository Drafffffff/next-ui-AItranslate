import AppKit
import ApplicationServices
import Network
import CryptoKit

struct RemoteTask:Codable {
    var id:String
    var title:String
    var status:String
    var updated:Double
}

struct LatestReply:Codable {
    let text:String
    let turn:String
    let updated:Double
    let truncated:Bool
    func revision(task:String)->String {SHA256.hash(data:Data((task+"\n"+turn+"\n"+String(updated)+"\n"+text).utf8)).map{String(format:"%02x",$0)}.joined()}
}

private func wireText(_ text:String,bytes:Int,characters:Int=Int.max)->String {
    var value=""
    for character in text.prefix(characters){if value.utf8.count+String(character).utf8.count>bytes{break};value.append(character)}
    return value
}

private func replySlices(_ text:String)->(pages:[String],truncated:Bool) {
    var pages:[String]=[],page="",count=0,clipped=false
    for character in text {
        var next=String(character)
        if next.utf8.count>2000{next="〔超长字符略去〕";clipped=true}
        if !page.isEmpty && (count>=500||page.utf8.count+next.utf8.count>2000){pages.append(page);page="";count=0}
        page+=next;count+=1
    }
    if !page.isEmpty{pages.append(page)}
    return (pages.isEmpty ? [""]:pages,clipped)
}

// Runs on the main queue. Every answer belongs to one request and one BLE session.
// Missing, repeated, expired, cancelled and reconnected requests never approve.
final class ApprovalBroker {
    struct Request {
        let id:String,task:String,turn:String,connection:String,owner:String,title:String,tool:String,summary:String
        let expires:Double,allowAvailable:Bool
        let finish:([String:Any])->Void
        var wire:[String:Any]{["id":id,"task":task,"title":wireText(title,bytes:256,characters:64),"tool":tool,"summary":summary,"expiresAt":expires,"allowAvailable":allowAvailable]}
    }
    private(set) var requests:[Request]=[]
    var onChange:(()->Void)?
    var first:Request?{requests.first}
    @discardableResult func add(_ request:Request,now:Double=Date().timeIntervalSince1970)->Bool {
        expire(now:now)
        guard UUID(uuidString:request.id) != nil,!request.connection.isEmpty,request.expires>now,requests.count<8,!requests.contains(where:{$0.id==request.id}) else{request.finish([:]);return false}
        requests.append(request);onChange?();return true
    }
    @discardableResult func decide(_ id:String,choice:String,connection:String,now:Double=Date().timeIntervalSince1970)->Bool {
        expire(now:now)
        guard requests.first?.id==id,let index=requests.firstIndex(where:{$0.id==id}),["allow","deny"].contains(choice),requests[index].connection==connection,
              choice != "allow" || requests[index].allowAvailable else{return false}
        let request=requests.remove(at:index)
        request.finish(["request_id":request.id,"decision":choice]);onChange?();return true
    }
    func cancel(_ predicate:(Request)->Bool={_ in true}) {
        let removed=requests.filter(predicate);requests.removeAll(where:predicate)
        for request in removed{request.finish([:])}
        if !removed.isEmpty{onChange?()}
    }
    func expire(now:Double=Date().timeIntervalSince1970){cancel{$0.expires<=now}}
}

// Pure facts keep candidate policy testable without reading a running application.
// AXValue being read-only does not imply that a web editor cannot receive keys.
struct ComposerCandidateFacts {
    var role:String
    var subrole=""
    var hints=[String]()
    var enabled=true
    var explicitlyReadOnly=false
    var insideComposer=false
    var localSendAction=false
    var localTextEditorCount=0
    var valueWritable=false
    var focused=false
    var focusWritable=false
    var ancestorCandidates=[Int]()
}

enum ComposerCandidateChoice:Equatable {case selected(Int),none,disabled,ambiguous,incomplete}

enum ComposerCandidatePolicy {
    static func isTextRole(_ role:String)->Bool {role==kAXTextAreaRole || role==kAXTextFieldRole}
    static func isComposer(_ facts:ComposerCandidateFacts)->Bool {
        guard isTextRole(facts.role),!facts.explicitlyReadOnly else{return false}
        let hint=facts.hints.joined(separator:" ").lowercased()
        // Search/name editors are never a fallback, even if they happen to be focused.
        guard !facts.subrole.lowercased().contains("search"),!["search","filter","rename","chat title","搜索","查找","筛选","重命名","聊天标题","message history","transcript","conversation-log","message-list","消息历史","对话记录"].contains(where:{hint.contains($0)}) else{return false}
        // These exact accessible names come from the current app's composer,
        // rather than generic text-field heuristics.
        if exactComposerLabel(facts.hints){return true}
        let strong=["composer","prompt-input","prompt_input","message-input","message_input","chat-input","chat_input","follow up","follow-up","followup","ask codex","message codex","send a message","send message","输入消息","发送消息","继续对话","询问 codex","向 codex"]
        if strong.contains(where:{hint.contains($0)}){return true}
        // A prompt/ask label alone is accepted only on a text area; it must be a
        // whole word, so a generic task title such as 'task' cannot match 'ask'.
        let words=Set(hint.components(separatedBy:CharacterSet.alphanumerics.inverted).filter{!$0.isEmpty})
        if facts.role==kAXTextAreaRole && (!words.isDisjoint(with:["prompt","ask","message"]) || hint.contains("输入") || hint.contains("询问")){return true}
        if facts.insideComposer{return true}
        // Unlabelled rich text editors need a small local composer container:
        // one text area next to a clearly identified send action, not a window.
        return facts.role==kAXTextAreaRole && facts.localSendAction && facts.localTextEditorCount==1
    }
    static func exactComposerLabel(_ hints:[String])->Bool {
        let text=hints.joined(separator:" ").lowercased()
        guard !["search","filter","rename","chat title","搜索","查找","筛选","重命名","聊天标题"].contains(where:{text.contains($0)}) else{return false}
        return hints.contains{["do anything","随心输入"].contains($0.trimmingCharacters(in:.whitespacesAndNewlines).lowercased())}
    }
    static func focusConfirmed(frontmost:Bool,focusedEditable:Bool,sameElement:Bool,ancestorMatch:Bool)->Bool {
        frontmost && focusedEditable && (sameElement || ancestorMatch)
    }
    static func headingSemantics(role:String,hidden:Bool,labelMatches:Bool,subrole:String="",roleDescription:String="")->Bool {
        let semantic=role=="AXHeading" || subrole.lowercased().contains("heading") || roleDescription.lowercased().contains("heading") || roleDescription.contains("标题")
        return semantic && !hidden && labelMatches
    }
    static func mainDocumentIsUnique(documentCount:Int,scanComplete:Bool)->Bool{scanComplete && documentCount==1}
    static func bannerScope(role:String,subrole:String,hidden:Bool,pathRoles:[String])->Bool {
        let barriers:Set<String>=[kAXScrollAreaRole,"AXList",kAXTableRole,kAXTextAreaRole,kAXTextFieldRole]
        return role==kAXGroupRole && subrole==(kAXLandmarkBannerSubrole as String) && !hidden && pathRoles.first=="AXWebArea" && pathRoles.filter{$0=="AXWebArea"}.count==1 && pathRoles.dropFirst().dropLast().allSatisfy{!barriers.contains($0)}
    }
    static func bannerProof(role:String,subrole:String,hidden:Bool,pathRoles:[String],knownTitle:Bool,labelMatches:Bool,scanComplete:Bool)->Bool {
        scanComplete && knownTitle && labelMatches && bannerScope(role:role,subrole:subrole,hidden:hidden,pathRoles:pathRoles)
    }
    static func taskTitleLabel(role:String)->Bool{[kAXButtonRole,kAXStaticTextRole,"AXHeading"].contains(role)}
    static func identityLabelProof(hidden:Bool,labelMatches:Bool)->Bool{!hidden && labelMatches}
    static func currentSelection(selected:Bool,ariaCurrent:String)->Bool {
        selected || ["true","page","step","location","date","time"].contains(ariaCurrent.lowercased())
    }
    private static let uuidExpression=try? NSRegularExpression(pattern:"(?<![0-9a-fA-F])[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}(?![0-9a-fA-F])")
    static func exactUUID(_ text:String,id:String)->Bool {
        guard UUID(uuidString:id) != nil,let expression=uuidExpression else{return false}
        let value=text as NSString
        return expression.matches(in:text,range:NSRange(location:0,length:value.length)).contains{value.substring(with:$0.range).lowercased()==id.lowercased()}
    }
    // AXURL is a CFURLRef; accessibility documents may also expose strings.
    static func routeText(_ value:CFTypeRef?)->String {
        if let url=value as? URL{return url.absoluteString}
        if let url=value as? NSURL{return url.absoluteString ?? ""}
        return value as? String ?? ""
    }
    static func webDocumentProof(role:String,route:CFTypeRef?,id:String)->Bool {
        guard role=="AXWebArea",UUID(uuidString:id) != nil,let url=URLComponents(string:routeText(route)),url.scheme?.lowercased()=="codex",url.host?.lowercased()=="threads",url.query==nil,url.fragment==nil,url.user==nil,url.password==nil,url.port==nil else{return false}
        return url.path.lowercased()=="/"+id.lowercased()
    }
    static func select(_ candidates:[ComposerCandidateFacts],scanComplete:Bool=true)->ComposerCandidateChoice {
        guard scanComplete else{return .incomplete}
        let plausible=candidates.indices.filter{isComposer(candidates[$0])}
        let possible=Set(plausible)
        // Nested editable nodes represent one composer, not independent editors.
        let roots=plausible.filter{Set(candidates[$0].ancestorCandidates).isDisjoint(with:possible)}
        if roots.isEmpty{return plausible.isEmpty ? .none:.incomplete}
        let groups=roots.map{root in plausible.filter{$0==root || candidates[$0].ancestorCandidates.contains(root)}}
        let enabled=groups.compactMap{group in group.first{candidates[$0].enabled}}
        if enabled.count>1{return .ambiguous}
        if let index=enabled.first{return .selected(index)}
        return .disabled
    }
    static func uniqueSend(_ candidates:[ComposerSendFacts],scanComplete:Bool)->Int? {
        guard scanComplete else{return nil}
        var seen=Set<Int>()
        let indexes=candidates.indices.filter{candidates[$0].enabled && candidates[$0].explicitSend && seen.insert(candidates[$0].identity).inserted}
        return indexes.count==1 ? indexes[0]:nil
    }

}

enum ComposerTraversalPolicy {
    static let terminalRoles:Set<String>=[kAXStaticTextRole,kAXImageRole,kAXButtonRole,"AXLink",kAXMenuItemRole,"AXSeparator"]
    static func excludesSubtree(role:String,identifier:String,description:String,hidden:Bool)->Bool {
        if hidden || terminalRoles.contains(role){return true}
        let id=identifier.lowercased(),label=description.lowercased()
        let historyIDs=["message-list","message_list","message-history","message_history","chat-history","chat_history","conversation-transcript","chat-log","chat_log"]
        let historyLabels:Set<String>=["message history","conversation history","chat history","conversation transcript","消息历史","对话历史","聊天记录"]
        return [kAXScrollAreaRole,"AXList",kAXGroupRole].contains(role) && (historyIDs.contains{ id==$0 || id.hasPrefix($0+"-") || id.hasPrefix($0+"_") } || historyLabels.contains(label))
    }
    static func excludesIdentitySubtree(role:String,identifier:String,description:String,hidden:Bool)->Bool {
        if [kAXButtonRole,"AXLink"].contains(role) && !hidden{return false}
        return excludesSubtree(role:role,identifier:identifier,description:description,hidden:hidden)
    }
    static func crossesDocument(role:String,isScopeRoot:Bool)->Bool{role=="AXWebArea" && !isScopeRoot}
    static func preferVisibleChildren<Element>(_ visible:[Element]?,fallback:[Element])->[Element] {visible ?? fallback}
}

// Every asynchronous retry checks both this lease and the caller's mode/session.
struct ComposerFocusLease {
    private(set) var generation:UInt64=0
    mutating func begin()->UInt64{generation &+= 1;return generation}
    mutating func cancel(){generation &+= 1}
    func allows(_ request:UInt64,contextCurrent:Bool)->Bool{request==generation && contextCurrent}
}

struct ComposerSendFacts {let identity:Int;let enabled:Bool;let explicitSend:Bool}

enum ComposerIdentityMetric:String,CaseIterable {
    case knownID,titleUnique,selectedCount,ariaCurrentCount,headingCount,webAreaCount,bannerCount,bannerScopeRejectedCount,bannerLabelMatchCount,bannerIncompleteCount,urlPresentCount,webRouteRejectedCount,idMatchCount,headerRelationCount,headerLabelMatchCount,proofFound,scanComplete,capHit
}

// Shared by worker-side reads/traversals; a partial tree cannot prove uniqueness.
final class ComposerScanBudget {
    enum Stop:Equatable {case cancelled,deadline,truncated,axFailure}
    private(set) var stop:Stop?
    private(set) var reads=0,nodes=0
    private(set) var pruned=0,visibleLists=0,axError=0
    private(set) var identityCounts:[ComposerIdentityMetric:Int]=[:]
    let deadline:TimeInterval,startedAt:TimeInterval,messageSeconds:TimeInterval
    private let maxReads:Int,maxNodes:Int,current:()->Bool,now:()->TimeInterval
    init(deadline:TimeInterval,maxReads:Int=16000,maxNodes:Int=10000,messageSeconds:TimeInterval=0.1,current:@escaping()->Bool={true},now:@escaping()->TimeInterval={ProcessInfo.processInfo.systemUptime}) {
        self.deadline=deadline;self.startedAt=now();self.messageSeconds=messageSeconds;self.maxReads=maxReads;self.maxNodes=maxNodes;self.current=current;self.now=now
    }
    var remaining:TimeInterval {max(0,deadline-now())}
    var elapsedMs:Int {Int(max(0,now()-startedAt)*1000)}
    var messageTimeout:Float {Float(min(messageSeconds,max(0.001,remaining)))}
    @discardableResult func check()->Bool {
        if stop != nil{return false}
        if !current(){stop = .cancelled;return false}
        if remaining<=0{stop = .deadline;return false}
        return true
    }
    func read()->Bool {guard check() else{return false};guard reads<maxReads else{stop = .truncated;return false};reads+=1;return true}
    func visit()->Bool {guard check() else{return false};guard nodes<maxNodes else{stop = .truncated;return false};nodes+=1;return true}
    func truncate(){if stop==nil{stop = .truncated}}
    func failAX(_ code:Int){axError=code;if stop==nil{stop = .axFailure}}
    func identity(_ metric:ComposerIdentityMetric,_ count:Int=1){identityCounts[metric,default:0]+=count}
    func recordPruned(){pruned+=1}
    func recordVisible(){visibleLists+=1}
    func recoverTransientAX()->Bool {
        guard stop == .axFailure,axError==Int(AXError.cannotComplete.rawValue),remaining>0,current() else{return false}
        stop=nil;return true
    }
}

// Cancellation is the only worker-side access to mutable request state.
private final class ComposerFocusGate {
    private let lock=NSLock()
    private var lease=ComposerFocusLease()
    func begin()->UInt64{lock.lock();defer{lock.unlock()};return lease.begin()}
    func cancel(){lock.lock();defer{lock.unlock()};lease.cancel()}
    func snapshot()->UInt64{lock.lock();defer{lock.unlock()};return lease.generation}
    func allows(_ id:UInt64)->Bool{lock.lock();defer{lock.unlock()};return lease.allows(id,contextCurrent:true)}
}

private struct ComposerRegistry:Equatable {let ids:Set<String>,titles:Set<String>}

// Uses public accessibility and the documented task deep link. No private Codex DB/API.
final class RemoteEditor {
    enum FocusFailure:Equatable {
        case permission,appUnavailable,invalidTask,targetUnverified,composerMissing,composerAmbiguous,composerDisabled,focusUnsupported,focusUnconfirmed,scanIncomplete,cancelled
        var message:String {
            switch self {
            case .permission:return "请在 Mac 允许辅助功能权限 · 文字已保留"
            case .appUnavailable:return "Codex 尚未打开 · 文字已保留"
            case .invalidTask:return "任务标识无效 · 请重新选择任务"
            case .targetUnverified:return "任务已打开，但无法确认目标任务 · 文字已保留"
            case .composerMissing:return "任务已打开，但未识别到消息输入框 · 文字已保留"
            case .composerAmbiguous:return "发现多个消息输入框 · 请关闭多余任务窗口"
            case .composerDisabled:return "消息输入框暂不可编辑 · 文字已保留"
            case .focusUnsupported:return "已找到输入框，但应用未接受聚焦 · 文字已保留"
            case .focusUnconfirmed:return "已找到输入框，但焦点尚未生效 · 文字已保留"
            case .scanIncomplete:return "输入框检查超时或未完成 · 文字已保留，请重试"
            case .cancelled:return "输入操作已取消 · 文字已保留"
            }
        }
    }
    private enum TaskProof {case document,title,banner(windowPath:[AXUIElement],web:AXUIElement,banner:AXUIElement,labelPath:[AXUIElement],label:AXUIElement),web(path:[AXUIElement],node:AXUIElement,key:String),headerRelation(ownerPath:[AXUIElement],owner:AXUIElement,key:String,header:AXUIElement,labelPath:[AXUIElement],label:AXUIElement)}
    private struct Match {let id:String;let pid:pid_t;let field:AXUIElement,window:AXUIElement;let proof:TaskProof;var strongID:Bool{switch proof{case .document,.web:return true;default:return false}}}
    private struct ScanNode {let element:AXUIElement;let parent:Int?;let values:[String:CFTypeRef];var role:String{values[kAXRoleAttribute] as? String ?? ""}}
    // These caches and registry snapshots are accessed only from the main queue.
    private static var binding:Match?
    private static var identityHint:(id:String,pid:pid_t,window:AXUIElement,proof:TaskProof)?
    private static var prepared:(match:Match,title:String,request:UInt64,expires:TimeInterval)?
    private static var draftResults:[String:(value:Bool,at:TimeInterval,pid:pid_t)]=[:]
    private static var draftLookupAt=Date.distantPast
    private static var draftLookupPending=false
    private static let scanQueue=DispatchQueue(label:"com.nextui.brickmic.composer-scan",qos:.userInitiated)
    private static let diagnosticQueue=DispatchQueue(label:"com.nextui.brickmic.composer-diagnostics",qos:.utility)
    private static func recordDiagnostic(_ stage:String,budget:ComposerScanBudget) {
        var object:[String:Any] = ["stage":stage,"reads":budget.reads,"nodes":budget.nodes,"pruned":budget.pruned,"visibleLists":budget.visibleLists,"stop":budget.stop.map{String(describing:$0)} ?? "none","axError":budget.axError,"remainingMs":Int(budget.remaining*1000),"elapsedMs":budget.elapsedMs]
        for (metric,count) in budget.identityCounts{object[metric.rawValue]=count}
        guard let data=try? JSONSerialization.data(withJSONObject:object,options:.sortedKeys) else{return}
        diagnosticQueue.async {
            let directory=FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/Brick Mic")
            let file=directory.appendingPathComponent("composer-diagnostics.jsonl")
            try? FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
            if let size=(try? FileManager.default.attributesOfItem(atPath:file.path)[.size]) as? NSNumber,size.intValue>131072{try? FileManager.default.removeItem(at:file)}
            if !FileManager.default.fileExists(atPath:file.path){FileManager.default.createFile(atPath:file.path,contents:nil,attributes:[.posixPermissions:0o600])}
            guard let handle=try? FileHandle(forWritingTo:file) else{return}
            defer{try? handle.close()}
            _ = try? handle.seekToEnd();try? handle.write(contentsOf:data+Data([10]))
        }
    }
    private static let focusGate=ComposerFocusGate()
    private(set) static var lastFocusFailure:FocusFailure?
    private(set) static var desktopIDs=Set<String>()
    private(set) static var uniqueDesktopTitles=Set<String>()
    private static var registry:ComposerRegistry{ComposerRegistry(ids:desktopIDs,titles:uniqueDesktopTitles)}
    static func register(_ tasks:[RemoteTask],ids:Set<String>){
        let validIDs=Set(tasks.filter{UUID(uuidString:$0.id) != nil}.map{$0.id})
        let nextIDs=ids.intersection(validIDs)
        // CLI/hook tasks can collide with a desktop title but are not ID sources.
        let counts=Dictionary(grouping:tasks,by:{$0.title})
        let nextTitles=Set(counts.filter{$0.value.count==1 && nextIDs.contains($0.value[0].id)}.keys)
        if nextIDs != desktopIDs || nextTitles != uniqueDesktopTitles {focusGate.cancel();binding=nil;identityHint=nil;prepared=nil;draftResults.removeAll()}
        desktopIDs=nextIDs;uniqueDesktopTitles=nextTitles
    }
    static func attribute(_ element:AXUIElement,_ key:String,budget:ComposerScanBudget?=nil)->CFTypeRef? {
        if let budget=budget {guard budget.read() else{return nil};AXUIElementSetMessagingTimeout(element,budget.messageTimeout)}
        var value:CFTypeRef?
        let result=AXUIElementCopyAttributeValue(element,key as CFString,&value)
        if result == .cannotComplete || result == .invalidUIElement || result == .apiDisabled || result == .failure{budget?.failAX(Int(result.rawValue))}
        if let budget=budget,!budget.check(){return nil}
        return result == .success ? value:nil
    }
    static func string(_ e:AXUIElement,_ key:String,budget:ComposerScanBudget?=nil)->String {attribute(e,key,budget:budget) as? String ?? ""}
    static func children(_ e:AXUIElement,budget:ComposerScanBudget?=nil)->[AXUIElement] {attribute(e,kAXChildrenAttribute,budget:budget) as? [AXUIElement] ?? []}
    private static func values(_ element:AXUIElement,_ keys:[String],budget:ComposerScanBudget)->[String:CFTypeRef] {
        guard budget.read() else{return [:]}
        AXUIElementSetMessagingTimeout(element,budget.messageTimeout)
        var result:CFArray?
        let error=AXUIElementCopyMultipleAttributeValues(element,keys as CFArray,[],&result)
        guard error == .success,let array=result as? [CFTypeRef],array.count==keys.count else{budget.failAX(Int(error.rawValue));return [:]}
        var out=[String:CFTypeRef]()
        for (index,key) in keys.enumerated() {
            let value=array[index]
            if CFGetTypeID(value)==CFNullGetTypeID(){continue}
            if CFGetTypeID(value)==AXValueGetTypeID() {
                let typed=unsafeBitCast(value,to:AXValue.self)
                if AXValueGetType(typed) == .axError {
                    var error=AXError.success
                    if AXValueGetValue(typed,.axError,&error),[kAXRoleAttribute,kAXChildrenAttribute].contains(key),[AXError.cannotComplete,.invalidUIElement,.apiDisabled,.failure].contains(error){budget.failAX(Int(error.rawValue))}
                    continue
                }
            }
            out[key]=value
        }
        return budget.check() ? out:[:]
    }
    private static func nodeValues(_ element:AXUIElement,budget:ComposerScanBudget,identity:Bool=false)->[String:CFTypeRef] {
        var keys=[kAXRoleAttribute,kAXIdentifierAttribute,kAXDescriptionAttribute,kAXSelectedAttribute,"AXHidden",kAXChildrenAttribute,kAXVisibleChildrenAttribute]
        if identity{keys += [kAXSubroleAttribute,kAXRoleDescriptionAttribute,"AXARIACurrent"]}
        return values(element,keys,budget:budget)
    }
    private static func nodeChildren(_ values:[String:CFTypeRef],budget:ComposerScanBudget)->[AXUIElement] {
        let visible=values[kAXVisibleChildrenAttribute] as? [AXUIElement]
        if visible != nil{budget.recordVisible()}
        return ComposerTraversalPolicy.preferVisibleChildren(visible,fallback:values[kAXChildrenAttribute] as? [AXUIElement] ?? [])
    }
    private static func tree(_ root:AXUIElement,limit:Int,budget:ComposerScanBudget,prune:Bool=false,identity:Bool=false,documentScope:Bool=false)->[ScanNode] {
        var pending:[(AXUIElement,Int?)]=[(root,nil)],out=[ScanNode]()
        while let (element,parent)=pending.popLast() {
            guard budget.visit() else{break}
            guard out.count<limit else{budget.truncate();break}
            let metadata=nodeValues(element,budget:budget,identity:identity)
            guard budget.check() else{break}
            let index=out.count;out.append(ScanNode(element:element,parent:parent,values:metadata))
            let excluded=prune && (identity ? ComposerTraversalPolicy.excludesIdentitySubtree:ComposerTraversalPolicy.excludesSubtree)(metadata[kAXRoleAttribute] as? String ?? "",metadata[kAXIdentifierAttribute] as? String ?? "",metadata[kAXDescriptionAttribute] as? String ?? "",(metadata["AXHidden"] as? Bool)==true)
            if excluded || (documentScope && ComposerTraversalPolicy.crossesDocument(role:metadata[kAXRoleAttribute] as? String ?? "",isScopeRoot:CFEqual(element,root))){budget.recordPruned();continue}
            pending.append(contentsOf:nodeChildren(metadata,budget:budget).map{($0,index)})
        };return out
    }
    private static func path(_ nodes:[ScanNode],index:Int)->[AXUIElement] {
        var current:Int?=index,out=[AXUIElement]()
        while let value=current{out.append(nodes[value].element);current=nodes[value].parent}
        return out.reversed()
    }
    static func descendants(_ root:AXUIElement,limit:Int=1200,budget:ComposerScanBudget?=nil)->[AXUIElement] {
        let scan=budget ?? ComposerScanBudget(deadline:ProcessInfo.processInfo.systemUptime+0.1,maxReads:400,maxNodes:1200)
        return tree(root,limit:limit,budget:scan).map{$0.element}
    }
    private static func elementAttribute(_ e:AXUIElement,_ key:String,budget:ComposerScanBudget?=nil)->AXUIElement? {
        guard let value=attribute(e,key,budget:budget),CFGetTypeID(value)==AXUIElementGetTypeID() else{return nil}
        return unsafeBitCast(value,to:AXUIElement.self)
    }
    static func editable(_ e:AXUIElement,budget:ComposerScanBudget?=nil)->Bool {
        let scan=budget ?? ComposerScanBudget(deadline:ProcessInfo.processInfo.systemUptime+0.06,maxReads:2,maxNodes:2,messageSeconds:0.03)
        let metadata=values(e,[kAXRoleAttribute,kAXEnabledAttribute,"AXReadOnly","AXEditable"],budget:scan)
        return ComposerCandidatePolicy.isTextRole(metadata[kAXRoleAttribute] as? String ?? "") && (metadata[kAXEnabledAttribute] as? Bool) != false && (metadata["AXReadOnly"] as? Bool) != true && (metadata["AXEditable"] as? Bool) != false && scan.check()
    }
    static func focused(_ pid:pid_t,budget:ComposerScanBudget?=nil)->AXUIElement? {
        let scan=budget ?? ComposerScanBudget(deadline:ProcessInfo.processInfo.systemUptime+0.08,maxReads:12,maxNodes:8,messageSeconds:0.03)
        guard let field=elementAttribute(AXUIElementCreateApplication(pid),kAXFocusedUIElementAttribute,budget:scan),editable(field,budget:scan),scan.check() else{return nil}
        return field
    }
    private static func leafBelongs(_ focused:AXUIElement,to field:AXUIElement,budget:ComposerScanBudget)->Bool {
        if CFEqual(field,focused){return ComposerCandidatePolicy.focusConfirmed(frontmost:true,focusedEditable:true,sameElement:true,ancestorMatch:false)}
        var parent=focused
        for _ in 0..<6 {
            guard budget.visit(),let next=elementAttribute(parent,kAXParentAttribute,budget:budget) else{return false}
            if CFEqual(next,field){return ComposerCandidatePolicy.focusConfirmed(frontmost:true,focusedEditable:true,sameElement:false,ancestorMatch:true)};parent=next
        };return false
    }
    private static func focusedWithin(_ field:AXUIElement,pid:pid_t,budget:ComposerScanBudget)->Bool {
        guard let focused=focused(pid,budget:budget) else{return false}
        return leafBelongs(focused,to:field,budget:budget)
    }
    static func post(_ key:CGKeyCode,pid:pid_t,flags:CGEventFlags=[]){
        for down in [true,false] {if let event=CGEvent(keyboardEventSource:nil,virtualKey:key,keyDown:down){event.flags=flags;event.postToPid(pid)}}
    }
    static func edit(_ op:String,selection:Bool,pid:pid_t,expectedField:AXUIElement?=nil)->Bool {
        guard NSWorkspace.shared.frontmostApplication?.processIdentifier==pid else{return false}
        var field:AXUIElement?
        if let expected=expectedField {
            guard let focused=focused(pid),CFEqual(expected,focused) else{return false}
            field=focused
        }
        let arrows:[String:CGKeyCode]=["left":123,"right":124,"up":126,"down":125]
        if let key=arrows[op]{post(key,pid:pid,flags:selection ? .maskShift:[]);return true}
        let shortcuts:[String:(CGKeyCode,CGEventFlags)]=["delete":(51,[]),"undo":(6,.maskCommand),"redo":(6,[.maskCommand,.maskShift]),"copy":(8,.maskCommand),"paste":(9,.maskCommand),"all":(0,.maskCommand),"space":(49,[]),"enter":(36,[])]
        if let (key,flags)=shortcuts[op]{post(key,pid:pid,flags:flags);return true}
        if op=="newline",expectedField==nil || field.map({string($0,kAXRoleAttribute)==kAXTextAreaRole})==true {
            for down in [true,false]{if let event=CGEvent(keyboardEventSource:nil,virtualKey:0,keyDown:down){event.flags=[];event.keyboardSetUnicodeString(stringLength:1,unicodeString:[10]);event.postToPid(pid)}};return true
        };return false
    }
    static func editingOperation(_ op:String,codex:Bool)->String {op=="enter" && codex ? "newline":op}
    static func codex()->NSRunningApplication? {NSWorkspace.shared.runningApplications.first{$0.bundleIdentifier=="com.openai.codex"}}
    private static func hint(_ e:AXUIElement,budget:ComposerScanBudget)->String {
        let keys=[kAXPlaceholderValueAttribute,kAXDescriptionAttribute,kAXIdentifierAttribute,kAXTitleAttribute]
        let metadata=values(e,keys,budget:budget)
        return keys.compactMap{metadata[$0] as? String}.joined(separator:" ").lowercased()
    }
    private static func sendButton(_ e:AXUIElement,budget:ComposerScanBudget,role:String?=nil)->Bool {
        guard (role ?? string(e,kAXRoleAttribute,budget:budget))==kAXButtonRole else{return false}
        let label=hint(e,budget:budget)
        let words=Set(label.components(separatedBy:CharacterSet.alphanumerics.inverted))
        return (words.contains("send") || label.contains("发送")) && !["resend","feedback","反馈"].contains(where:{label.contains($0)})
    }
    private static func localContainers(_ field:AXUIElement,budget:ComposerScanBudget)->[[ScanNode]] {
        var current=field,containers=[[ScanNode]]()
        for _ in 0..<2 {
            guard budget.visit(),let parent=elementAttribute(current,kAXParentAttribute,budget:budget) else{break}
            let role=string(parent,kAXRoleAttribute,budget:budget)
            if [kAXWindowRole,"AXWebArea",kAXSplitGroupRole,kAXScrollAreaRole].contains(role){break}
            // Oversized containers provide no evidence. Inspect at most 49 nodes
            // locally without truncating the complete window scan's budget.
            let localBudget=ComposerScanBudget(deadline:budget.deadline,maxReads:200,maxNodes:49,messageSeconds:budget.messageSeconds,current:{budget.check()})
            let local=tree(parent,limit:49,budget:localBudget)
            if localBudget.stop==nil,local.count<=48{containers.append(local)}
            else if localBudget.stop != .truncated {budget.check();return []}
            current=parent
        };return containers
    }
    private static func facts(_ field:AXUIElement,role:String,budget:ComposerScanBudget)->ComposerCandidateFacts {
        let metadata=values(field,[kAXSubroleAttribute,kAXPlaceholderValueAttribute,kAXTitleAttribute,kAXDescriptionAttribute,kAXIdentifierAttribute,kAXEnabledAttribute,"AXReadOnly","AXEditable"],budget:budget)
        let hints=[kAXPlaceholderValueAttribute,kAXTitleAttribute,kAXDescriptionAttribute,kAXIdentifierAttribute].compactMap{metadata[$0] as? String}
        var result=ComposerCandidateFacts(role:role,subrole:metadata[kAXSubroleAttribute] as? String ?? "",hints:hints,enabled:(metadata[kAXEnabledAttribute] as? Bool) != false,explicitlyReadOnly:(metadata["AXReadOnly"] as? Bool)==true || (metadata["AXEditable"] as? Bool)==false)
        // Labelled editors already have positive evidence; do not traverse their
        // ancestors/messages again for optional structural evidence.
        if ComposerCandidatePolicy.isComposer(result) || result.explicitlyReadOnly{return result}
        let hint=hints.joined(separator:" ").lowercased()
        if ["search","filter","rename","chat title","搜索","查找","筛选","重命名","聊天标题"].contains(where:{hint.contains($0)}){return result}
        for container in localContainers(field,budget:budget) {
            guard budget.check(),let parent=container.first?.element else{break}
            let label=self.hint(parent,budget:budget)
            if ["composer","message-input","prompt-input","chat-input"].contains(where:{label.contains($0)}){result.insideComposer=true}
            if container.contains(where:{sendButton($0.element,budget:budget,role:$0.role)}) {
                result.localSendAction=true
                let editorIndexes=container.indices.filter{ComposerCandidatePolicy.isTextRole(container[$0].role)}
                let indexSet=Set(editorIndexes)
                result.localTextEditorCount=editorIndexes.filter{index in
                    var parent=container[index].parent
                    while let value=parent{if indexSet.contains(value){return false};parent=container[value].parent}
                    return true
                }.count
            }
        };return result
    }
    private static func labelMatches(_ node:AXUIElement,title:String,budget:ComposerScanBudget,taskLabelOnly:Bool=false)->Bool {
        let label=values(node,[kAXTitleAttribute,kAXDescriptionAttribute,kAXValueAttribute,kAXRoleAttribute,"AXHidden"],budget:budget)
        if taskLabelOnly,!ComposerCandidatePolicy.taskTitleLabel(role:label[kAXRoleAttribute] as? String ?? ""){return false}
        let matches=(label[kAXTitleAttribute] as? String)==title || (label[kAXValueAttribute] as? String)==title || (label[kAXDescriptionAttribute] as? String)==title
        return ComposerCandidatePolicy.identityLabelProof(hidden:(label["AXHidden"] as? Bool)==true,labelMatches:matches) && budget.check()
    }
    private static func proofPathCurrent(_ path:[AXUIElement],budget:ComposerScanBudget)->Bool {
        guard !path.isEmpty else{return false}
        for index in 1..<path.count {
            guard budget.visit() else{return false}
            let metadata=values(path[index-1],[kAXChildrenAttribute,kAXVisibleChildrenAttribute,"AXHidden"],budget:budget)
            guard (metadata["AXHidden"] as? Bool) != true,nodeChildren(metadata,budget:budget).contains(where:{CFEqual($0,path[index])}) else{return false}
        }
        return budget.check()
    }
    private static func proofMatches(_ proof:TaskProof,_ task:RemoteTask,window:AXUIElement,registry:ComposerRegistry,budget:ComposerScanBudget)->Bool {
        let knownTitle=registry.ids.contains(task.id) && registry.titles.contains(task.title)
        switch proof {
        case .document:return ComposerCandidatePolicy.exactUUID(ComposerCandidatePolicy.routeText(attribute(window,kAXDocumentAttribute,budget:budget)),id:task.id) && budget.check()
        case .title:
            let title=string(window,kAXTitleAttribute,budget:budget)
            return knownTitle && (title==task.title || title==task.title+" — Codex") && budget.check()
        case .banner(let windowPath,let web,let banner,let labelPath,let label):
            guard knownTitle,let first=windowPath.first,CFEqual(first,window),bannerPathCurrent(windowPath,web:web,banner:banner,budget:budget),let labelRoot=labelPath.first,CFEqual(labelRoot,banner),proofPathCurrent(labelPath,budget:budget) else{return false}
            return labelMatches(label,title:task.title,budget:budget,taskLabelOnly:true) && budget.check()
        case .web(let path,let node,let key):
            guard let first=path.first,CFEqual(first,window),proofPathCurrent(path,budget:budget),string(node,kAXRoleAttribute,budget:budget)=="AXWebArea" else{return false}
            return ComposerCandidatePolicy.webDocumentProof(role:"AXWebArea",route:attribute(node,key,budget:budget),id:task.id) && budget.check()
        case .headerRelation(let ownerPath,let owner,let key,let header,let labelPath,let label):
            guard knownTitle,let first=ownerPath.first,CFEqual(first,window),proofPathCurrent(ownerPath,budget:budget),CFEqual(owner,window) || string(owner,kAXRoleAttribute,budget:budget)=="AXWebArea",let current=elementAttribute(owner,key,budget:budget),CFEqual(current,header),proofPathCurrent(labelPath,budget:budget),(attribute(label,"AXHidden",budget:budget) as? Bool) != true else{return false}
            return labelMatches(label,title:task.title,budget:budget) && budget.check()
        }
    }
    // A document's identity may authorize only an editor inside that document.
    // Workspaces can contain several independent WebAreas in the same window.
    private static func composerRoot(_ proof:TaskProof,window:AXUIElement)->AXUIElement {
        switch proof {
        case .banner(_,let web,_,_,_):return web
        case .web(_,let node,_):return node
        case .headerRelation(_,let owner,_,_,_,_):return owner
        default:return window
        }
    }
    private static func composerInScope(_ field:AXUIElement,proof:TaskProof,window:AXUIElement,budget:ComposerScanBudget)->Bool {
        let root=composerRoot(proof,window:window)
        if CFEqual(root,window){return true}
        var current=field
        for _ in 0..<24 {
            if CFEqual(current,root){return budget.check()}
            guard budget.visit(),!ComposerTraversalPolicy.crossesDocument(role:string(current,kAXRoleAttribute,budget:budget),isScopeRoot:false),let parent=elementAttribute(current,kAXParentAttribute,budget:budget) else{return false}
            current=parent
        }
        return false
    }
    private static func pathIndexes(_ nodes:[ScanNode],index:Int)->[Int] {
        var current:Int?=index,out=[Int]()
        while let value=current{out.append(value);current=nodes[value].parent}
        return out.reversed()
    }
    // Optional identity scopes have explicit local completeness without turning
    // a local node cap into a deadline/cancellation failure for the whole scan.
    private static func identityNodes(_ root:AXUIElement,limit:Int,budget:ComposerScanBudget,stopAtDocuments:Bool=false,documentScope:Bool=false)->(nodes:[ScanNode],complete:Bool){
        var pending:[(AXUIElement,Int?)]=[(root,nil)],nodes=[ScanNode](),offset=0
        while offset<pending.count {
            guard nodes.count<limit,budget.visit() else{return (nodes,false)}
            let (element,parent)=pending[offset];offset+=1
            let data=nodeValues(element,budget:budget,identity:true)
            guard budget.check() else{return (nodes,false)}
            let index=nodes.count;nodes.append(ScanNode(element:element,parent:parent,values:data))
            let role=data[kAXRoleAttribute] as? String ?? ""
            if ComposerTraversalPolicy.excludesIdentitySubtree(role:role,identifier:data[kAXIdentifierAttribute] as? String ?? "",description:data[kAXDescriptionAttribute] as? String ?? "",hidden:(data["AXHidden"] as? Bool)==true) || (stopAtDocuments && role=="AXWebArea") || (documentScope && ComposerTraversalPolicy.crossesDocument(role:role,isScopeRoot:index==0)){budget.recordPruned();continue}
            pending.append(contentsOf:nodeChildren(data,budget:budget).map{($0,index)})
        }
        return (nodes,budget.check())
    }
    private static func bannerPathCurrent(_ path:[AXUIElement],web:AXUIElement,banner:AXUIElement,budget:ComposerScanBudget)->Bool {
        guard let last=path.last,CFEqual(last,banner) else{return false}
        var roles=[String](),subrole="",webIndex:Int?
        for index in path.indices {
            guard budget.visit() else{return false}
            let data=values(path[index],[kAXRoleAttribute,kAXSubroleAttribute,"AXHidden",kAXChildrenAttribute,kAXVisibleChildrenAttribute],budget:budget)
            guard (data["AXHidden"] as? Bool) != true,budget.check() else{return false}
            roles.append(data[kAXRoleAttribute] as? String ?? "")
            if CFEqual(path[index],web){webIndex=index}
            if index<path.count-1,!nodeChildren(data,budget:budget).contains(where:{CFEqual($0,path[index+1])}){return false}
            if index==path.count-1{subrole=data[kAXSubroleAttribute] as? String ?? ""}
        }
        guard let first=webIndex,roles.filter({$0=="AXWebArea"}).count==1 else{return false}
        return ComposerCandidatePolicy.bannerScope(role:roles.last ?? "",subrole:subrole,hidden:false,pathRoles:Array(roles[first...])) && budget.check()
    }
    private static func discoverBanner(_ task:RemoteTask,window:AXUIElement,registry:ComposerRegistry,budget:ComposerScanBudget)->TaskProof? {
        guard registry.ids.contains(task.id),registry.titles.contains(task.title) else{return nil}
        // Complete only the native window structure. Stop at every outer WebArea
        // so chat history and embedded workspace documents do not enter this scan.
        let outer=identityNodes(window,limit:160,budget:budget,stopAtDocuments:true)
        let documents=outer.nodes.indices.filter{outer.nodes[$0].role=="AXWebArea" && (outer.nodes[$0].values["AXHidden"] as? Bool) != true}
        budget.identity(.webAreaCount,documents.count)
        guard ComposerCandidatePolicy.mainDocumentIsUnique(documentCount:documents.count,scanComplete:outer.complete),let document=documents.first else{budget.identity(.bannerScopeRejectedCount);return nil}
        let web=outer.nodes[document].element,webPath=path(outer.nodes,index:document)
        var pending:[(AXUIElement,Int?)]=[(web,nil)],nodes=[ScanNode](),offset=0
        while offset<pending.count && nodes.count<512 {
            guard budget.visit() else{return nil}
            let (element,parent)=pending[offset];offset+=1
            let data=nodeValues(element,budget:budget,identity:true)
            guard budget.check() else{return nil}
            let index=nodes.count;nodes.append(ScanNode(element:element,parent:parent,values:data))
            let role=data[kAXRoleAttribute] as? String ?? "",subrole=data[kAXSubroleAttribute] as? String ?? "",hidden=(data["AXHidden"] as? Bool)==true
            if subrole==(kAXLandmarkBannerSubrole as String) {
                budget.identity(.bannerCount)
                let roles=pathIndexes(nodes,index:index).map{nodes[$0].role}
                if ComposerCandidatePolicy.bannerScope(role:role,subrole:subrole,hidden:hidden,pathRoles:roles) {
                    let labels=identityNodes(element,limit:80,budget:budget,documentScope:true)
                    let match=labels.complete ? labels.nodes.indices.first(where:{labelMatches(labels.nodes[$0].element,title:task.title,budget:budget,taskLabelOnly:true)}):nil
                    if ComposerCandidatePolicy.bannerProof(role:role,subrole:subrole,hidden:hidden,pathRoles:roles,knownTitle:true,labelMatches:match != nil,scanComplete:labels.complete),let label=match,budget.check(){
                        budget.identity(.bannerLabelMatchCount)
                        return .banner(windowPath:webPath+path(nodes,index:index).dropFirst(),web:web,banner:element,labelPath:path(labels.nodes,index:label),label:labels.nodes[label].element)
                    }
                    if !labels.complete{budget.identity(.bannerIncompleteCount)}
                }else{budget.identity(.bannerScopeRejectedCount)}
            }
            if ComposerTraversalPolicy.excludesIdentitySubtree(role:role,identifier:data[kAXIdentifierAttribute] as? String ?? "",description:data[kAXDescriptionAttribute] as? String ?? "",hidden:hidden) || ComposerTraversalPolicy.crossesDocument(role:role,isScopeRoot:index==0){budget.recordPruned();continue}
            pending.append(contentsOf:nodeChildren(data,budget:budget).map{($0,index)})
        }
        if offset<pending.count{budget.identity(.bannerIncompleteCount)}
        return nil
    }
    private static func discoverProof(_ task:RemoteTask,window:AXUIElement,registry:ComposerRegistry,hint:TaskProof?,budget:ComposerScanBudget)->TaskProof? {
        let knownTitle=registry.ids.contains(task.id) && registry.titles.contains(task.title)
        for metric in ComposerIdentityMetric.allCases{budget.identity(metric,0)}
        defer{if budget.stop == .truncated,budget.identityCounts[.capHit]==0{budget.identity(.capHit)}}
        budget.identity(.knownID,registry.ids.contains(task.id) ? 1:0);budget.identity(.titleUnique,knownTitle ? 1:0)
        func found(_ proof:TaskProof)->TaskProof{budget.identity(.proofFound);return proof}
        let metadata=values(window,[kAXDocumentAttribute,kAXTitleAttribute],budget:budget)
        if ComposerCandidatePolicy.exactUUID(ComposerCandidatePolicy.routeText(metadata[kAXDocumentAttribute]),id:task.id){budget.identity(.idMatchCount);return found(.document)}
        let title=metadata[kAXTitleAttribute] as? String ?? ""
        if knownTitle && (title==task.title || title==task.title+" — Codex"){return found(.title)}
        if let hint=hint,proofMatches(hint,task,window:window,registry:registry,budget:budget){return found(hint)}
        if let banner=discoverBanner(task,window:window,registry:registry,budget:budget){return found(banner)}
        guard budget.check() else{return nil}
        // Identity scope differs from composer scope: sidebar buttons may own
        // their selected/current task labels and must remain traversable.
        var pending:[(AXUIElement,Int?)]=[(window,nil)],nodes=[ScanNode](),offset=0
        while offset<pending.count && nodes.count<2048 {
            guard budget.visit() else{return nil}
            let (element,parent)=pending[offset];offset+=1
            let data=nodeValues(element,budget:budget,identity:true)
            guard budget.check() else{return nil}
            let index=nodes.count;nodes.append(ScanNode(element:element,parent:parent,values:data))
            if (data["AXHidden"] as? Bool)==true{budget.recordPruned();continue}
            let role=data[kAXRoleAttribute] as? String ?? ""
            if role=="AXWebArea" || index==0 {
                if role=="AXWebArea"{budget.identity(.webAreaCount)}
                let document=values(element,[kAXURLAttribute,kAXDocumentAttribute,kAXHeaderAttribute,kAXTitleUIElementAttribute],budget:budget)
                for key in [kAXURLAttribute,kAXDocumentAttribute] {
                    let value=ComposerCandidatePolicy.routeText(document[key]);if !value.isEmpty{budget.identity(.urlPresentCount)}
                    // Only the window/current web document URL, never a sidebar
                    // link's URL, is accepted as current-task route evidence.
                    if ComposerCandidatePolicy.webDocumentProof(role:role,route:document[key],id:task.id){budget.identity(.idMatchCount);return found(.web(path:path(nodes,index:index),node:element,key:key))}
                    if role=="AXWebArea",!value.isEmpty{budget.identity(.webRouteRejectedCount)}
                }
                if knownTitle {
                    for key in [kAXHeaderAttribute,kAXTitleUIElementAttribute] {
                        guard let value=document[key],CFGetTypeID(value)==AXUIElementGetTypeID() else{continue}
                        budget.identity(.headerRelationCount)
                        let header=unsafeBitCast(value,to:AXUIElement.self)
                        let headerBudget=ComposerScanBudget(deadline:budget.deadline,maxReads:80,maxNodes:48,current:{budget.check()})
                        let labels=tree(header,limit:48,budget:headerBudget,prune:true,identity:true)
                        if headerBudget.stop==nil,let label=labels.indices.first(where:{labelMatches(labels[$0].element,title:task.title,budget:budget)}),budget.check() {
                            budget.identity(.headerLabelMatchCount)
                            return found(.headerRelation(ownerPath:path(nodes,index:index),owner:element,key:key,header:header,labelPath:path(labels,index:label),label:labels[label].element))
                        }
                    }
                }
            }
            let selected=(data[kAXSelectedAttribute] as? Bool)==true
            let aria=data["AXARIACurrent"] as? String ?? ""
            if selected{budget.identity(.selectedCount)}
            if ComposerCandidatePolicy.currentSelection(selected:false,ariaCurrent:aria){budget.identity(.ariaCurrentCount)}
            // Sidebar selection can change before the chat document finishes
            // navigating. Keep its diagnostics; it cannot authorize a composer.
            let heading=ComposerCandidatePolicy.headingSemantics(role:role,hidden:false,labelMatches:true,subrole:data[kAXSubroleAttribute] as? String ?? "",roleDescription:data[kAXRoleDescriptionAttribute] as? String ?? "")
            if heading{budget.identity(.headingCount)}
            // Body headings may quote another task's title, so they are never
            // identity evidence without an explicit header/banner relationship.
            if ComposerTraversalPolicy.excludesIdentitySubtree(role:role,identifier:data[kAXIdentifierAttribute] as? String ?? "",description:data[kAXDescriptionAttribute] as? String ?? "",hidden:false){budget.recordPruned();continue}
            pending.append(contentsOf:nodeChildren(data,budget:budget).map{($0,index)})
        }
        if offset<pending.count{budget.identity(.capHit);budget.truncate()}else{budget.identity(.scanComplete,budget.check() ? 1:0)}
        return nil
    }
    private static func findComposer(_ task:RemoteTask,pid:pid_t,registry:ComposerRegistry,cached:Match?,identity:(window:AXUIElement,proof:TaskProof)?=nil,budget:ComposerScanBudget)->(match:Match?,failure:FocusFailure) {
        let app=AXUIElementCreateApplication(pid)
        guard let window=elementAttribute(app,kAXFocusedWindowAttribute,budget:budget) ?? (attribute(app,kAXWindowsAttribute,budget:budget) as? [AXUIElement])?.first else{return (nil,.targetUnverified)}
        let proofHint=identity.flatMap{CFEqual($0.window,window) ? $0.proof:nil}
        let proofBudget=ComposerScanBudget(deadline:min(budget.deadline,ProcessInfo.processInfo.systemUptime+0.35),maxReads:3600,maxNodes:2600,current:{budget.check()})
        let proof=discoverProof(task,window:window,registry:registry,hint:proofHint,budget:proofBudget)
        recordDiagnostic("identity-discovery",budget:proofBudget)
        guard budget.check() else{return (nil,.scanIncomplete)}
        guard let proof=proof else{return (nil,.targetUnverified)}
        // Task navigation commonly already focuses its composer. Reuse that
        // exact named editor inside the proven main document before tree fallback.
        if case .banner=proof,let field=focused(pid,budget:budget) {
            let names=values(field,[kAXSubroleAttribute,kAXPlaceholderValueAttribute,kAXTitleAttribute,kAXDescriptionAttribute,kAXIdentifierAttribute],budget:budget)
            let hints=names.values.compactMap{$0 as? String}
            if ComposerCandidatePolicy.exactComposerLabel(hints),composerInScope(field,proof:proof,window:window,budget:budget),budget.check(){
                recordDiagnostic("composer-focused",budget:budget)
                return (Match(id:task.id,pid:pid,field:field,window:window,proof:proof),.focusUnconfirmed)
            }
        }
        if let cached=cached,cached.strongID,cached.id==task.id,cached.pid==pid,CFEqual(cached.window,window),proofMatches(proof,task,window:window,registry:registry,budget:budget),composerInScope(cached.field,proof:proof,window:window,budget:budget),editable(cached.field,budget:budget),budget.check(){return (cached,.focusUnconfirmed)}
        let root=composerRoot(proof,window:window)
        let nodes=tree(root,limit:1800,budget:budget,prune:true,documentScope:!CFEqual(root,window))
        guard budget.check() else{recordDiagnostic("composer-tree",budget:budget);return (nil,.scanIncomplete)}
        let indexes=nodes.indices.filter{ComposerCandidatePolicy.isTextRole(nodes[$0].role)}
        var candidates=[ComposerCandidateFacts]()
        let candidateMap=Dictionary(uniqueKeysWithValues:indexes.enumerated().map{($0.element,$0.offset)})
        for index in indexes {
            guard budget.visit() else{break}
            var value=facts(nodes[index].element,role:nodes[index].role,budget:budget),parent=nodes[index].parent
            while let ancestor=parent {guard budget.visit() else{break};if let candidate=candidateMap[ancestor]{value.ancestorCandidates.append(candidate)};parent=nodes[ancestor].parent}
            candidates.append(value)
        }
        recordDiagnostic("composer-candidates",budget:budget)
        switch ComposerCandidatePolicy.select(candidates,scanComplete:budget.check() && candidates.count==indexes.count) {
        case .selected(let index):return (Match(id:task.id,pid:pid,field:nodes[indexes[index]].element,window:window,proof:proof),.focusUnconfirmed)
        case .ambiguous:return (nil,.composerAmbiguous)
        case .disabled:return (nil,.composerDisabled)
        case .none:return (nil,.composerMissing)
        case .incomplete:return (nil,.scanIncomplete)
        }
    }
    // No full tree scan is performed by main-thread editing/draft callers.
    static func composer(_ task:RemoteTask,pid:pid_t)->AXUIElement? {
        let budget=ComposerScanBudget(deadline:ProcessInfo.processInfo.systemUptime+0.06,maxReads:8,maxNodes:8,messageSeconds:0.03)
        guard let cached=binding,cached.strongID,cached.id==task.id,cached.pid==pid,proofMatches(cached.proof,task,window:cached.window,registry:registry,budget:budget),composerInScope(cached.field,proof:cached.proof,window:cached.window,budget:budget),editable(cached.field,budget:budget),budget.check() else{return nil}
        return cached.field
    }
    static func draftAvailable(_ task:RemoteTask)->Bool? {
        guard AXIsProcessTrusted(),let app=codex(),NSWorkspace.shared.frontmostApplication?.processIdentifier==app.processIdentifier else{return nil}
        let now=ProcessInfo.processInfo.systemUptime
        let result=draftResults[task.id]
        if !draftLookupPending,Date().timeIntervalSince(draftLookupAt)>=1 {
            draftLookupAt=Date();draftLookupPending=true
            let snapshot=registry,cached=binding,request=focusGate.snapshot(),pid=app.processIdentifier
            let identity=identityHint.flatMap{$0.id==task.id && $0.pid==pid ? (window:$0.window,proof:$0.proof):nil}
            let budget=ComposerScanBudget(deadline:now+0.25,maxReads:3500,maxNodes:2500,current:{focusGate.allows(request)})
            scanQueue.async {
                let found=findComposer(task,pid:pid,registry:snapshot,cached:cached,identity:identity,budget:budget)
                let text=found.match.flatMap{attribute($0.field,kAXValueAttribute,budget:budget) as? String}
                DispatchQueue.main.async {
                    draftLookupPending=false
                    guard focusGate.allows(request),registry==snapshot else{return}
                    if let match=found.match,match.strongID,budget.check(){binding=match}
                    if let text=text,budget.check(){draftResults[task.id]=(!text.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty,ProcessInfo.processInfo.systemUptime,pid)}
                    else{draftResults.removeValue(forKey:task.id)}
                }
            }
        }
        if let result=result,result.pid==app.processIdentifier,now-result.at<=1.5{return result.value}
        return nil
    }
    static func cancelFocus(){focusGate.cancel();prepared=nil;lastFocusFailure=nil}
    // Weak identities are checked from the current window/selected row, never
    // from a retained heading or stale row captured by a previous scan.
    private static func currentIdentity(_ task:RemoteTask,match:Match,registry:ComposerRegistry,budget:ComposerScanBudget)->Bool {
        guard let window=elementAttribute(AXUIElementCreateApplication(match.pid),kAXFocusedWindowAttribute,budget:budget),CFEqual(window,match.window),budget.check() else{return false}
        return proofMatches(match.proof,task,window:window,registry:registry,budget:budget) && composerInScope(match.field,proof:match.proof,window:window,budget:budget)
    }
    private static func verifiedLeaf(_ task:RemoteTask,match:Match,registry:ComposerRegistry,budget:ComposerScanBudget)->AXUIElement? {
        guard currentIdentity(task,match:match,registry:registry,budget:budget),let leaf=focused(match.pid,budget:budget),leafBelongs(leaf,to:match.field,budget:budget),budget.check() else{return nil}
        return leaf
    }
    // Called immediately before the app captures or inserts text. PID alone is
    // insufficient because users can select another editor in the same app.
    static func preparedFocused(_ pid:pid_t)->AXUIElement? {
        guard let value=prepared,value.match.pid==pid,focusGate.allows(value.request),ProcessInfo.processInfo.systemUptime<value.expires,NSWorkspace.shared.frontmostApplication?.processIdentifier==pid else{return nil}
        let task=RemoteTask(id:value.match.id,title:value.title,status:"",updated:0)
        let budget=ComposerScanBudget(deadline:min(value.expires,ProcessInfo.processInfo.systemUptime+0.12),maxReads:700,maxNodes:400,messageSeconds:0.03,current:{focusGate.allows(value.request)})
        let leaf=verifiedLeaf(task,match:value.match,registry:registry,budget:budget)
        recordDiagnostic("prepared-validation",budget:budget)
        guard NSWorkspace.shared.frontmostApplication?.processIdentifier==pid,ProcessInfo.processInfo.systemUptime<value.expires else{return nil}
        return leaf
    }
    static func focus(_ task:RemoteTask,isCurrent:@escaping()->Bool={true},completion:@escaping(pid_t?)->Void){
        let deadline=ProcessInfo.processInfo.systemUptime+4
        let request=focusGate.begin();prepared=nil;lastFocusFailure=nil
        let snapshot=registry
        var finished=false,navigated=false,activated=false
        func current()->Bool {focusGate.allows(request) && isCurrent() && registry==snapshot}
        func finish(_ pid:pid_t?,_ failure:FocusFailure?=nil){
            guard !finished else{return};finished=true
            if current(){lastFocusFailure=failure}
            completion(pid)
        }
        guard current() else{finish(nil,.cancelled);return}
        guard AXIsProcessTrusted() else{finish(nil,.permission);return}
        guard let app=codex() else{finish(nil,.appUnavailable);return}
        let pid=app.processIdentifier
        guard UUID(uuidString:task.id) != nil,let url=URL(string:"codex://threads/"+task.id) else{finish(nil,.invalidTask);return}
        let budget=ComposerScanBudget(deadline:deadline,current:{focusGate.allows(request)})
        var focusAccepted=false,lastFailure=FocusFailure.targetUnverified,transientRetries=0
        func attempt(){
            guard current() else{finish(nil,.cancelled);return}
            guard !app.isTerminated else{finish(nil,.appUnavailable);return}
            guard ProcessInfo.processInfo.systemUptime<deadline else{finish(nil,lastFailure);return}
            let cached=binding
            let identity=identityHint.flatMap{$0.id==task.id && $0.pid==pid ? (window:$0.window,proof:$0.proof):nil}
            scanQueue.async {
                let found=findComposer(task,pid:pid,registry:snapshot,cached:cached,identity:identity,budget:budget)
                let complete=budget.check()
                DispatchQueue.main.async {
                    guard current() else{finish(nil,.cancelled);return}
                    guard complete,ProcessInfo.processInfo.systemUptime<deadline else {
                        if transientRetries<1,budget.recoverTransientAX(){transientRetries+=1;DispatchQueue.main.asyncAfter(deadline:.now()+0.08){attempt()};return}
                        finish(nil,budget.stop == .cancelled ? .cancelled:.scanIncomplete);return
                    }
                    lastFailure=found.failure
                    guard let match=found.match else {
                        if !navigated,found.failure == .targetUnverified {
                            guard current() else{finish(nil,.cancelled);return}
                            navigated=true;NSWorkspace.shared.open(url)
                            guard current() else{finish(nil,.cancelled);return}
                            activated=true;app.activate(options:[.activateAllWindows])
                        }
                        DispatchQueue.main.asyncAfter(deadline:.now()+0.12){attempt()};return
                    }
                    identityHint=(task.id,pid,match.window,match.proof)
                    if match.strongID{binding=match}
                    if NSWorkspace.shared.frontmostApplication?.processIdentifier != pid {
                        if !activated {guard current() else{finish(nil,.cancelled);return};activated=true;app.activate(options:[.activateAllWindows])}
                        DispatchQueue.main.asyncAfter(deadline:.now()+0.12){attempt()};return
                    }
                    // No worker-side AX focus action uses a stale frontmost snapshot.
                    let actionBudget=ComposerScanBudget(deadline:min(deadline,ProcessInfo.processInfo.systemUptime+0.12),maxReads:700,maxNodes:400,messageSeconds:0.03,current:{focusGate.allows(request)})
                    guard current(),NSWorkspace.shared.frontmostApplication?.processIdentifier==pid,currentIdentity(task,match:match,registry:snapshot,budget:actionBudget) else{recordDiagnostic("focus-identity",budget:actionBudget);finish(nil,actionBudget.stop==nil ? .targetUnverified:.scanIncomplete);return}
                    var leaf=focused(pid,budget:actionBudget)
                    if let value=leaf,!leafBelongs(value,to:match.field,budget:actionBudget){leaf=nil}
                    if leaf==nil,actionBudget.read(),current(),NSWorkspace.shared.frontmostApplication?.processIdentifier==pid {
                        AXUIElementSetMessagingTimeout(match.field,actionBudget.messageTimeout)
                        let accepted=AXUIElementSetAttributeValue(match.field,kAXFocusedAttribute as CFString,kCFBooleanTrue) == .success
                        focusAccepted=focusAccepted || accepted
                        if !accepted,actionBudget.read(),current(),NSWorkspace.shared.frontmostApplication?.processIdentifier==pid {
                            var actions:CFArray?
                            if AXUIElementCopyActionNames(match.field,&actions) == .success,(actions as? [String] ?? []).contains(kAXPressAction),actionBudget.read(),current(){focusAccepted = AXUIElementPerformAction(match.field,kAXPressAction as CFString) == .success || focusAccepted}
                        }
                        leaf=focused(pid,budget:actionBudget)
                        if let value=leaf,!leafBelongs(value,to:match.field,budget:actionBudget){leaf=nil}
                    }
                    recordDiagnostic("focus-action",budget:actionBudget)
                    guard current() else{finish(nil,.cancelled);return}
                    if leaf != nil,actionBudget.check(),NSWorkspace.shared.frontmostApplication?.processIdentifier==pid {
                        if match.strongID{binding=match}else{binding=nil}
                        prepared=(match,task.title,request,ProcessInfo.processInfo.systemUptime+0.25)
                        finish(pid);return
                    }
                    if !actionBudget.check(){finish(nil,.scanIncomplete);return}
                    lastFailure=focusAccepted ? .focusUnconfirmed:.focusUnsupported
                    DispatchQueue.main.asyncAfter(deadline:.now()+0.12){attempt()}
                }
            }
        };attempt()
    }
    static let submitKeyCode:CGKeyCode=36
    static func submit(_ task:RemoteTask,pid:pid_t)->Bool {
        // Only the explicit handheld submit command calls this path. Preparing
        // or inserting recognized text never sends the message automatically.
        guard let value=prepared,value.match.id==task.id,value.match.pid==pid,focusGate.allows(value.request),ProcessInfo.processInfo.systemUptime<value.expires,NSWorkspace.shared.frontmostApplication?.processIdentifier==pid,preparedFocused(pid) != nil else{return false}
        guard focusGate.allows(value.request),ProcessInfo.processInfo.systemUptime<value.expires,NSWorkspace.shared.frontmostApplication?.processIdentifier==pid else{return false}
        prepared=nil
        post(submitKeyCode,pid:pid)
        return true
    }
}

final class MicControl {
    let ble:BrickBluetooth
    var busy:()->Bool={false}
    var permission:()->Bool={false}
    var onHint:((String)->Void)?
    var onBattery:((String)->Void)?
    var onBatteryLevel:((Int?,Bool)->Void)?
    private(set) var mode="ordinary"
    private var targetID:String?
    private var lastID:String?
    private var tasks:[RemoteTask]=[]
    private var unread:[[String:String]]=[]
    private var delivered=Set<String>()
    private var notifications=NotificationDelivery()
    private var focusInFlight=false
    private var listener:NWListener?
    private let queue=DispatchQueue(label:"com.nextui.brickmic.hooks")
    private var connections=Set<ObjectIdentifier>()
    private var observers:[NSObjectProtocol]=[]
    private var draftTimer:Timer?
    private var draftTaskID:String?
    private var draftAvailable:Bool?
    private var hint=""
    private var taskPage=0
    private var lastSnapshot=Data()
    private var lastTasksSnapshot=Data()
    private var desktopIDs=Set<String>()
    private var available=false
    private var lastBattery=""
    private var generation=0
    private var cancelledTurns=Set<String>()
    private var finishedTurns=Set<String>()
    private var replies:[String:LatestReply]=[:]
    private var replyTaskID:String?
    private var replyPage=0
    private var replyView:(id:String,turn:String,updated:Double,pages:[String],truncated:Bool)?
    private let approvals=ApprovalBroker()
    private let hookToken=UUID().uuidString
    private let directory:URL
    private let desktopMetadata:DesktopTaskMetadataStore
    private var metadataLifecycle=0
    private var metadataFailure:DesktopTaskMetadataStore.Failure?
    private var metadataGeneratedAt:Double?
    private var metadataStale=false
    private var stateFile:URL{directory.appendingPathComponent("control-state.json")}
    init(_ ble:BrickBluetooth,directory:URL?=nil){
        self.ble=ble
        let base=directory ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/Brick Mic")
        self.directory=base;self.desktopMetadata=DesktopTaskMetadataStore(directory:base)
    }
    private var selectableTasks:[RemoteTask]{tasks.filter{desktopIDs.contains($0.id)}}
    var inputMode:String{ble.controlsAllowed ? mode:"ordinary"}
    var target:RemoteTask?{tasks.first{$0.id==targetID}}
    func start(){
        try? FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true,attributes:[.posixPermissions:0o700])
        if let data=try? Data(contentsOf:stateFile),let object=try? JSONSerialization.jsonObject(with:data) as? [String:Any] {
            if let data=try? JSONSerialization.data(withJSONObject:object["tasks"] ?? []),let values=try? JSONDecoder().decode([RemoteTask].self,from:data){tasks=values}
            // Legacy control state records delivery/history, not desktop provenance.
            // Only the explicitly sourced canonical snapshot can register IDs.
            desktopIDs=[]
            lastID=object["lastID"] as? String;mode=object["mode"] as? String=="codex" ? "codex":"ordinary"
            targetID=mode=="codex" ? lastID:nil
            unread=object["unread"] as? [[String:String]] ?? []
            delivered=Set(object["delivered"] as? [String] ?? [])
            // Legacy unread entries describe reading, not missed delivery. Migrate
            // them silently; only the new delivery queue can trigger catch-up alerts.
            if let data=try? JSONSerialization.data(withJSONObject:object["pendingNotifications"] ?? []),let values=try? JSONDecoder().decode([PendingNotification].self,from:data){notifications=NotificationDelivery(pending:values)}
            // Pending approvals cannot survive restarting the approval socket.
            notifications.remove(kind:"waiting")
            if let data=try? JSONSerialization.data(withJSONObject:object["replies"] ?? [:]),let values=try? JSONDecoder().decode([String:LatestReply].self,from:data){replies=values}
        }else if let url=Bundle.main.url(forResource:"codex-tasks",withExtension:"json"),let data=try? Data(contentsOf:url),let values=try? JSONDecoder().decode([RemoteTask].self,from:data){tasks=values}
        RemoteEditor.register(tasks,ids:desktopIDs)
        refreshDesktopMetadata(force:true)
        approvals.onChange={[weak self] in self?.publish()}
        for event in [NSWorkspace.didLaunchApplicationNotification,NSWorkspace.didTerminateApplicationNotification] {
            observers.append(NSWorkspace.shared.notificationCenter.addObserver(forName:event,object:nil,queue:.main){[weak self] _ in self?.availability()})
        }
        availability();startHooks()
        draftTimer=Timer.scheduledTimer(withTimeInterval:0.5,repeats:true){[weak self] _ in self?.refreshDraft()}
    }
    func availability(){let next=RemoteEditor.codex() != nil;if next != available {available=next;cancelPreparation();if !next{approvals.cancel()};publish(mode=="codex" && !next ? "Codex 已关闭 · START 普通输入":nil)}}
    func connected(){lastSnapshot=Data();lastTasksSnapshot=Data();availability();publish(includeTasks:true);deliverPendingNotifications()}
    func disconnected(){cancelPreparation();replyTaskID=nil;notifications.remove(kind:"waiting");approvals.cancel();save();onBattery?(lastBattery.isEmpty ? "掌机未连接":"未连接 · 上次 "+lastBattery)}
    func stop(){metadataLifecycle+=1;desktopMetadata.cancel();cancelPreparation();draftTimer?.invalidate();draftTimer=nil;approvals.cancel();listener?.cancel();listener=nil;for observer in observers{NSWorkspace.shared.notificationCenter.removeObserver(observer)};try? FileManager.default.removeItem(at:directory.appendingPathComponent("hook-endpoint.json"))}
    func save(){
        let keep=Set(replies.sorted{$0.value.updated>$1.value.updated}.prefix(64).map{$0.key})
        replies=replies.filter{keep.contains($0.key)}
        let replyValues=replies.mapValues{["text":$0.text,"turn":$0.turn,"updated":$0.updated,"truncated":$0.truncated] as [String:Any]}
        let value:[String:Any]=["desktop_ids":Array(desktopIDs),"desktop_metadata":["generated_at":metadataGeneratedAt as Any? ?? NSNull(),"stale":metadataStale,"failure":metadataFailure?.rawValue as Any? ?? NSNull()],"mode":mode,"lastID":lastID ?? "","tasks":tasks.map{["id":$0.id,"title":$0.title,"status":$0.status,"updated":$0.updated]},"unread":unread,"delivered":Array(delivered).suffix(512).map{$0},"replies":replyValues,"pendingNotifications":notifications.pending.map{["id":$0.id,"task":$0.task,"kind":$0.kind]}]
        if let data=try? JSONSerialization.data(withJSONObject:value){try? data.write(to:stateFile,options:.atomic);try? FileManager.default.setAttributes([.posixPermissions:0o600],ofItemAtPath:stateFile.path)}
    }
    // The selected task owns the main-screen reply, including restored/reconnected sessions.
    private func syncReplyTarget(){
        let id=mode=="codex" ? targetID:nil
        if replyTaskID != id{replyTaskID=id;replyPage=0;replyView=nil}
        if let id=id,let reply=replies[id],let view=replyView,
           view.turn != reply.turn || view.updated != reply.updated{replyPage=0;replyView=nil}
    }
    func refreshDraft(){
        guard !ble.controlToken.isEmpty,ble.controlsAllowed,inputMode=="codex",available,!busy(),!focusInFlight,let task=target,desktopIDs.contains(task.id),let value=RemoteEditor.draftAvailable(task) else{return}
        if draftTaskID != task.id || draftAvailable != value{draftTaskID=task.id;draftAvailable=value;publish()}
    }
    func publish(_ message:String?=nil,includeTasks:Bool=false){
        syncReplyTarget()
        if inputMode != "codex" || draftTaskID != targetID{draftTaskID=targetID;draftAvailable=nil}
        if let message=message{hint=message;onHint?(message)}
        tasks=tasks.filter{UUID(uuidString:$0.id) != nil}
        if tasks.count>512{tasks=Array(tasks.sorted{$0.updated>$1.updated}.prefix(512))}
        desktopIDs=desktopIDs.intersection(Set(tasks.map{$0.id}));RemoteEditor.register(tasks,ids:desktopIDs)
        let sorted=selectableTasks.sorted{$0.updated>$1.updated};let pages=max(1,(sorted.count+11)/12);taskPage=min(taskPage,pages-1)
        let page=sorted.dropFirst(taskPage*12).prefix(12)
        var state:[String:Any]=["targetID":ble.controlsAllowed ? (targetID ?? ""):"","focusPending":focusInFlight,"mode":inputMode,"remoteAllowed":ble.controlsAllowed,"codexAvailable":available&&ble.controlsAllowed,"target":ble.controlsAllowed ? wireText(target?.title ?? "",bytes:256,characters:64):"","unread":ble.controlsAllowed ? unread.count:0,"controlHint":ble.controlsAllowed ? hint:"","taskPage":taskPage,"taskPages":ble.controlsAllowed ? pages:1]
        state["draftAvailable"]=draftAvailable.map{$0 as Any} ?? NSNull()
        state["replyAvailable"]=ble.controlsAllowed && targetID.flatMap{replies[$0]} != nil
        state["approval"]=NSNull()
        if ble.controlsAllowed,let request=approvals.first{state["approval"]=request.wire}
        state["reply"]=NSNull()
        if ble.controlsAllowed,let id=replyTaskID,let reply=replies[id] {
            if replyView?.id != id || replyView?.turn != reply.turn || replyView?.updated != reply.updated{let parts=replySlices(reply.text);replyView=(id,reply.turn,reply.updated,parts.pages,reply.truncated||parts.truncated)}
            if let view=replyView{replyPage=min(replyPage,view.pages.count-1);state["reply"]=["task":id,"title":wireText(tasks.first{$0.id==id}?.title ?? "Codex",bytes:256,characters:64),"text":view.pages[replyPage],"page":replyPage,"pages":view.pages.count,"truncated":view.truncated,"revision":reply.revision(task:id)]}
        }
        // Badges reflect the complete unread ledger, not the five latest alerts.
        let counts=Dictionary(grouping:unread,by:{$0["task"] ?? ""}).mapValues{$0.count}
        let rows:[[String:Any]]=ble.controlsAllowed ? page.map{["id":$0.id,"title":wireText($0.title,bytes:256,characters:64),"status":$0.status,"unread":counts[$0.id] ?? 0] as [String:Any]}:[]
        let tasksSnapshot=(try? JSONSerialization.data(withJSONObject:rows,options:.sortedKeys)) ?? Data()
        // Send changed rows immediately, without repeating the task page for typing telemetry.
        if includeTasks || tasksSnapshot != lastTasksSnapshot{state["tasks"]=rows}
        var object:[String:Any]=["op":"state","state":state]
        var serialized=(try? JSONSerialization.data(withJSONObject:object,options:.sortedKeys)) ?? Data()
        // Preserve the complete pending approval/reply. The task page can be requested again.
        if serialized.count>12288{state.removeValue(forKey:"tasks");object["state"]=state;serialized=(try? JSONSerialization.data(withJSONObject:object,options:.sortedKeys)) ?? Data()}
        if serialized != lastSnapshot{lastSnapshot=serialized;if state["tasks"] != nil{lastTasksSnapshot=tasksSnapshot};ble.control(object)}
        if includeTasks {save()}
    }
    // Metadata updates alter names and provenance only. Hooks retain their live
    // status, timestamps, replies, delivery receipts and unread state.
    @discardableResult private func applyDesktopMetadata(_ result:DesktopTaskMetadataStore.LoadResult)->Bool {
        metadataFailure=result.failure
        guard result.failure==nil else{return false}
        metadataGeneratedAt=result.generatedAt;metadataStale=result.stale
        let ids=Set(result.entries.map{$0.id})
        var changed=ids != desktopIDs
        for entry in result.entries {
            if let index=tasks.firstIndex(where:{$0.id==entry.id}) {
                if tasks[index].title != entry.title{tasks[index].title=entry.title;changed=true}
            }else{
                tasks.append(RemoteTask(id:entry.id,title:entry.title,status:entry.status,updated:entry.updated));changed=true
            }
        }
        desktopIDs=ids
        // A metadata revision invalidates AX leases, while the user's pending
        // intent keeps its generation so it may resume with the canonical title.
        // User mode/task/connection changes still advance generation separately.
        if changed{RemoteEditor.cancelFocus();draftAvailable=nil}
        RemoteEditor.register(tasks,ids:desktopIDs)
        return changed
    }
    private func refreshDesktopMetadata(force:Bool=false,isCurrent:@escaping()->Bool={true},completion:((Bool)->Void)?=nil){
        let lifecycle=metadataLifecycle
        desktopMetadata.refresh(force:force){[weak self] result in
            guard let self=self,self.metadataLifecycle==lifecycle else{completion?(false);return}
            // Check the waiting caller before our own canonical update invalidates
            // focus leases. User cancellation still prevents restarting preparation.
            let callerCurrent=isCurrent()
            self.applyDesktopMetadata(result)
            self.publish(includeTasks:true)
            completion?(callerCurrent)
        }
    }
    private var metadataFailureMessage:String {
        switch metadataFailure {
        case .missingSnapshot:return "桌面任务资料尚未导入 · 请先同步任务列表 · 文字已保留"
        case .invalidSchema:return "桌面任务资料版本不兼容 · 请更新任务资料 · 文字已保留"
        case .invalidSource:return "桌面任务资料来源无效 · 请重新同步 · 文字已保留"
        case .invalidSnapshot,.tooLarge:return "桌面任务资料无效 · 请重新同步 · 文字已保留"
        case .io:return "无法读取桌面任务资料 · 请稍后重试 · 文字已保留"
        case .cancelled:return "输入操作已取消 · 文字已保留"
        case nil:return "此任务未获桌面来源登记 · 请先同步任务列表 · 文字已保留"
        }
    }
    // Cancels only focus retries. The chosen task and recognized text stay intact.
    func cancelPreparation(){generation+=1;focusInFlight=false;lastInputFailure=nil;RemoteEditor.cancelFocus()}
    private(set) var lastInputFailure:String?
    func prepare(completion:@escaping(pid_t?)->Void){
        lastInputFailure=nil
        func failed(_ message:String){lastInputFailure=message;publish(message);completion(nil)}
        if inputMode=="ordinary"{RemoteEditor.cancelFocus();let pid=NSWorkspace.shared.frontmostApplication?.processIdentifier;completion(pid==getpid() ? nil:pid);return}
        guard available else{failed(RemoteEditor.FocusFailure.appUnavailable.message);return}
        guard let task=target else{failed("请先选择目标任务 · 文字已保留");return}
        guard permission() else{failed(RemoteEditor.FocusFailure.permission.message);return}
        let waiting=generation,connection=ble.controlToken
        refreshDesktopMetadata(force:!desktopIDs.contains(task.id),isCurrent:{[weak self] in
            guard let self=self else{return false}
            return waiting==self.generation && self.targetID==task.id && self.inputMode=="codex" && self.ble.controlToken==connection && self.ble.controlsAllowed
        }){[weak self] callerCurrent in
            guard let self=self,callerCurrent,self.targetID==task.id,self.inputMode=="codex",self.ble.controlToken==connection,self.ble.controlsAllowed else{completion(nil);return}
            guard self.desktopIDs.contains(task.id),let canonical=self.target else{self.lastInputFailure=self.metadataFailureMessage;self.publish(self.metadataFailureMessage);completion(nil);return}
            let mine=self.generation
            RemoteEditor.focus(canonical,isCurrent:{[weak self] in
                guard let self=self else{return false}
                return mine==self.generation && self.targetID==canonical.id && self.inputMode=="codex" && self.ble.controlToken==connection && self.ble.controlsAllowed
            }){[weak self] pid in
                guard let self=self else{completion(nil);return}
                guard mine==self.generation,self.targetID==canonical.id,self.inputMode=="codex",self.ble.controlToken==connection else{completion(nil);return}
                if pid==nil{let message=(RemoteEditor.lastFocusFailure ?? .cancelled).message;self.lastInputFailure=message;self.publish(message)}
                completion(pid)
            }
        }
    }
    func message(_ data:Data){
        guard !ble.controlToken.isEmpty,let object=(try? JSONSerialization.jsonObject(with:data)) as? [String:Any],object["token"] as? String==ble.controlToken,let op=object["op"] as? String else{return}
        if op=="battery"{
            let battery=object["battery"] as? [String:Any] ?? [:]
            let level=(battery["percent"] as? Int).flatMap{(0...100).contains($0) ? $0:nil}
            let charging=(battery["charging"] as? Bool)==true
            let percent=level.map{String($0)+"%"} ?? "—%"
            lastBattery=percent;onBattery?("Brick · "+percent+(charging ? " · 充电":""));onBatteryLevel?(level,charging);return
        }
        guard ble.controlsAllowed else{return}
        // Delivery receipts stay responsive during recording and never execute
        // editing, task selection or approval actions.
        if op=="notifyAck"{
            if let event=object["event"] as? String,notifications.acknowledge(event:event){save()}
            return
        }
        availability()
        if op=="approval"{
            guard !busy(),let id=object["id"] as? String,let choice=object["arg"] as? String else{return}
            if !approvals.decide(id,choice:choice,connection:ble.controlToken){publish("审批已结束或需在 Mac 查看完整内容")}
            return
        }
        if op=="reply"{
            guard !busy() else{return}
            let action=object["arg"] as? String ?? "open"
            if action=="seen" {
                guard inputMode=="codex",let id=targetID,let reply=replies[id],object["revision"] as? String==reply.revision(task:id) else{return}
                unread.removeAll{$0["task"]==id && $0["kind"]=="completed"}
                notifications.remove(task:id,kind:"completed");save();publish();return
            }
            syncReplyTarget()
            if action=="close"{replyPage=0;publish();return}
            if action=="open"{guard let id=targetID,replies[id] != nil else{publish("尚未收到此任务的上一段回复");return};replyTaskID=id;replyPage=0}
            else if action=="next"{replyPage+=1}
            else if action=="prev"{replyPage=max(0,replyPage-1)}
            else{return}
            publish();return
        }
        guard !busy() else{return}
        if op=="mode"{
            if object["arg"] as? String=="ordinary"{cancelPreparation();mode="ordinary";targetID=nil;replyTaskID=nil;publish("");save();return}
            guard available else{publish("请先打开 Codex");return};cancelPreparation();mode="codex";targetID=lastID;publish(target==nil ? "按 Y 选择任务":"");save();return
        }
        if op=="tasks"{
            let action=object["arg"] as? String ?? "open"
            let pages=max(1,(selectableTasks.count+11)/12)
            if action=="next"{taskPage=(taskPage+1)%pages;publish(includeTasks:true)}
            else if action=="prev"{taskPage=(taskPage+pages-1)%pages;publish(includeTasks:true)}
            else{refreshDesktopMetadata(force:true)}
            return
        }
        if op=="choose"{
            guard mode=="codex",available,let id=object["id"] as? String,desktopIDs.contains(id),tasks.contains(where:{$0.id==id}) else{publish("任务未获桌面来源登记 · 请同步任务列表");return}
            cancelPreparation();targetID=id;lastID=id;replyPage=0;unread.removeAll{$0["task"]==id};notifications.remove(task:id);focusInFlight=true;save();publish("正在打开任务…")
            let mine=generation
            prepare{[weak self] pid in
                guard let self=self,mine==self.generation,self.targetID==id else{return}
                self.focusInFlight=false
                if pid != nil{self.publish("")}else{self.publish()}
            };return
        }
        if op=="read"{unread.removeAll();notifications.remove();save();publish();return}
        guard !focusInFlight else{return}
        guard permission() else{publish("请在 Mac 允许辅助功能权限");return}
        if mode=="codex",(!available||target==nil){publish("任务不可用 · START 返回普通输入");return}
        if op=="submit",mode != "codex" {return}
        focusInFlight=true
        let mine=generation
        prepare{[weak self] pid in
            guard let self=self,mine==self.generation else{return};self.focusInFlight=false
            guard let pid=pid else{self.publish();return}
            guard NSWorkspace.shared.frontmostApplication?.processIdentifier==pid else{self.publish("前台应用已变化 · 文字已保留");return}
            let ok:Bool
            if op=="submit",let task=self.target {ok=RemoteEditor.submit(task,pid:pid)}
            else {
                let field=self.mode=="codex" ? RemoteEditor.preparedFocused(pid):nil
                if self.mode=="codex",field==nil{self.publish("目标输入框已变化 · 操作未执行");return}
                ok=RemoteEditor.edit(RemoteEditor.editingOperation(op,codex:self.mode=="codex"),selection:object["arg"] as? String=="select",pid:pid,expectedField:field)
            }
            self.publish(ok ? (op=="submit" ? "已发送":""):"未执行 · 无法确认输入框")
        }
    }
    private func sendNotification(_ batch:NotificationBatch,silent:Bool=false){
        ble.control(["op":"notify","event":batch.event,"events":batch.events,"kind":batch.kind,"silent":silent])
    }
    private func deliverPendingNotifications(){
        guard ble.controlsAllowed,!ble.controlToken.isEmpty,let batch=notifications.batch() else{return}
        // Reopening/reconnecting synchronizes history; only live arrivals buzz.
        sendNotification(batch,silent:true)
    }
    private func receiveHook(_ object:[String:Any],socketID:String="",approvalReply:(([String:Any])->Void)?=nil){
        guard object["token"] as? String==hookToken,let id=object["session_id"] as? String,UUID(uuidString:id) != nil,let event=object["hook_event_name"] as? String else{approvalReply?([:]);return}
        let now=object["at"] as? Double ?? Date().timeIntervalSince1970
        let turn=object["turn_id"] as? String ?? ""
        let turnKey=id+":"+turn
        if event=="Interrupt" && !turn.isEmpty{cancelledTurns.insert(turnKey)}
        if !turn.isEmpty && (cancelledTurns.contains(turnKey) && event != "Interrupt" || finishedTurns.contains(turnKey) && event != "Stop"){approvalReply?([:]);return}
        if event=="Interrupt"||event=="SessionEnd" {notifications.remove(task:id,kind:"waiting");approvals.cancel{$0.task==id && (turn.isEmpty||$0.turn==turn)}}
        if !tasks.contains(where:{$0.id==id}){let cwd=object["cwd"] as? String ?? "Codex";tasks.append(RemoteTask(id:id,title:URL(fileURLWithPath:cwd).lastPathComponent,status:"idle",updated:now))}
        guard let index=tasks.firstIndex(where:{$0.id==id}) else{approvalReply?([:]);return}
        tasks[index].updated=now
        if event=="UserPromptSubmit"{tasks[index].status="running"}
        if event=="SessionEnd"{tasks[index].status="idle"}
        var kind=""
        if event=="Stop" && object["stop_hook_active"] as? Bool != true{
            if !finishedTurns.contains(turnKey),let text=object["last_assistant_message"] as? String,!text.isEmpty,
               now >= (replies[id]?.updated ?? 0) {
                let clipped=text.unicodeScalars.count>12000
                replies[id]=LatestReply(text:String(text.unicodeScalars.prefix(12000)),turn:turn,updated:now,truncated:clipped || object["reply_truncated"] as? Bool == true)
                if replyTaskID==id{replyPage=0}
            }
            tasks[index].status="completed";kind="completed";finishedTurns.insert(turnKey)
            notifications.remove(task:id,kind:"waiting")
            approvals.cancel{$0.task==id && (turn.isEmpty||$0.turn==turn)}
        }
        if event=="PermissionRequest"{
            tasks[index].status="waiting";kind="waiting"
            if let respond=approvalReply {
                guard ble.controlsAllowed,!ble.controlToken.isEmpty,!turn.isEmpty,let requestID=object["request_id"] as? String,
                      let details=object["permission"] as? [String:Any],let tool=details["tool"] as? String,!tool.isEmpty,
                      let summary=details["summary"] as? String,!summary.isEmpty,summary.count<=1200,summary.utf8.count<=4000,tool.count<=80,tool.utf8.count<=128 else{respond([:]);publish(includeTasks:true);return}
                let connection=ble.controlToken
                let expires=Date().timeIntervalSince1970+90
                let request=ApprovalBroker.Request(id:requestID,task:id,turn:turn,connection:connection,owner:socketID,title:tasks[index].title,
                    tool:tool,summary:summary,expires:expires,allowAvailable:details["allowAvailable"] as? Bool == true,finish:{[weak self] response in
                        respond(response)
                        guard let self=self,!self.approvals.requests.contains(where:{$0.task==id}) else{return}
                        self.notifications.remove(task:id,kind:"waiting")
                        if response["decision"] != nil{
                            if let index=self.tasks.firstIndex(where:{$0.id==id}),self.tasks[index].status=="waiting"{self.tasks[index].status="running"}
                            self.unread.removeAll{$0["task"]==id&&$0["kind"]=="waiting"}
                        }
                        self.save()
                    })
                if approvals.add(request){DispatchQueue.main.asyncAfter(deadline:.now()+90){[weak self] in self?.approvals.expire()}}
            }
        }else{approvalReply?([:])}
        if event=="Interrupt"{tasks[index].status="idle"}
        if !kind.isEmpty,let turn=object["turn_id"] as? String,!turn.isEmpty{
            let key=id+":"+turn+":"+event
            if !delivered.contains(key){
                delivered.insert(key);unread.append(["id":key,"task":id,"title":tasks[index].title,"kind":kind]);unread=Array(unread.suffix(100))
                if let eventID=notifications.enqueue(sourceID:key,task:id,kind:kind){
                    save()
                    if ble.controlsAllowed,!ble.controlToken.isEmpty,let batch=notifications.batch(ids:[eventID]){sendNotification(batch)}
                }
            }
        }
        publish(includeTasks:true)
    }
    func verify() -> Bool {
        guard RemoteEditor.editingOperation("enter",codex:true)=="newline",RemoteEditor.editingOperation("enter",codex:false)=="enter",RemoteEditor.editingOperation("space",codex:true)=="space" else{return false}
        let id="00000000-0000-4000-8000-000000000001"
        func event(_ name:String,_ turn:String="turn1",text:String?=nil){
            var object:[String:Any]=["token":hookToken,"session_id":id,"turn_id":turn,"hook_event_name":name,"cwd":"/tmp/test-project"]
            if let text=text{object["last_assistant_message"]=text}
            receiveHook(object)
        }
        receiveHook(["token":"invalid","session_id":id,"hook_event_name":"Stop"])
        guard tasks.isEmpty else{return false}
        event("UserPromptSubmit");guard tasks.first?.status=="running" else{return false}
        event("Stop",text:"First complete reply");event("Stop",text:"Duplicate must not replace reply");guard unread.count==1,tasks.first?.status=="completed",replies[id]?.text=="First complete reply" else{return false}
        event("PermissionRequest");guard tasks.first?.status=="completed",unread.count==1 else{return false}
        event("UserPromptSubmit","turn2");event("Interrupt","turn2");event("Stop","turn2")
        guard tasks.first?.status=="idle",unread.count==1 else{return false}
        event("UserPromptSubmit","turn3");event("Stop","turn3",text:"Second complete reply")
        guard replies[id]?.text=="Second complete reply",unread.count==2 else{return false}
        mode="codex";targetID=id;draftTaskID=id;draftAvailable=true;publish()
        if ble.controlsAllowed{guard draftAvailable==true else{return false}}else{guard draftAvailable==nil else{return false}}
        syncReplyTarget()
        guard replyTaskID==id,replyPage==0 else{return false}
        replyPage=2;syncReplyTarget();guard replyPage==2 else{return false}
        replyView=(id,"old-turn",0,["stale page"],false);syncReplyTarget()
        guard replyPage==0,replyView==nil else{return false}
        targetID="00000000-0000-4000-8000-000000000002";publish();guard draftAvailable==nil else{return false};syncReplyTarget()
        guard replyTaskID==targetID,replyPage==0 else{return false}
        mode="ordinary";targetID=nil;syncReplyTarget();guard replyTaskID==nil else{return false}
        let pages=replySlices(String(repeating:"😀",count:1100))
        guard pages.pages.count==3,pages.pages.allSatisfy({$0.utf8.count<=2000&&$0.count<=500}),pages.pages.joined()==String(repeating:"😀",count:1100),!pages.truncated else{return false}
        let oversized=replySlices("a"+String(repeating:"\u{301}",count:2100))
        guard oversized.truncated,oversized.pages.allSatisfy({$0.utf8.count<=2000}) else{return false}
        let broker=ApprovalBroker();var answers:[[String:Any]]=[]
        func request(_ number:Int,_ connection:String="connection-1",expires:Double=120,allow:Bool=true)->ApprovalBroker.Request {
            ApprovalBroker.Request(id:String(format:"00000000-0000-4000-8000-%012d",number),task:id,turn:"approval-turn",connection:connection,owner:"socket-\(number)",title:"Fixture",tool:"Bash",summary:"printf test",expires:expires,allowAvailable:allow,finish:{answers.append($0)})
        }
        let first=request(1),second=request(2)
        guard broker.add(first,now:100),broker.add(second,now:100),!broker.decide(second.id,choice:"allow",connection:"connection-1",now:100),
              !broker.decide(first.id,choice:"allow",connection:"old-connection",now:100),answers.isEmpty else{return false}
        guard broker.decide(first.id,choice:"allow",connection:"connection-1",now:100),answers.count==1,answers[0]["decision"] as? String=="allow",!broker.decide(first.id,choice:"allow",connection:"connection-1",now:100) else{return false}
        broker.expire(now:121);guard answers.count==2,answers[1].isEmpty,broker.requests.isEmpty else{return false}
        let unsafe=request(3,expires:150,allow:false)
        guard broker.add(unsafe,now:130),!broker.decide(unsafe.id,choice:"allow",connection:"connection-1",now:130),broker.decide(unsafe.id,choice:"deny",connection:"connection-1",now:130),answers.last?["decision"] as? String=="deny" else{return false}
        guard broker.add(request(4,expires:180),now:150),broker.add(request(5,"connection-2",expires:180),now:150) else{return false}
        broker.cancel{$0.owner=="socket-4"};guard broker.requests.count==1,broker.first?.connection=="connection-2",answers.last?.isEmpty==true else{return false}
        broker.cancel();guard broker.requests.isEmpty,answers.last?.isEmpty==true else{return false}
        let other=MicControl(ble,directory:directory);other.start();defer{other.stop()}
        let permissions=(try? FileManager.default.attributesOfItem(atPath:stateFile.path)[.posixPermissions]) as? Int
        return other.tasks.count==1 && other.unread.count==2 && other.replies[id]?.text=="Second complete reply" && permissions==0o600
    }
    private func startHooks(){
        let parameters=NWParameters.tcp
        parameters.requiredLocalEndpoint = .hostPort(host:"127.0.0.1",port:.any)
        guard let service=try? NWListener(using:parameters) else{return};listener=service
        service.stateUpdateHandler={[weak self,weak service] state in
            guard let self=self,case .ready=state,let port=service?.port else{return}
            let object:[String:Any]=["port":port.rawValue,"token":self.hookToken]
            if let data=try? JSONSerialization.data(withJSONObject:object){let file=self.directory.appendingPathComponent("hook-endpoint.json");try? data.write(to:file,options:.atomic);try? FileManager.default.setAttributes([.posixPermissions:0o600],ofItemAtPath:file.path)}
        }
        service.newConnectionHandler={[weak self] connection in
            // Leave room for Stop/Interrupt while up to eight approval sockets wait.
            guard let self=self,self.connections.count<16 else{connection.cancel();return}
            let identity=ObjectIdentifier(connection),socketID=UUID().uuidString;self.connections.insert(identity);connection.start(queue:self.queue)
            var finished=false,input=Data(),requestID:String?
            var deadline:DispatchWorkItem?
            func finish(_ response:[String:Any]) {
                guard !finished else{return};finished=true;deadline?.cancel();self.connections.remove(identity)
                if let id=requestID{DispatchQueue.main.async{self.approvals.cancel{$0.id==id&&$0.owner==socketID}}}
                let packet=((try? JSONSerialization.data(withJSONObject:response)) ?? Data("{}".utf8))+Data([10])
                connection.send(content:packet,completion:.contentProcessed{_ in connection.cancel()})
            }
            func timeout(_ seconds:Double){deadline?.cancel();let work=DispatchWorkItem{finish([:])};deadline=work;self.queue.asyncAfter(deadline:.now()+seconds,execute:work)}
            func read(){connection.receive(minimumIncompleteLength:1,maximumLength:4096){data,_,complete,error in
                guard !finished else{return}
                if let data=data{input.append(data)}
                if let end=input.firstIndex(of:10){
                    guard end<=65536,let object=(try? JSONSerialization.jsonObject(with:input.prefix(upTo:end))) as? [String:Any] else{finish([:]);return}
                    if object["hook_event_name"] as? String=="PermissionRequest"{requestID=object["request_id"] as? String;timeout(92)}else{timeout(2)}
                    DispatchQueue.main.async{self.receiveHook(object,socketID:socketID,approvalReply:{response in self.queue.async{finish(response)}})}
                    // Peer exit/timeout cancels its pending prompt. Never retain a stale request.
                    connection.receive(minimumIncompleteLength:1,maximumLength:1){_,_,complete,error in if complete||error != nil{finish([:])}}
                }else if input.count>65536||complete||error != nil{finish([:])}else{read()}
            }}
            timeout(2);read()
        };service.start(queue:queue)
    }
}
