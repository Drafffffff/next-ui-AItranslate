import Foundation

// Standalone test double. Do not include this file in the production application.
final class BrickBluetooth {
    var controlsAllowed=true
    var controlToken="fixture-connection"
    var onControl:(([String:Any])->Void)?
    func control(_ object:[String:Any]){onControl?(object)}
}

@main struct ControlHarness {
    static func main() throws {
        guard CommandLine.arguments.count==3 else{exit(2)}
        let directory=URL(fileURLWithPath:CommandLine.arguments[1]),scenario=CommandLine.arguments[2]
        let ble=BrickBluetooth(),controller=MicControl(BrickBluetooth(),directory:directory)
        if scenario=="unit"{let ok=controller.verify();print(ok ? "PASS: control fixtures":"FAIL: control fixtures");exit(ok ? 0:1)}
        let live=MicControl(ble,directory:directory)
        ble.onControl={object in
            guard let state=object["state"] as? [String:Any],let request=state["approval"] as? [String:Any],let id=request["id"] as? String else{return}
            DispatchQueue.main.asyncAfter(deadline:.now()+0.02){
                if scenario=="disconnect"{live.disconnected();return}
                let message:[String:Any]=["token":ble.controlToken,"op":"approval","id":id,"arg":scenario]
                if let data=try? JSONSerialization.data(withJSONObject:message){live.message(data)}
            }
        }
        live.start();defer{live.stop()}
        RunLoop.main.run(until:Date().addingTimeInterval(6))
    }
}
