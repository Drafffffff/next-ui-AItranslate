import Foundation

struct ShellCredentials {
    let home:URL
    var config:URL {home.appendingPathComponent(".zshrc").resolvingSymlinksInPath()}
    private let names=["DASHSCOPE_API_KEY","BAILIAN_API_KEY"]
    static func valid(_ key:String)->Bool {
        key.hasPrefix("sk-") && key.count>10 && key.unicodeScalars.allSatisfy {
            CharacterSet(charactersIn:"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_").contains($0)
        }
    }
    private func assignment(_ line:String,_ name:String,fish:Bool=false)->String? {
        let pattern=fish ? "^\\s*set\\s+(?:-[gx]+\\s+)+\(name)\\s+(.*)$":"^\\s*(?:export\\s+)?\(name)\\s*=\\s*(.*)$"
        let regex=try! NSRegularExpression(pattern:pattern)
        let range=NSRange(line.startIndex...,in:line)
        guard let match=regex.firstMatch(in:line,range:range),let capture=Range(match.range(at:1),in:line) else{return nil}
        return String(line[capture])
    }
    private func literal(_ value:String)->String? {
        // Read literal assignments only. Never execute or source shell files.
        let pattern="^(?:'([^']*)'|\"([^\"\\\\$`]*)\"|([A-Za-z0-9_-]+))\\s*(?:#.*)?$"
        let regex=try! NSRegularExpression(pattern:pattern)
        guard let match=regex.firstMatch(in:value,range:NSRange(value.startIndex...,in:value)) else{return nil}
        for i in 1...3 {if let range=Range(match.range(at:i),in:value) {return String(value[range])}}
        return nil
    }
    func load(environment:[String:String]=ProcessInfo.processInfo.environment)->String {
        let sources=[(".zshrc",false),(".bashrc",false),(".config/fish/config.fish",true)]
        for (path,fish) in sources {
            guard let data=try? Data(contentsOf:home.appendingPathComponent(path)),data.count<=2_000_000,
                  let text=String(data:data,encoding:.utf8) else{continue}
            for name in names {
                // Last assignment has shell precedence, including an explicit clear.
                if let value=text.components(separatedBy:"\n").reversed().compactMap({assignment($0,name,fish:fish)}).first,
                   let key=literal(value),Self.valid(key) {return key}
            }
        }
        return names.compactMap{environment[$0]}.first(where:Self.valid) ?? ""
    }
    func save(_ key:String)throws {
        guard Self.valid(key) else{throw NSError(domain:"BrickMic.Credentials",code:1)}
        let file=config
        let existing=FileManager.default.fileExists(atPath:file.path) ? try String(contentsOf:file,encoding:.utf8):""
        var lines=existing.components(separatedBy:"\n")
        let export="export DASHSCOPE_API_KEY='\(key)'"
        if let index=lines.lastIndex(where:{assignment($0,"DASHSCOPE_API_KEY") != nil}),
           let value=assignment(lines[index],"DASHSCOPE_API_KEY"),literal(value) != nil {
            let indent=String(lines[index].prefix(while:{$0==" " || $0=="\t"}))
            lines[index]=indent+export
        } else {
            if lines.last=="" {lines.removeLast()}
            lines.append(export);lines.append("")
        }
        let data=Data(lines.joined(separator:"\n").utf8)
        try data.write(to:file,options:.atomic)
        try FileManager.default.setAttributes([.posixPermissions:0o600],ofItemAtPath:file.path)
    }
}
enum MicCredentials {
    private static let store=ShellCredentials(home:FileManager.default.homeDirectoryForCurrentUser)
    static func load()->String {store.load()}
    static func save(_ key:String)->Bool {do {try store.save(key);return true}catch{return false}}
}
