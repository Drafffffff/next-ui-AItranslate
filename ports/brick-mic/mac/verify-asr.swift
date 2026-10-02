import Foundation

private final class MockASRTransport:ASRMessageTransport {
    var messages:[ASRWireMessage]=[]
    var resumed=false,cancelled=false,holdAudio=false
    var pendingSends:[(Error?)->Void]=[]
    private var receiver:((Result<Data,Error>)->Void)?
    func resume(){resumed=true}
    func send(_ message:ASRWireMessage,completion:@escaping (Error?)->Void) {
        messages.append(message)
        if case .audio = message,holdAudio{pendingSends.append(completion)}else{completion(nil)}
    }
    func receive(completion:@escaping (Result<Data,Error>)->Void){receiver=completion}
    func cancel(){cancelled=true}
    func completeSend(_ error:Error?=nil){precondition(!pendingSends.isEmpty);pendingSends.removeFirst()(error)}
    func emit(_ event:String,taskID:String,payload:[String:Any]=[:],extras:[String:Any]=[:]) {
        var header:[String:Any]=["event":event,"task_id":taskID]
        extras.forEach{header[$0]=$1}
        let data=try! JSONSerialization.data(withJSONObject:["header":header,"payload":payload])
        let callback=receiver;receiver=nil;precondition(callback != nil);callback?(.success(data))
    }
    func sentence(_ id:Int,_ text:String,taskID:String,ended:Bool=true,heartbeat:Bool=false) {
        emit("result-generated",taskID:taskID,payload:["output":["sentence":["sentence_id":id,"text":text,
            "sentence_end":ended,"heartbeat":heartbeat]]])
    }
    func object(_ index:Int)->[String:Any] {
        guard case .text(let text)=messages[index] else{fatalError("Expected JSON text frame")}
        return try! JSONSerialization.jsonObject(with:Data(text.utf8)) as! [String:Any]
    }
    var taskID:String {(object(0)["header"] as! [String:Any])["task_id"] as! String}
}

@main struct ASRChecks {
    static func main()throws {
        try migrationChecks()
        protocolChecks()
        cancellationAndLimits()
        print("PASS: Message ASR run-task, start gate, ordered binary PCM, release flush, sentence revisions, single full final, task isolation, cancellation, bounds and region-preserving settings migration")
    }
    static func migrationChecks()throws {
        let name="brick-mic-asr-test-"+UUID().uuidString,defaults=UserDefaults(suiteName:name)!
        defer{defaults.removePersistentDomain(forName:name)}
        defaults.set("qwen3-asr-flash-realtime",forKey:"asrModel")
        defaults.set("wss://dashscope-intl.aliyuncs.com/api-ws/v1/realtime?model=old&custom=1",forKey:"asrEndpoint")
        defaults.set("sk-test-placeholder",forKey:"key")
        let current=ASRConfiguration.current(defaults)
        precondition(current.model==ASRConfiguration.model)
        precondition(current.endpoint=="wss://dashscope-intl.aliyuncs.com/api-ws/v1/inference?custom=1")
        precondition(defaults.string(forKey:"asrModel")=="qwen3-asr-flash-realtime","Read-only settings accessor wrote preferences")
        ASRConfiguration.migrateDefaults(defaults)
        precondition(defaults.string(forKey:"key")=="sk-test-placeholder")
        precondition(ASRConfiguration.current(defaults).model==ASRConfiguration.model)
        let workspace="wss://example-workspace.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference"
        defaults.set(workspace,forKey:"asrEndpoint")
        ASRConfiguration.migrateDefaults(defaults)
        precondition(ASRConfiguration.current(defaults).endpoint==workspace)
        precondition(ASRConfiguration.inferenceEndpoint(workspace)==workspace)
    }
    static func protocolChecks() {
        let transport=MockASRTransport();transport.holdAudio=true
        var url:URL?,finals:[String]=[],errors:[String]=[],previewCount=0,metrics:ASRMetrics?
        let asr=BailianASR{request in url=request.url;return transport}
        asr.onFinal={text,_ in finals.append(text)};asr.onError={errors.append($0)}
        asr.onPreview={_ in previewCount+=1};asr.onMetrics={metrics=$0}
        asr.start(key:"sk-test-placeholder",endpoint:"wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=old",model:ASRConfiguration.model)
        precondition(transport.resumed && url?.path=="/api-ws/v1/inference" && url?.query==nil)
        let run=transport.object(0),header=run["header"] as! [String:Any],payload=run["payload"] as! [String:Any]
        precondition(header["action"] as? String=="run-task" && header["streaming"] as? String=="duplex")
        precondition(UUID(uuidString:transport.taskID) != nil)
        precondition(payload["model"] as? String==ASRConfiguration.model)
        let parameters=payload["parameters"] as! [String:Any]
        precondition(parameters["format"] as? String=="pcm" && parameters["sample_rate"] as? Int==16000)
        precondition(parameters["intermediate_result_enabled"] as? Bool==false)
        let pcm=Data((0..<8000).map{UInt8($0%251)})
        asr.append(pcm);asr.end();asr.end()
        precondition(transport.messages.count==1,"Audio or finish-task preceded task-started")
        transport.emit("task-started",taskID:UUID().uuidString)
        precondition(transport.messages.count==1,"Foreign task opened upload gate")
        let taskID=transport.taskID
        transport.emit("task-started",taskID:taskID)
        precondition(transport.messages.count==2 && transport.pendingSends.count==1)
        transport.completeSend();transport.completeSend()
        precondition(transport.messages.count==4,"finish-task overtook in-flight audio")
        transport.completeSend()
        precondition(transport.messages.count==5 && transport.pendingSends.isEmpty)
        var uploaded=Data(),sizes:[Int]=[]
        for frame in transport.messages {if case .audio(let data)=frame{uploaded.append(data);sizes.append(data.count)}}
        precondition(uploaded==pcm && sizes==[3200,3200,1600])
        let finish=transport.object(4)
        precondition((finish["header"] as! [String:Any])["action"] as? String=="finish-task")
        precondition((finish["header"] as! [String:Any])["task_id"] as? String==taskID)
        precondition(((finish["payload"] as! [String:Any])["input"] as! [String:Any]).isEmpty)
        transport.sentence(2,"第二句。",taskID:taskID)
        transport.sentence(1,"中间文本",taskID:taskID,ended:false)
        transport.sentence(1,"初稿。",taskID:taskID)
        transport.sentence(1,"第一句。",taskID:taskID)
        transport.sentence(0,"忽略心跳",taskID:taskID,heartbeat:true)
        transport.sentence(3,"其他任务",taskID:UUID().uuidString)
        precondition(finals.isEmpty && previewCount==0,"Sentence events exposed a partial utterance")
        transport.emit("task-finished",taskID:taskID)
        precondition(finals==["第一句。第二句。"] && errors.isEmpty && transport.cancelled)
        precondition(metrics?.queuedAtEndMS==250 && metrics?.previews==0)
    }
    static func cancellationAndLimits() {
        do {
            let transport=MockASRTransport();var finals=0,errors=0
            let asr=BailianASR{_ in transport};asr.onFinal={_,_ in finals+=1};asr.onError={_ in errors+=1}
            asr.start(key:"sk-test-placeholder",endpoint:ASRConfiguration.defaultEndpoint,model:ASRConfiguration.model)
            let taskID=transport.taskID;asr.cancel()
            transport.emit("task-failed",taskID:taskID,extras:["error_code":"CLIENT_ERROR"])
            asr.end();asr.append(Data(repeating:0,count:640))
            precondition(finals==0 && errors==0 && transport.messages.count==1 && transport.cancelled)
        }
        do {
            let transport=MockASRTransport();var errors=0
            let asr=BailianASR{_ in transport};asr.onError={_ in errors+=1}
            asr.start(key:"sk-test-placeholder",endpoint:ASRConfiguration.defaultEndpoint,model:ASRConfiguration.model)
            asr.append(Data(repeating:0,count:96002))
            precondition(errors==1 && transport.cancelled && transport.messages.count==1)
        }
        do {
            let transport=MockASRTransport();var errors:[String]=[]
            let asr=BailianASR{_ in transport};asr.onError={errors.append($0)}
            asr.start(key:"sk-test-placeholder",endpoint:ASRConfiguration.defaultEndpoint,model:ASRConfiguration.model)
            transport.emit("task-failed",taskID:transport.taskID,extras:["error_code":"CLIENT_ERROR","error_message":"must not expose request or key"])
            precondition(errors.count==1 && errors[0]=="百炼识别失败（CLIENT_ERROR）" && transport.cancelled)
        }
        do {
            var opened=false,errorCount=0
            let asr=BailianASR{_ in opened=true;return MockASRTransport()};asr.onError={_ in errorCount+=1}
            asr.start(key:"sk-test-placeholder",endpoint:ASRConfiguration.defaultEndpoint,model:"qwen3-asr-flash-realtime")
            precondition(!opened && errorCount==1,"Old model incorrectly used Message wire protocol")
        }
        do {
            let transport=MockASRTransport();var finals=0,errors=0
            let asr=BailianASR{_ in transport};asr.onFinal={_,_ in finals+=1};asr.onError={_ in errors+=1}
            asr.start(key:"sk-test-placeholder",endpoint:ASRConfiguration.defaultEndpoint,model:ASRConfiguration.model)
            transport.emit("task-finished",taskID:transport.taskID)
            precondition(finals==0 && errors==1,"Unexpected server finish inserted text before release")
        }
    }
}
