import Foundation
import CoreBluetooth

final class BrickBluetooth:NSObject,CBCentralManagerDelegate,CBPeripheralDelegate {
    static let service=CBUUID(string:"BA1C0000-7E89-4C31-A2D0-4F923CB1A100")
    static let audio=CBUUID(string:"BA1C0001-7E89-4C31-A2D0-4F923CB1A100")
    static let control=CBUUID(string:"BA1C0002-7E89-4C31-A2D0-4F923CB1A100")
    var onState:((String)->Void)?
    var onMessage:((UInt8,Int,Int,Data)->Void)?
    var onDisconnect:(()->Void)?
 var onReady:(()->Void)?
 private(set) var controlToken=""
 private var controlID:UInt32=0
 private var rejected=false
 private var receiverID:String {
  if let id=UserDefaults.standard.string(forKey:"receiverID"),UUID(uuidString:id) != nil{return id}
  let id=UUID().uuidString;UserDefaults.standard.set(id,forKey:"receiverID");return id
 }
 private var receiverName:String {String((Host.current().localizedName ?? "Mac").unicodeScalars.filter{ !CharacterSet.controlCharacters.contains($0) }.prefix(24))}
 private var diagnostic:Bool{CommandLine.arguments.contains{$0.hasPrefix("--probe-out=")}}
 private(set) var controlsAllowed=false
 var deviceID:String?{peripheral?.identifier.uuidString}
 func trustCurrent(_ allow:Bool){if allow,let id=deviceID{UserDefaults.standard.set(id,forKey:"controlPeer")}else{UserDefaults.standard.removeObject(forKey:"controlPeer")};reconnect()}
    private var manager:CBCentralManager!
    private var peripheral:CBPeripheral?
    private var tx:CBCharacteristic?
    private var rx:CBCharacteristic?
    private let assembler=MicAssembler()
    private var writes:[Data]=[]
    private var writing=false
    private var stopped=false
    private var ready=false
    private var scanTimeout:DispatchWorkItem?
    private var retry:DispatchWorkItem?
    private var useCached=true
    private func stopDiscovery() {manager?.stopScan();scanTimeout?.cancel();scanTimeout=nil;retry?.cancel();retry=nil}
    private func find() {
        guard !stopped,manager.state == .poweredOn,peripheral==nil else{return}
        if useCached,let value=UserDefaults.standard.string(forKey:"brickPeripheral"),let id=UUID(uuidString:value),
           let known=manager.retrievePeripherals(withIdentifiers:[id]).first {
            stopDiscovery();peripheral=known;known.delegate=self
            onState?("正在等待上次的 Brick…");manager.connect(known,options:nil)
        } else {scan()}
    }
    func start() {
        stopped=false
        // This upgrade is authorized for the Brick already used for dictation.
        if !diagnostic,!UserDefaults.standard.bool(forKey:"controlPeerMigrated") {
            if let known=UserDefaults.standard.string(forKey:"brickPeripheral"){UserDefaults.standard.set(known,forKey:"controlPeer")}
            UserDefaults.standard.set(true,forKey:"controlPeerMigrated")
        }
        switch CBCentralManager.authorization {
        case .notDetermined:onState?("请确认 Brick Mic 的蓝牙授权")
        case .denied,.restricted:onState?("请在系统设置中允许 Brick Mic 使用蓝牙")
        default:onState?("正在连接 Brick…")
        }
        manager=CBCentralManager(delegate:self,queue:nil)
    }
    func reconnect() {stopped=false;useCached=false;if let p=peripheral {manager.cancelPeripheralConnection(p)} else {find()}}
    private func scan() {
        guard !stopped,manager.state == .poweredOn,peripheral==nil else{return}
        stopDiscovery();onState?("正在寻找 Brick…")
        manager.scanForPeripherals(withServices:CommandLine.arguments.contains("--scan-all") ? nil:[Self.service],options:[CBCentralManagerScanOptionAllowDuplicatesKey:false])
        let timeout=DispatchWorkItem{[weak self] in
            guard let self=self,self.peripheral==nil,!self.stopped else{return}
            self.manager.stopScan();self.onState?("等待 Brick · 暂停扫描")
            let again=DispatchWorkItem{[weak self] in self?.find()};self.retry=again
            DispatchQueue.main.asyncAfter(deadline:.now()+12,execute:again)
        }
        scanTimeout=timeout;DispatchQueue.main.asyncAfter(deadline:.now()+8,execute:timeout)
    }
    func centralManagerDidUpdateState(_ central:CBCentralManager) {
        switch central.state {case .poweredOn:find();case .unauthorized:onState?("请在系统设置中允许 Brick Mic 使用蓝牙");case .poweredOff:onState?("请打开 Mac 蓝牙");default:onState?("蓝牙暂不可用")}
    }
    func centralManager(_ central:CBCentralManager,didDiscover peripheral:CBPeripheral,advertisementData:[String:Any],rssi RSSI:NSNumber) {
        if CommandLine.arguments.contains("--scan-all") {print("discovered name=\(peripheral.name ?? "unnamed") services=\(advertisementData[CBAdvertisementDataServiceUUIDsKey] ?? []) rssi=\(RSSI)");fflush(stdout)}
        guard (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []).contains(Self.service) else{return}
        // Keep an established pair isolated even when cached connection fails.
        if !diagnostic,let expected=UserDefaults.standard.string(forKey:"brickPeripheral"),peripheral.identifier.uuidString != expected{return}
        guard self.peripheral==nil else{return};self.peripheral=peripheral;peripheral.delegate=self;stopDiscovery();onState?("正在连接 Brick…");central.connect(peripheral,options:nil)
    }
    func centralManager(_ central:CBCentralManager,didConnect peripheral:CBPeripheral) {peripheral.discoverServices([Self.service])}
    func centralManager(_ central:CBCentralManager,didFailToConnect peripheral:CBPeripheral,error:Error?) {useCached=false;disconnected()}
    func centralManager(_ central:CBCentralManager,didDisconnectPeripheral peripheral:CBPeripheral,error:Error?) {disconnected()}
    private func disconnected() {
        stopDiscovery();peripheral=nil;tx=nil;rx=nil;ready=false;controlsAllowed=false;controlToken="";writes.removeAll();writing=false;assembler.reset();onDisconnect?()
        if !stopped {let again=DispatchWorkItem{[weak self] in self?.find()};retry=again;DispatchQueue.main.asyncAfter(deadline:.now()+(rejected ? 3:1),execute:again)}
    }
    func peripheral(_ peripheral:CBPeripheral,didDiscoverServices error:Error?) {
        guard error==nil else{onState?("读取蓝牙服务失败");manager.cancelPeripheralConnection(peripheral);return}
        for service in peripheral.services ?? [] where service.uuid==Self.service {peripheral.discoverCharacteristics([Self.audio,Self.control],for:service)}
    }
    func peripheral(_ peripheral:CBPeripheral,didDiscoverCharacteristicsFor service:CBService,error:Error?) {
        guard error==nil else{onState?("读取音频通道失败");manager.cancelPeripheralConnection(peripheral);return}
        for characteristic in service.characteristics ?? [] {if characteristic.uuid==Self.audio {rx=characteristic};if characteristic.uuid==Self.control {tx=characteristic}}
        if let rx=rx,tx != nil {peripheral.setNotifyValue(true,for:rx)}
    }
    func peripheral(_ peripheral:CBPeripheral,didUpdateNotificationStateFor characteristic:CBCharacteristic,error:Error?) {
        guard error==nil,characteristic.isNotifying else{onState?("无法订阅音频通道");manager.cancelPeripheralConnection(peripheral);return}
        // Without-response write size is the ATT payload capacity, unlike long writes.
        let packet=min(244,peripheral.maximumWriteValueLength(for:.withoutResponse))
        controlsAllowed = !diagnostic && UserDefaults.standard.string(forKey:"controlPeer")==peripheral.identifier.uuidString
        controlToken=String(UUID().uuidString.replacingOccurrences(of:"-",with:"").prefix(16))
        rejected=false
        write(["receiverID":receiverID,"receiverName":receiverName,"op":"hello","packet":packet,"acks":true,"version":diagnostic ? 1:2,"remote":controlsAllowed,"token":controlToken]);print("BLE notification payload=\(packet)")
    }
    func peripheral(_ peripheral:CBPeripheral,didWriteValueFor characteristic:CBCharacteristic,error:Error?) {
        writing=false
        if error != nil {rejected = !ready;onState?(ready ? "蓝牙控制消息发送失败":"Brick 尚未选择这台电脑");manager.cancelPeripheralConnection(peripheral);return}
        if !ready {ready=true;useCached=true;UserDefaults.standard.set(peripheral.identifier.uuidString,forKey:"brickPeripheral");stopDiscovery();onState?("已连接 · 在 Brick 上按住 A 说话");onReady?()}
        pump()
    }
    func peripheral(_ peripheral:CBPeripheral,didUpdateValueFor characteristic:CBCharacteristic,error:Error?) {
        guard error==nil,let value=characteristic.value else{return}
        do {if let m=try assembler.append(value) {onMessage?(m.kind,m.session,m.frame,m.data)}} catch {onState?("音频分片丢失，本次输入已取消");onDisconnect?()}
    }
    func write(_ object:[String:Any]) {
        guard tx != nil,let data=try? JSONSerialization.data(withJSONObject:object),data.count<=512 else{return}
        if object["op"] as? String=="ack" {writes.insert(data,at:0)}else{writes.append(data)};pump()
    }
    private func pump() {guard !writing,!writes.isEmpty,let p=peripheral,let tx=tx else{return};writing=true;p.writeValue(writes.removeFirst(),for:tx,type:.withResponse)}
    func result(session:Int,text:String) {
        var chunk="";var chunks:[String]=[]
        // Character boundaries are preserved; packets stay below a 185-byte ATT MTU.
        for character in text {let c=String(character);if chunk.utf8.count+c.utf8.count>90 {chunks.append(chunk);chunk=""};chunk+=c}
        chunks.append(chunk)
        for (i,part) in chunks.enumerated() {write(["op":i==0 ? "result":"append","session":session,"text":part,"final":i==chunks.count-1])}
    }
    func control(_ object:[String:Any]) {
        guard ready,!diagnostic,!controlToken.isEmpty,let data=try? JSONSerialization.data(withJSONObject:object),data.count<=16384 else{return}
        controlID &+= 1
        let size=48,total=(data.count+size-1)/size
        for part in 0..<total {
            let bytes=data.subdata(in:part*size..<min((part+1)*size,data.count))
            write(["op":"c","token":controlToken,"id":controlID,"p":part,"n":total,"d":bytes.base64EncodedString()])
        }
    }
    func stop() {stopped=true;stopDiscovery();if let p=peripheral {manager.cancelPeripheralConnection(p)}}
}
