import Foundation

enum ASRConfiguration {
    static let model="qwen-audio-3.1-asr-flash-message"
    static let defaultEndpoint="wss://dashscope.aliyuncs.com/api-ws/v1/inference"
    private static let migrationKey="asrMessageProtocolVersion"

    // Preserve the configured region/workspace host and credentials. Message ASR
    // uses a different WebSocket path and a run-task payload, not ?model=.
    static func inferenceEndpoint(_ endpoint:String)->String {
        guard var parts=URLComponents(string:endpoint),parts.scheme=="wss",parts.host != nil else{return endpoint}
        if parts.path=="/api-ws/v1/realtime" {parts.path="/api-ws/v1/inference"}
        parts.queryItems=parts.queryItems?.filter{$0.name != "model"}
        if parts.queryItems?.isEmpty==true {parts.queryItems=nil}
        return parts.url?.absoluteString ?? endpoint
    }
    static func migrateDefaults(_ defaults:UserDefaults = .standard) {
        guard defaults.integer(forKey:migrationKey)<1 else{return}
        defaults.set(model,forKey:"asrModel")
        let endpoint=defaults.string(forKey:"asrEndpoint") ?? defaultEndpoint
        defaults.set(inferenceEndpoint(endpoint),forKey:"asrEndpoint")
        defaults.set(1,forKey:migrationKey)
    }
    static func current(_ defaults:UserDefaults = .standard)->(model:String,endpoint:String) {
        let endpoint=defaults.string(forKey:"asrEndpoint") ?? defaultEndpoint
        if defaults.integer(forKey:migrationKey)<1{return (model,inferenceEndpoint(endpoint))}
        return (defaults.string(forKey:"asrModel") ?? model,endpoint)
    }
}

struct ASRMetrics {
    var connectionMS:Double
    var queuedAtEndMS:Double
    var uploadTailMS:Double
    var serverFinalMS:Double
    var endToFinalMS:Double
    var previews:Int
}

enum ASRWireMessage {
    case text(String)
    case audio(Data)
}
protocol ASRMessageTransport:AnyObject {
    func resume()
    func send(_ message:ASRWireMessage,completion:@escaping (Error?)->Void)
    func receive(completion:@escaping (Result<Data,Error>)->Void)
    func cancel()
}
private final class ASRWebSocketTransport:ASRMessageTransport {
    private let session:URLSession
    private let socket:URLSessionWebSocketTask
    init(_ request:URLRequest) {
        let config=URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest=15;config.timeoutIntervalForResource=90
        session=URLSession(configuration:config)
        socket=session.webSocketTask(with:request)
    }
    func resume(){socket.resume()}
    func send(_ message:ASRWireMessage,completion:@escaping (Error?)->Void) {
        let frame:URLSessionWebSocketTask.Message
        switch message {case .text(let text):frame = .string(text);case .audio(let pcm):frame = .data(pcm)}
        socket.send(frame){error in DispatchQueue.main.async{completion(error)}}
    }
    func receive(completion:@escaping (Result<Data,Error>)->Void) {
        socket.receive{result in DispatchQueue.main.async {
            switch result {
            case .failure(let error):completion(.failure(error))
            case .success(let message):
                switch message {
                case .string(let text):completion(.success(Data(text.utf8)))
                case .data(let data):completion(.success(data))
                @unknown default:completion(.failure(NSError(domain:"BrickMic.ASR",code:1)))
                }
            }
        }}
    }
    func cancel(){socket.cancel(with:.normalClosure,reason:nil);session.invalidateAndCancel()}
}

final class BailianASR {
    // The Message model produces sentence results internally. Only onFinal
    // exposes the complete utterance after the user releases A and task-finished.
    var onPreview: ((String)->Void)?
    var onFinal: ((String,Double)->Void)?
    var onError: ((String)->Void)?
    var onMetrics: ((ASRMetrics)->Void)?
    private let transportFactory:(URLRequest)->ASRMessageTransport
    private var taskID=UUID().uuidString
    private var started=false
    private var startedAt=Date()
    private var readyAt:Date?
    private var finishAt:Date?
    private var queuedAtEndMS=0.0
    private var transport:ASRMessageTransport?
    private var queue: [Data] = []
    private var queuedBytes=0
    private var sending=false
    private var ready=false
    private var ended=false
    private var cancelled=false
    private var finishSent=false
    private var endAt:Date?
    private var results:[Int:String]=[:]
    private var timer:Timer?

    // manual remains source-compatible with the old benchmark entry point.
    // Message ASR always aggregates VAD sentences until explicit end().
    init(manual:Bool=true,transportFactory:@escaping (URLRequest)->ASRMessageTransport = {ASRWebSocketTransport($0)}) {
        self.transportFactory=transportFactory
    }
    func start(key:String,endpoint:String,model:String) {
        guard !started,!cancelled else{return}
        started=true;startedAt=Date()
        guard !key.isEmpty else{fail("请先配置百炼 API Key");return}
        guard model==ASRConfiguration.model else{fail("识别模型请设置为 \(ASRConfiguration.model)");return}
        guard let components=URLComponents(string:ASRConfiguration.inferenceEndpoint(endpoint)),
              components.scheme=="wss",components.host?.isEmpty==false,
              components.path=="/api-ws/v1/inference",let url=components.url else{
            fail("识别地址应为 wss:// 服务域名/api-ws/v1/inference");return
        }
        var request=URLRequest(url:url);request.timeoutInterval=10
        request.setValue("Bearer \(key)",forHTTPHeaderField:"Authorization")
        request.setValue("BrickMic/0.3",forHTTPHeaderField:"User-Agent")
        let transport=transportFactory(request);self.transport=transport
        transport.resume();receive()
        sending=true
        sendJSON(["header":header("run-task"),"payload":["task_group":"audio","task":"asr",
            "function":"recognition","model":model,"input":[:],
            "parameters":["format":"pcm","sample_rate":16000,"intermediate_result_enabled":false,
                "vad_model":"near_meeting_16k"]]]){[weak self] error in
            guard let self=self,!self.cancelled else{return}
            self.sending=false
            if error != nil{self.fail("识别任务启动失败")}else{self.pump()}
        }
        if !cancelled && !ready {
            timer=Timer.scheduledTimer(withTimeInterval:10,repeats:false){[weak self] _ in
                if self?.ready != true{self?.fail("百炼连接超时，请检查网络、Key、地域和模型")}
            }
        }
    }
    private func header(_ action:String)->[String:Any]{["action":action,"task_id":taskID,"streaming":"duplex"]}
    private func sendJSON(_ value:[String:Any],completion:@escaping (Error?)->Void) {
        guard let transport=transport,!cancelled else{return}
        guard let data=try? JSONSerialization.data(withJSONObject:value),let text=String(data:data,encoding:.utf8) else{
            fail("识别请求编码失败");return
        }
        transport.send(.text(text),completion:completion)
    }
    func append(_ pcm:Data) {
        guard started,!cancelled,!ended,!pcm.isEmpty else{return}
        guard pcm.count%2==0 else{fail("录音数据格式错误");return}
        // 16kHz mono PCM16: bound buffered audio to three seconds regardless
        // of Bluetooth frame size. In-flight audio is at most another 100ms.
        guard queuedBytes+pcm.count<=96000 else{fail("网络上传过慢，本次输入已停止");return}
        queue.append(pcm);queuedBytes+=pcm.count;pump()
    }
    func end() {
        guard started,!cancelled,!ended else{return}
        ended=true;endAt=Date();queuedAtEndMS=Double(queuedBytes)/32
        pump()
        guard !cancelled else{return}
        timer?.invalidate();timer=Timer.scheduledTimer(withTimeInterval:15,repeats:false){[weak self] _ in
            self?.fail("等待最终识别结果超时")
        }
    }
    private func pump() {
        guard ready,!sending,!cancelled,let transport=transport else{return}
        if !queue.isEmpty {
            // Binary PCM, not base64 JSON. Send no more than 100ms at a time
            // and flush queued frames before finish-task, without a silence wait.
            var pcm=Data()
            while !queue.isEmpty && pcm.count<3200 {
                let count=min(3200-pcm.count,queue[0].count)
                pcm.append(queue[0].prefix(count));queuedBytes-=count
                if count==queue[0].count{queue.removeFirst()}else{queue[0].removeFirst(count)}
            }
            sending=true
            transport.send(.audio(pcm)){[weak self] error in
                guard let self=self,!self.cancelled else{return};self.sending=false
                if error != nil{self.fail("百炼音频上传失败")}else{self.pump()}
            }
        }else if ended && !finishSent {
            finishSent=true;finishAt=Date();sending=true
            sendJSON(["header":header("finish-task"),"payload":["input":[:]]]){[weak self] error in
                guard let self=self,!self.cancelled else{return};self.sending=false
                if error != nil{self.fail("结束识别失败")}
            }
        }
    }
    private func receive() {
        transport?.receive{[weak self] result in
            guard let self=self,!self.cancelled else{return}
            switch result {
            case .failure:self.fail("百炼连接中断，请检查 Key、网络、地域和模型")
            case .success(let data):
                guard data.count<=1_000_000,let object=(try? JSONSerialization.jsonObject(with:data)) as? [String:Any] else{
                    self.fail("百炼返回了无效的识别事件");return
                }
                self.handle(object)
                if !self.cancelled{self.receive()}
            }
        }
    }
    private func handle(_ value:[String:Any]) {
        guard let header=value["header"] as? [String:Any],header["task_id"] as? String==taskID else{return}
        switch header["event"] as? String ?? "" {
        case "task-started":
            guard !ready else{return};ready=true;readyAt=Date()
            if !ended{timer?.invalidate()};pump()
        case "result-generated":
            guard ready,let payload=value["payload"] as? [String:Any],let output=payload["output"] as? [String:Any],
                  let sentence=output["sentence"] as? [String:Any],sentence["heartbeat"] as? Bool != true,
                  sentence["sentence_end"] as? Bool==true,let id=sentence["sentence_id"] as? Int,id>0,
                  let text=sentence["text"] as? String else{return}
            guard id<=4096,text.count<=24000 else{fail("识别结果过长，请缩短本次录音");return}
            results[id]=text
            if results.values.reduce(0,{$0+$1.count})>24000{fail("识别结果过长，请缩短本次录音")}
        case "task-finished":
            guard finishSent else{fail("识别任务提前结束，请重试");return}
            let text=results.keys.sorted().compactMap{results[$0]}.joined(),now=Date()
            let latency=endAt.map{now.timeIntervalSince($0)*1000} ?? 0
            onMetrics?(ASRMetrics(connectionMS:readyAt.map{$0.timeIntervalSince(startedAt)*1000} ?? 0,
                queuedAtEndMS:queuedAtEndMS,uploadTailMS:finishAt.flatMap{finish in endAt.map{finish.timeIntervalSince($0)*1000}} ?? 0,
                serverFinalMS:finishAt.map{now.timeIntervalSince($0)*1000} ?? 0,endToFinalMS:latency,previews:0))
            cancel();onFinal?(text,latency)
        case "task-failed":
            // Display bounded code identifiers only; never echo error_message
            // or request payloads, which can contain credential/audio data.
            let raw=header["error_code"] as? String ?? "unknown"
            let code=String(raw.prefix(64).filter{$0.isASCII && ($0.isLetter || $0.isNumber || $0=="_" || $0=="-" || $0==".")})
            fail("百炼识别失败（\(code.isEmpty ? "unknown":code)）")
        default:break
        }
    }
    func cancel() {
        cancelled=true;timer?.invalidate();timer=nil;queue.removeAll();queuedBytes=0
        transport?.cancel();transport=nil
    }
    private func fail(_ message:String){guard !cancelled else{return};cancel();onError?(message)}
}
