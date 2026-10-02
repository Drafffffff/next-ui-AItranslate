import Foundation

// Concatenated with Control.swift to exercise its pure policy boundaries.
// No AX call, app launch, keyboard event, BLE connection or user state is used.
final class BrickBluetooth {
    var controlsAllowed=true
    var controlToken="synthetic-session"
    var packets=[[String:Any]]()
    func control(_ object:[String:Any]){packets.append(object)}
}

extension MicControl {
    fileprivate func configureFocusFixture(_ id:String){
        mode="codex";targetID=id;lastID=id;focusInFlight=true
        tasks=[RemoteTask(id:id,title:"Synthetic Task",status:"idle",updated:0)]
    }
    fileprivate var fixtureSelectedID:String? {targetID}
    fileprivate var fixturePending:Bool {focusInFlight}
    fileprivate var fixtureGeneration:Int {generation}
    fileprivate var fixtureDesktopIDs:Set<String>{desktopIDs}
    fileprivate var fixtureTasks:[RemoteTask]{tasks}
    fileprivate var fixtureSelectable:[RemoteTask]{selectableTasks}
    fileprivate var fixtureMetadataMessage:String{metadataFailureMessage}
    fileprivate var fixturePreservedHistory:Bool{
        replies.values.contains{$0.text=="Fixture reply"} && unread.count==1 && delivered.contains("fixture-delivery") && notifications.pending.count==1
    }
    fileprivate func configureMetadataFixture(_ id:String,hook:String){
        configureFocusFixture(id)
        tasks=[RemoteTask(id:id,title:"Project folder",status:"running",updated:20),RemoteTask(id:hook,title:"CLI only",status:"completed",updated:30)]
        replies[id]=LatestReply(text:"Fixture reply",turn:"fixture-turn",updated:21,truncated:false)
        unread=[["id":"fixture-unread","task":id,"title":"Project folder","kind":"completed"]]
        delivered=["fixture-delivery"]
        notifications=NotificationDelivery(pending:[PendingNotification(id:NotificationDelivery.eventID("fixture-pending"),task:id,kind:"completed")])
    }
    fileprivate func fixtureApplyMetadata(_ result:DesktopTaskMetadataStore.LoadResult)->Bool{applyDesktopMetadata(result)}
    fileprivate func fixtureRefreshMetadata(completion:@escaping(Bool)->Void){
        let mine=generation,id=targetID,token=ble.controlToken
        refreshDesktopMetadata(force:true,isCurrent:{[weak self] in
            guard let self=self else{return false}
            return mine==self.generation && self.targetID==id && self.inputMode=="codex" && self.ble.controlToken==token && self.ble.controlsAllowed
        },completion:completion)
    }
    fileprivate func fixtureOrdinary(){cancelPreparation();mode="ordinary";targetID=nil}
}

@main enum ComposerFocusHarness {
    struct SyntheticNode {
        var role:String
        var identifier=""
        var description=""
        var children=[Int]()
        var visible:[Int]?
        var candidate:ComposerCandidateFacts?
    }
    static func scanFixture(_ graph:[SyntheticNode],limit:Int)->(choice:ComposerCandidateChoice,visited:Int){
        let budget=ComposerScanBudget(deadline:100,maxReads:limit,maxNodes:limit,now:{0})
        var pending=[0],facts=[ComposerCandidateFacts](),visited=0
        while let index=pending.popLast(){
            guard budget.visit() else{break}
            visited+=1
            let node=graph[index]
            if let candidate=node.candidate{facts.append(candidate)}
            if ComposerTraversalPolicy.excludesSubtree(role:node.role,identifier:node.identifier,description:node.description,hidden:false){continue}
            pending.append(contentsOf:ComposerTraversalPolicy.preferVisibleChildren(node.visible,fallback:node.children))
        }
        return (ComposerCandidatePolicy.select(facts,scanComplete:budget.check() && pending.isEmpty),visited)
    }
    struct SyntheticIdentityNode {
        var role:String
        var label=""
        var selected=false
        var ariaCurrent=""
        var children=[Int]()
    }
    static func identityFixture(_ graph:[SyntheticIdentityNode],title:String,limit:Int)->(found:Bool,visited:Int,stop:ComposerScanBudget.Stop?) {
        let budget=ComposerScanBudget(deadline:100,maxReads:limit+1,maxNodes:limit+1,now:{0})
        var pending=[0],offset=0,visited=0
        while offset<pending.count && visited<limit {
            guard budget.visit() else{break}
            let node=graph[pending[offset]];offset+=1;visited+=1
            if ComposerCandidatePolicy.currentSelection(selected:node.selected,ariaCurrent:node.ariaCurrent),node.label==title{return (true,visited,budget.stop)}
            if ComposerTraversalPolicy.excludesIdentitySubtree(role:node.role,identifier:"",description:"",hidden:false){continue}
            pending.append(contentsOf:node.children)
        }
        if offset<pending.count{budget.truncate()}
        return (false,visited,budget.stop)
    }
    static func waitFixture(_ finished:()->Bool){
        let deadline=Date().addingTimeInterval(3)
        while !finished(),Date()<deadline{RunLoop.current.run(until:Date().addingTimeInterval(0.005))}
        check(finished(),"synthetic metadata callback completes within its bounded test wait")
    }
    static func check(_ condition:Bool,_ message:String){
        if !condition{fputs("FAIL: \(message)\n",stderr);exit(1)}
    }
    static func main(){
        let composer=ComposerCandidateFacts(role:kAXTextAreaRole,hints:["Ask Codex"],valueWritable:false,focused:false,focusWritable:true)
        check(ComposerCandidatePolicy.select([composer]) == .selected(0),"read-only AXValue must not exclude a focusable composer")
        var alreadyFocused=composer;alreadyFocused.focused=true;alreadyFocused.focusWritable=false
        check(ComposerCandidatePolicy.select([alreadyFocused]) == .selected(0),"already focused composer must not require an AX setter")
        var readOnly=composer;readOnly.explicitlyReadOnly=true
        check(ComposerCandidatePolicy.select([readOnly]) == .none,"explicitly read-only viewers cannot receive input")
        let search=ComposerCandidateFacts(role:kAXTextFieldRole,subrole:"AXSearchField",hints:["Search messages"])
        let rename=ComposerCandidateFacts(role:kAXTextFieldRole,hints:["Rename task"])
        check(ComposerCandidatePolicy.select([search,rename,composer]) == .selected(2),"search/name fields cannot compete with the composer")
        let taskTitle=ComposerCandidateFacts(role:kAXTextFieldRole,hints:["task name"])
        check(ComposerCandidatePolicy.select([taskTitle]) == .none,"ask must not match the word task")
        let transcript=ComposerCandidateFacts(role:kAXTextAreaRole,hints:["Message history"])
        check(ComposerCandidatePolicy.select([transcript]) == .none,"a transcript label is not a composer")
        let anonymous=ComposerCandidateFacts(role:kAXTextAreaRole)
        check(ComposerCandidatePolicy.select([anonymous]) == .none,"an arbitrary text area has insufficient evidence")
        var local=anonymous;local.localSendAction=true;local.localTextEditorCount=1
        check(ComposerCandidatePolicy.select([local]) == .selected(0),"one editor in a small send container is a supported structural candidate")
        local.localTextEditorCount=2
        check(ComposerCandidatePolicy.select([local]) == .none,"send action in a container with several editors is insufficient")
        var inherited=anonymous;inherited.insideComposer=true
        check(ComposerCandidatePolicy.select([inherited]) == .selected(0),"explicit composer container supplies context")
        var disabled=composer;disabled.enabled=false
        check(ComposerCandidatePolicy.select([disabled]) == .disabled,"disabled composer reports a specific failure")
        check(ComposerCandidatePolicy.select([composer,inherited]) == .ambiguous,"multiple supported composers require disambiguation")
        var nested=inherited;nested.ancestorCandidates=[0]
        check(ComposerCandidatePolicy.select([composer,nested]) == .selected(0),"parent and focused text leaf are one composer")
        var deepest=nested;deepest.ancestorCandidates=[1,0]
        check(ComposerCandidatePolicy.select([composer,nested,deepest]) == .selected(0),"the whole nested editable chain collapses to one composer")
        check(ComposerCandidatePolicy.select([composer,nested,inherited]) == .ambiguous,"an independent sibling remains ambiguous after nested folding")
        check(ComposerCandidatePolicy.select([disabled,nested]) == .selected(1),"an enabled nested child can represent a disabled outer editor")
        check(ComposerCandidatePolicy.select([composer],scanComplete:false) == .incomplete,"truncated scans never prove a unique composer")
        let history=[SyntheticNode(role:kAXGroupRole,children:[1,2]),SyntheticNode(role:kAXScrollAreaRole,identifier:"message-history",children:Array(3..<10003)),SyntheticNode(role:kAXTextAreaRole,candidate:composer)]+Array(repeating:SyntheticNode(role:kAXStaticTextRole),count:10000)
        let longHistory=scanFixture(history,limit:32)
        check(longHistory.choice == .selected(0) && longHistory.visited==3,"ten thousand explicit history nodes do not consume composer traversal budget")
        var twoComposers=history
        twoComposers[0].children.append(twoComposers.count)
        twoComposers.append(SyntheticNode(role:kAXTextAreaRole,candidate:inherited))
        check(scanFixture(twoComposers,limit:32).choice == .ambiguous,"a second independent composer remains ambiguous after history exclusion")
        let staticHistory=[SyntheticNode(role:kAXGroupRole,children:[1,2]),SyntheticNode(role:kAXStaticTextRole,children:Array(3..<10003)),SyntheticNode(role:kAXTextAreaRole,candidate:composer)]+Array(repeating:SyntheticNode(role:kAXGroupRole),count:10000)
        check(scanFixture(staticHistory,limit:32).visited==3,"terminal static text never expands decorative history descendants")
        var unknown=history;unknown[1].identifier="unknown-scroll-container"
        check(scanFixture(unknown,limit:32).choice == .incomplete,"an unknown scroll scope is retained and its truncation cannot prove uniqueness")
        check(ComposerTraversalPolicy.preferVisibleChildren([Int](),fallback:[1,2]).isEmpty,"explicit empty visible children cannot fall back to hidden children")
        check(ComposerTraversalPolicy.preferVisibleChildren(nil as [Int]?,fallback:[1,2])==[1,2],"missing visible-children attribute falls back to ordinary children")
        check(!ComposerTraversalPolicy.excludesSubtree(role:kAXScrollAreaRole,identifier:"main",description:"",hidden:false),"unlabelled scroll areas are never discarded")
        var tick=10.0,current=true
        let budget=ComposerScanBudget(deadline:11,maxReads:2,maxNodes:2,current:{current},now:{tick})
        check(budget.read() && budget.visit(),"live scan permits reads and traversal")
        current=false
        check(!budget.read() && budget.stop == .cancelled,"context cancellation stops every subsequent AX read")
        let timed=ComposerScanBudget(deadline:11,now:{tick})
        tick=11
        check(!timed.visit() && timed.stop == .deadline,"deadline is checked before traversing any node")
        let readBound=ComposerScanBudget(deadline:12,maxReads:1,now:{tick})
        check(readBound.read() && !readBound.read() && readBound.stop == .truncated,"read limit is explicit incompleteness")
        let nodeBound=ComposerScanBudget(deadline:12,maxNodes:1,now:{tick})
        check(nodeBound.visit() && !nodeBound.visit() && nodeBound.stop == .truncated,"node limit is explicit incompleteness")
        let transient=ComposerScanBudget(deadline:12,now:{tick})
        transient.failAX(Int(AXError.cannotComplete.rawValue))
        check(transient.recoverTransientAX() && transient.read(),"transient messaging failure can retry within the original deadline")
        transient.failAX(Int(AXError.invalidUIElement.rawValue))
        check(!transient.recoverTransientAX(),"invalid UI objects cannot be revived as complete input scopes")
        let oneButton=[ComposerSendFacts(identity:7,enabled:true,explicitSend:true),ComposerSendFacts(identity:7,enabled:true,explicitSend:true)]
        check(ComposerCandidatePolicy.uniqueSend(oneButton,scanComplete:true)==0,"one Send button exposed by two ancestor containers is deduplicated")
        check(ComposerCandidatePolicy.uniqueSend(oneButton,scanComplete:false)==nil,"incomplete send scans cannot press a button")
        check(ComposerCandidatePolicy.uniqueSend(oneButton+[ComposerSendFacts(identity:8,enabled:true,explicitSend:true)],scanComplete:true)==nil,"independent Send buttons are refused")
        check(ComposerCandidatePolicy.focusConfirmed(frontmost:true,focusedEditable:true,sameElement:true,ancestorMatch:false),"direct focused field is confirmed")
        check(ComposerCandidatePolicy.focusConfirmed(frontmost:true,focusedEditable:true,sameElement:false,ancestorMatch:true),"editable focused child inside the composer is confirmed")
        check(!ComposerCandidatePolicy.focusConfirmed(frontmost:true,focusedEditable:true,sameElement:false,ancestorMatch:false),"unrelated focused search field is not confirmed")
        check(!ComposerCandidatePolicy.focusConfirmed(frontmost:false,focusedEditable:true,sameElement:true,ancestorMatch:false),"background app never authorizes text insertion")
        check(!ComposerCandidatePolicy.focusConfirmed(frontmost:true,focusedEditable:false,sameElement:true,ancestorMatch:false),"read-only focused leaf cannot be used")
        check(ComposerCandidatePolicy.headingSemantics(role:"AXHeading",hidden:false,labelMatches:true),"fresh visible matching heading can be counted diagnostically")
        check(!ComposerCandidatePolicy.headingSemantics(role:"AXHeading",hidden:true,labelMatches:true),"hidden retained headings are not counted as live semantic headings")
        check(!ComposerCandidatePolicy.headingSemantics(role:kAXStaticTextRole,hidden:false,labelMatches:true),"reused node must still have the heading role")
        check(ComposerCandidatePolicy.headingSemantics(role:kAXStaticTextRole,hidden:false,labelMatches:true,subrole:"AXHeading"),"semantic heading subrole supports non-heading labels")
        check(ComposerCandidatePolicy.headingSemantics(role:kAXStaticTextRole,hidden:false,labelMatches:true,roleDescription:"heading"),"semantic heading role description supports non-heading labels")
        check(!ComposerCandidatePolicy.headingSemantics(role:kAXStaticTextRole,hidden:true,labelMatches:true,roleDescription:"标题"),"localized semantic headings still reject hidden labels")
        check(ComposerCandidatePolicy.identityLabelProof(hidden:false,labelMatches:true),"fresh visible row/header labels can prove task title")
        check(!ComposerCandidatePolicy.identityLabelProof(hidden:true,labelMatches:true),"hidden stale row/header labels cannot prove task title")
        check(!ComposerTraversalPolicy.crossesDocument(role:"AXWebArea",isScopeRoot:true),"the proof's own chat WebArea remains in composer scope")
        check(ComposerTraversalPolicy.crossesDocument(role:"AXWebArea",isScopeRoot:false),"nested workspace browser WebAreas cannot supply a composer for the parent document identity")
        check(!ComposerTraversalPolicy.crossesDocument(role:kAXGroupRole,isScopeRoot:false),"ordinary chat editor ancestors stay in document scope")
        check(ComposerCandidatePolicy.currentSelection(selected:true,ariaCurrent:""),"AXSelected proves current selection")
        check(ComposerCandidatePolicy.currentSelection(selected:false,ariaCurrent:"page") && ComposerCandidatePolicy.currentSelection(selected:false,ariaCurrent:"TRUE"),"public ARIA current page/true markers prove current selection")
        check(!ComposerCandidatePolicy.currentSelection(selected:false,ariaCurrent:"false") && !ComposerCandidatePolicy.currentSelection(selected:false,ariaCurrent:""),"absent/false current marker cannot turn an ordinary sidebar title into identity proof")
        check(ComposerTraversalPolicy.excludesSubtree(role:kAXButtonRole,identifier:"",description:"",hidden:false) && !ComposerTraversalPolicy.excludesIdentitySubtree(role:kAXButtonRole,identifier:"",description:"",hidden:false),"identity scan keeps button child labels without expanding composer button traversal")
        check(!ComposerTraversalPolicy.excludesIdentitySubtree(role:"AXLink",identifier:"",description:"",hidden:false),"a current sidebar link can expose its child label")
        check(ComposerTraversalPolicy.excludesIdentitySubtree(role:kAXButtonRole,identifier:"",description:"",hidden:true),"hidden buttons remain excluded from identity discovery")
        let identityID="01234567-89ab-4cde-8123-456789abcdef"
        let route=URL(string:"codex://threads/"+identityID)!
        check(ComposerCandidatePolicy.exactUUID("thread-"+identityID.uppercased()+"-header",id:identityID),"UUID tokens in namespaced AXIdentifiers are case insensitive")
        check(!ComposerCandidatePolicy.exactUUID("f"+identityID,id:identityID) && !ComposerCandidatePolicy.exactUUID(identityID+"a",id:identityID),"partial UUID tokens cannot match extended hexadecimal identifiers")
        check(!ComposerCandidatePolicy.exactUUID("01234567-89ab-4cde-8123-456789abcdee",id:identityID),"a different task UUID cannot prove the target")
        check(ComposerCandidatePolicy.routeText(route as NSURL)==route.absoluteString,"public CFURL-compatible AXURL values preserve the document route")
        check(ComposerCandidatePolicy.webDocumentProof(role:"AXWebArea",route:route as NSURL,id:identityID),"current web document URL can supply exact task identity")
        check(!ComposerCandidatePolicy.webDocumentProof(role:"AXLink",route:route as NSURL,id:identityID),"a sidebar link URL is not evidence of the current task")
        check(!ComposerCandidatePolicy.webDocumentProof(role:"AXWebArea",route:URL(string:"https://example.invalid/browser?thread="+identityID)! as NSURL,id:identityID),"a workspace browser query UUID is not a supported task route")
        check(!ComposerCandidatePolicy.webDocumentProof(role:"AXWebArea",route:URL(string:"https://example.invalid/threads/"+identityID)! as NSURL,id:identityID),"an external path with a UUID is not a supported task route")
        check(!ComposerCandidatePolicy.webDocumentProof(role:"AXWebArea",route:URL(string:"codex://threads/"+identityID+"?other=1")! as NSURL,id:identityID),"route query cannot masquerade as the exact current task document")
        check(!ComposerCandidatePolicy.webDocumentProof(role:"AXWebArea",route:URL(string:"codex://threads/"+identityID+"/other")! as NSURL,id:identityID),"an extended route path is not the target thread document")
        check(!ComposerCandidatePolicy.webDocumentProof(role:"AXWebArea",route:"codex://threads/01234567-89ab-4cde-8123-456789abcdee" as CFString,id:identityID),"web route must match the exact target UUID")
        var identityGraph=[SyntheticIdentityNode(role:kAXGroupRole,children:Array(1...640))]+Array(repeating:SyntheticIdentityNode(role:kAXStaticTextRole),count:639)+[SyntheticIdentityNode(role:kAXButtonRole,ariaCurrent:"page",children:[641]),SyntheticIdentityNode(role:kAXStaticTextRole,label:"Target")]
        // The production selected-row label scan follows button descendants;
        // make the same button's own label available to this pure traversal test.
        identityGraph[640].label="Target"
        let narrowIdentity=identityFixture(identityGraph,title:"Target",limit:240)
        check(!narrowIdentity.found && narrowIdentity.stop == .truncated,"the former 240-node identity range cannot silently claim a complete scan")
        let wideIdentity=identityFixture(identityGraph,title:"Target",limit:2048)
        check(wideIdentity.found && wideIdentity.visited>240 && wideIdentity.stop==nil,"bounded diagnostic traversal reaches a current row after 240 nodes")
        identityGraph[640].ariaCurrent="false"
        let ordinarySidebar=identityFixture(identityGraph,title:"Target",limit:2048)
        check(!ordinarySidebar.found && ordinarySidebar.stop==nil,"an exact title in an unselected sidebar button is rejected")
        let identityPartial=[SyntheticIdentityNode(role:kAXGroupRole,children:Array(1...3000))]+Array(repeating:SyntheticIdentityNode(role:kAXGroupRole),count:3000)
        check(identityFixture(identityPartial,title:"Target",limit:2048).stop == .truncated,"identity range cap is explicit incompleteness")
        check(RemoteEditor.submitKeyCode==36,"explicit handheld submit maps to Return without a Send-button search")
        check(ComposerCandidatePolicy.exactComposerLabel(["Do anything"]) && ComposerCandidatePolicy.exactComposerLabel(["随心输入"]),"focused-editor shortcut recognizes exact current composer accessible names")
        check(!ComposerCandidatePolicy.exactComposerLabel(["Chat title","Do anything later","Search"]),"focused-editor shortcut rejects rename, search and partial labels")
        check(!ComposerCandidatePolicy.exactComposerLabel(["Chat title","Do anything"]) && !ComposerCandidatePolicy.exactComposerLabel(["AXSearchField","随心输入"]),"conflicting rename/search semantics override a composer-like placeholder in the focused shortcut")
        let currentComposer=ComposerCandidateFacts(role:kAXTextAreaRole,hints:["Do anything"])
        let localizedComposer=ComposerCandidateFacts(role:kAXTextAreaRole,hints:["随心输入"])
        check(ComposerCandidatePolicy.select([currentComposer]) == .selected(0) && ComposerCandidatePolicy.select([localizedComposer]) == .selected(0),"exact current English/Chinese accessible composer labels are supported")
        check(ComposerCandidatePolicy.select([ComposerCandidateFacts(role:kAXTextFieldRole,hints:["Chat title"])]) == .none && ComposerCandidatePolicy.select([ComposerCandidateFacts(role:kAXTextFieldRole,hints:["聊天标题"])]) == .none,"actual English/Chinese rename labels are rejected")
        check(ComposerCandidatePolicy.select([ComposerCandidateFacts(role:kAXTextAreaRole,hints:["Do anything later"])]) == .none,"generic text containing part of the new label is insufficient")
        check(ComposerCandidatePolicy.mainDocumentIsUnique(documentCount:1,scanComplete:true),"one fully enumerated outer WebArea is eligible")
        check(!ComposerCandidatePolicy.mainDocumentIsUnique(documentCount:1,scanComplete:false) && !ComposerCandidatePolicy.mainDocumentIsUnique(documentCount:2,scanComplete:true),"partial or multiple outer WebAreas cannot designate the current main document")
        check(ComposerCandidatePolicy.taskTitleLabel(role:kAXButtonRole) && ComposerCandidatePolicy.taskTitleLabel(role:kAXStaticTextRole),"banner task title can be a button or its static label")
        check(!ComposerCandidatePolicy.taskTitleLabel(role:kAXTextFieldRole) && !ComposerCandidatePolicy.taskTitleLabel(role:"AXWebArea"),"rename input values and embedded browser document titles cannot provide banner task identity")
        let bannerSubrole=kAXLandmarkBannerSubrole as String
        let bannerRoles=["AXWebArea",kAXGroupRole,kAXGroupRole]
        check(ComposerCandidatePolicy.bannerProof(role:kAXGroupRole,subrole:bannerSubrole,hidden:false,pathRoles:bannerRoles,knownTitle:true,labelMatches:true,scanComplete:true),"current main-document banner can prove its exact canonical title through a button label")
        check(!ComposerCandidatePolicy.bannerProof(role:kAXGroupRole,subrole:bannerSubrole,hidden:true,pathRoles:bannerRoles,knownTitle:true,labelMatches:true,scanComplete:true),"hidden old banners cannot prove a task")
        check(!ComposerCandidatePolicy.bannerProof(role:kAXGroupRole,subrole:bannerSubrole,hidden:false,pathRoles:["AXWebArea",kAXGroupRole,"AXWebArea",kAXGroupRole],knownTitle:true,labelMatches:true,scanComplete:true),"an embedded browser banner cannot supply current task identity")
        check(!ComposerCandidatePolicy.bannerProof(role:kAXGroupRole,subrole:bannerSubrole,hidden:false,pathRoles:["AXWebArea",kAXScrollAreaRole,kAXGroupRole],knownTitle:true,labelMatches:true,scanComplete:true),"a banner inside scrolling transcript content is not the top task bar")
        check(!ComposerCandidatePolicy.bannerProof(role:kAXGroupRole,subrole:bannerSubrole,hidden:false,pathRoles:bannerRoles,knownTitle:true,labelMatches:false,scanComplete:true),"task B's banner cannot prove selected task A")
        check(!ComposerCandidatePolicy.bannerProof(role:kAXGroupRole,subrole:bannerSubrole,hidden:false,pathRoles:bannerRoles,knownTitle:false,labelMatches:true,scanComplete:true),"an unregistered or nonunique title cannot authorize through a banner")
        check(!ComposerCandidatePolicy.bannerProof(role:kAXGroupRole,subrole:bannerSubrole,hidden:false,pathRoles:bannerRoles,knownTitle:true,labelMatches:true,scanComplete:false),"an 80-node partial banner label scan cannot prove a title")
        check(!ComposerCandidatePolicy.bannerProof(role:"AXHeading",subrole:"",hidden:false,pathRoles:["AXWebArea",kAXGroupRole,"AXHeading"],knownTitle:true,labelMatches:true,scanComplete:true),"task B quoting task A in a body heading cannot authorize task A input")
        check(!ComposerCandidatePolicy.bannerProof(role:kAXButtonRole,subrole:"",hidden:false,pathRoles:["AXWebArea",kAXGroupRole,kAXButtonRole],knownTitle:true,labelMatches:true,scanComplete:true),"an ordinary same-name sidebar button cannot prove task identity")
        var lease=ComposerFocusLease()
        let old=lease.begin();check(lease.allows(old,contextCurrent:true),"current lease initially valid")
        lease.cancel()
        var syntheticActivations=0
        if lease.allows(old,contextCurrent:true){syntheticActivations+=1}
        check(syntheticActivations==0,"cancelled retry must not activate or focus")
        let first=lease.begin(),newest=lease.begin()
        check(!lease.allows(first,contextCurrent:true) && lease.allows(newest,contextCurrent:true),"new request invalidates the old request")
        check(!lease.allows(newest,contextCurrent:false),"mode/session/target cancellation also invalidates a request")
        let temp=FileManager.default.temporaryDirectory.appendingPathComponent("brick-mic-focus-fixture-"+UUID().uuidString)
        defer{try? FileManager.default.removeItem(at:temp)}
        let control=MicControl(BrickBluetooth(),directory:temp)
        let task="00000000-0000-4000-8000-000000000001"
        control.configureFocusFixture(task)
        let generation=control.fixtureGeneration
        control.cancelPreparation()
        check(control.fixtureGeneration==generation+1 && !control.fixturePending,"cancellation clears pending state and advances generation")
        check(control.mode=="codex" && control.fixtureSelectedID==task,"cancellation preserves target and input mode")
        let failures:[RemoteEditor.FocusFailure]=[.permission,.appUnavailable,.invalidTask,.targetUnverified,.composerMissing,.composerAmbiguous,.composerDisabled,.focusUnsupported,.focusUnconfirmed,.scanIncomplete,.cancelled]
        check(Set(failures.map{$0.message}).count==failures.count,"failure stages must remain distinguishable")
        let desktop=RemoteTask(id:task,title:"Duplicate",status:"idle",updated:0)
        let cli=RemoteTask(id:"00000000-0000-4000-8000-000000000002",title:"Duplicate",status:"idle",updated:0)
        RemoteEditor.register([desktop,cli],ids:[task])
        check(RemoteEditor.desktopIDs==[task] && !RemoteEditor.uniqueDesktopTitles.contains("Duplicate"),"CLI title collisions prevent false unique title identity without becoming desktop IDs")
        RemoteEditor.register([desktop],ids:[task,cli.id])
        check(RemoteEditor.desktopIDs==[task] && RemoteEditor.uniqueDesktopTitles.contains("Duplicate"),"registry follows retained valid desktop tasks")
        let metadataTemp=temp.appendingPathComponent("metadata",isDirectory:true)
        try! FileManager.default.createDirectory(at:metadataTemp,withIntermediateDirectories:true)
        let unknownControl=MicControl(BrickBluetooth(),directory:metadataTemp.appendingPathComponent("unknown"))
        unknownControl.configureFocusFixture(task)
        let untrusted=DesktopTaskMetadataStore.LoadResult(entries:[.init(id:task,title:"Untrusted",status:"idle",updated:0)],generatedAt:nil,ageSeconds:nil,stale:false,failure:.invalidSource)
        check(!unknownControl.fixtureApplyMetadata(untrusted) && unknownControl.fixtureDesktopIDs.isEmpty && unknownControl.fixtureSelectable.isEmpty,"unknown or invalid sources cannot promote a hook-only task even when an entry is supplied")
        check(unknownControl.fixtureMetadataMessage.contains("来源无效"),"unregistered invalid source reports its cause before any AX lookup")
        let missing=DesktopTaskMetadataStore.LoadResult(entries:[],generatedAt:nil,ageSeconds:nil,stale:false,failure:.missingSnapshot)
        _=unknownControl.fixtureApplyMetadata(missing)
        check(unknownControl.fixtureMetadataMessage.contains("尚未导入"),"missing desktop inventory reports a specific source failure")
        let metadataBLE=BrickBluetooth(),metadataControl=MicControl(metadataBLE,directory:metadataTemp)
        metadataControl.configureMetadataFixture(task,hook:cli.id)
        let metadata=DesktopTaskMetadataStore.LoadResult(entries:[.init(id:task,title:"Canonical task title",status:"idle",updated:10)],generatedAt:1_700_000_000,ageSeconds:100_000,stale:true,failure:nil)
        let intentGeneration=metadataControl.fixtureGeneration
        check(metadataControl.fixtureApplyMetadata(metadata),"canonical import changes desktop provenance and title")
        check(metadataControl.target?.title=="Canonical task title" && metadataControl.fixtureDesktopIDs==[task],"existing selected task acquires its explicitly exported canonical desktop title")
        check(metadataControl.target?.status=="running" && metadataControl.target?.updated==20,"metadata preserves live hook status and timestamp")
        check(metadataControl.fixturePreservedHistory,"canonical merging preserves replies, unread, delivery and pending alerts")
        check(metadataControl.fixtureSelectable.map{$0.id}==[task] && metadataControl.fixtureTasks.contains{$0.id==cli.id},"hook-only tasks remain internally but cannot appear in the selectable task page")
        check(metadataControl.fixtureGeneration==intentGeneration,"own metadata update keeps the pending user intent generation while AX registry leases are invalidated")
        check(!metadataControl.fixtureApplyMetadata(metadata),"unchanged canonical data does not invalidate preparation again")
        let invalid=DesktopTaskMetadataStore.LoadResult(entries:[],generatedAt:nil,ageSeconds:nil,stale:false,failure:.invalidSource)
        check(!metadataControl.fixtureApplyMetadata(invalid) && metadataControl.fixtureDesktopIDs==[task],"invalid-source refresh preserves only the already validated canonical inventory")
        check(metadataControl.fixturePreservedHistory && metadataBLE.packets.allSatisfy{$0["op"] as? String != "notify"},"metadata never manufactures reminders or modifies history")
        let now=Date().timeIntervalSince1970
        let snapshot:[String:Any]=["schema_version":1,"source":"codex-app/list_threads","host_id":"local","generated_at":now,"threads":[["id":task,"title":"Fresh canonical title","status":"idle","updated":now-1]]]
        let metadataFile=metadataTemp.appendingPathComponent(DesktopTaskMetadataStore.filename)
        try! JSONSerialization.data(withJSONObject:snapshot).write(to:metadataFile,options:.atomic)
        var callback:Bool?
        metadataControl.fixtureRefreshMetadata{callback=$0}
        metadataControl.cancelPreparation()
        waitFixture{callback != nil}
        check(callback==false && metadataControl.target?.id==task,"recording cancellation cannot resume an old metadata-waiting preparation")
        callback=nil
        metadataControl.fixtureRefreshMetadata{callback=$0}
        metadataBLE.controlToken="replacement-session"
        waitFixture{callback != nil}
        check(callback==false,"a replacement BLE session cannot resume the old waiting request")
        callback=nil
        metadataControl.fixtureRefreshMetadata{callback=$0}
        metadataControl.fixtureOrdinary()
        waitFixture{callback != nil}
        check(callback==false && metadataControl.mode=="ordinary","switching to ordinary mode prevents late metadata from reactivating Codex")
        metadataControl.configureFocusFixture(task)
        callback=nil
        metadataControl.fixtureRefreshMetadata{callback=$0}
        metadataControl.stop()
        waitFixture{callback != nil}
        check(callback==false,"stopping the service invalidates pending metadata callbacks")
        let empty=DesktopTaskMetadataStore.LoadResult(entries:[],generatedAt:now,ageSeconds:0,stale:false,failure:nil)
        _=metadataControl.fixtureApplyMetadata(empty)
        check(metadataControl.fixtureDesktopIDs.isEmpty && metadataControl.fixturePreservedHistory,"a valid empty canonical inventory revokes selection without dropping reply history")
        print("COMPOSER_FOCUS_OK: candidate, identity scope, focus confirmation and cancellation boundaries passed")
    }
}
