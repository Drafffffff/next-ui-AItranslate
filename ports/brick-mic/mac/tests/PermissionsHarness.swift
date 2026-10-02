import AppKit

@main enum PermissionsHarness {
 static func main() {
  let running=URL(fileURLWithPath:"/Applications/Brick Mic.app")
  let other=URL(fileURLWithPath:"/tmp/Build/Brick Mic.app")
  precondition(PermissionApplication.resolve(running:running,bundle:other)==running)
  precondition(PermissionApplication.resolve(running:nil,bundle:other)==other)
  precondition(PermissionApplication.resolve(running:URL(string:"https://example.com/Fake.app"),bundle:URL(fileURLWithPath:"/tmp/BrickMic"))==nil)
  let pasteboard=NSPasteboard(name:NSPasteboard.Name("BrickMicPermissionFixture-"+UUID().uuidString))
  pasteboard.clearContents();precondition(pasteboard.writeObjects([running as NSURL]))
  let values=pasteboard.readObjects(forClasses:[NSURL.self]) as? [URL]
  precondition(values?.first==running);pasteboard.releaseGlobally()
  precondition(!InputPermissionState(accessibility:true,posting:false).allowed)
  precondition(!InputPermissionState(accessibility:false,posting:true).allowed)
  precondition(InputPermissionState(accessibility:true,posting:true).allowed)
  print("PASS: correct running app, real file URL drag payload, partial permission state")
 }
}
