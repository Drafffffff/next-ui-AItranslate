import AppKit

@main enum RenderIcon {
    static func main() throws {
        let output=URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true)
        try FileManager.default.createDirectory(at:output,withIntermediateDirectories:true)
        for size in [16,32,128,256,512] {
            for scale in [1,2] {
                let pixels=size*scale
                let bitmap=NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:pixels,pixelsHigh:pixels,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!
                NSGraphicsContext.saveGraphicsState()
                NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:bitmap)
                MicBrand.drawIcon(in:NSRect(x:0,y:0,width:pixels,height:pixels))
                NSGraphicsContext.restoreGraphicsState()
                let name="icon_\(size)x\(size)\(scale==2 ? "@2x":"").png"
                try bitmap.representation(using:.png,properties:[:])!.write(to:output.appendingPathComponent(name))
            }
        }
    }
}
