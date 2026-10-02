import Foundation

@main struct CredentialChecks {
    static func main()throws {
        let home=FileManager.default.temporaryDirectory.appendingPathComponent("brick-mic-credentials-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at:home,withIntermediateDirectories:true)
        defer {try? FileManager.default.removeItem(at:home)}
        let store=ShellCredentials(home:home),config=home.appendingPathComponent(".zshrc")
        let original="# keep my shell settings\nalias ll='ls -l'\nexport DASHSCOPE_API_KEY=\"sk-test-original\" # existing\n"
        try original.write(to:config,atomically:true,encoding:.utf8)
        precondition(store.load(environment:[:])=="sk-test-original")
        try store.save("sk-test-replacement")
        precondition(store.load(environment:[:])=="sk-test-replacement")
        let saved=try String(contentsOf:config,encoding:.utf8)
        precondition(saved.contains("alias ll='ls -l'") && saved.contains("# keep my shell settings"))
        let mode=(try FileManager.default.attributesOfItem(atPath:config.path)[.posixPermissions] as! NSNumber).intValue
        precondition(mode==0o600)
        do {try store.save("sk-bad\nvalue");fatalError("newline accepted")}catch{}
        let afterInvalid=try String(contentsOf:config,encoding:.utf8)
        precondition(afterInvalid==saved)
        let marker=home.appendingPathComponent("must-not-exist")
        try "export DASHSCOPE_API_KEY=\"$(touch \(marker.path))\"\n".write(to:config,atomically:true,encoding:.utf8)
        precondition(store.load(environment:[:]).isEmpty && !FileManager.default.fileExists(atPath:marker.path))
        try store.save("sk-test-safe-literal")
        precondition(store.load(environment:[:])=="sk-test-safe-literal")
        let afterLiteral=try String(contentsOf:config,encoding:.utf8)
        precondition(afterLiteral.contains("$(touch"))
        try FileManager.default.removeItem(at:config)
        let fish=home.appendingPathComponent(".config/fish/config.fish")
        try FileManager.default.createDirectory(at:fish.deletingLastPathComponent(),withIntermediateDirectories:true)
        try "set -gx DASHSCOPE_API_KEY 'sk-test-fish-key'\n".write(to:fish,atomically:true,encoding:.utf8)
        precondition(store.load(environment:[:])=="sk-test-fish-key")
        try FileManager.default.removeItem(at:fish)
        precondition(store.load(environment:["DASHSCOPE_API_KEY":"sk-test-environment"])=="sk-test-environment")
        let target=home.appendingPathComponent("dotfiles-zshrc")
        try original.write(to:target,atomically:true,encoding:.utf8)
        try FileManager.default.createSymbolicLink(at:config,withDestinationURL:target)
        try store.save("sk-test-symlink-key")
        precondition(store.load(environment:[:])=="sk-test-symlink-key")
        let linkType=try FileManager.default.attributesOfItem(atPath:config.path)[.type] as? FileAttributeType
        precondition(linkType == .typeSymbolicLink)
        print("PASS: shell literal loading, atomic save, unrelated settings, invalid input, no shell execution, fish/environment fallback and symlink preservation")
    }
}
