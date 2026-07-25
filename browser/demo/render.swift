import AppKit
import AVFoundation
import CoreText

private let width = 1920
private let height = 1080
private let framesPerSecond: Int32 = 30
private let duration: Int32 = 20

private var fetchCapture = ["$ cfx 71A"]
private var submitCapture = ["$ cfx submit"]
private var sourceCapture = ["void solve() {}"]

private extension NSColor {
    convenience init(hex: UInt32, alpha: CGFloat = 1) {
        self.init(
            red: CGFloat((hex >> 16) & 0xff) / 255,
            green: CGFloat((hex >> 8) & 0xff) / 255,
            blue: CGFloat(hex & 0xff) / 255,
            alpha: alpha
        )
    }
}

private func eased(_ time: Double, from start: Double, over length: Double = 0.45) -> CGFloat {
    let value = max(0, min(1, (time - start) / length))
    return CGFloat(value * value * (3 - 2 * value))
}

private func visible(_ time: Double, from start: Double, until end: Double) -> CGFloat {
    eased(time, from: start) * (1 - eased(time, from: end - 0.45))
}

private struct Canvas {
    let context: CGContext
    let width: CGFloat
    let height: CGFloat

    func fill(_ color: NSColor) {
        context.setFillColor(color.cgColor)
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
    }

    func roundedRect(_ rect: CGRect, radius: CGFloat, color: NSColor, alpha: CGFloat = 1) {
        context.setFillColor(color.withAlphaComponent(alpha).cgColor)
        context.addPath(CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius,
                               transform: nil))
        context.fillPath()
    }

    func line(from: CGPoint, to: CGPoint, color: NSColor, width: CGFloat = 2,
              alpha: CGFloat = 1) {
        context.setStrokeColor(color.withAlphaComponent(alpha).cgColor)
        context.setLineWidth(width)
        context.move(to: from)
        context.addLine(to: to)
        context.strokePath()
    }

    func text(_ value: String, x: CGFloat, y: CGFloat, size: CGFloat,
              color: NSColor, weight: NSFont.Weight = .regular,
              monospaced: Bool = false, alpha: CGFloat = 1) {
        guard alpha > 0 else { return }
        let font = monospaced
            ? NSFont.monospacedSystemFont(ofSize: size, weight: weight)
            : NSFont.systemFont(ofSize: size, weight: weight)
        let attributed = NSAttributedString(
            string: value,
            attributes: [.font: font, .foregroundColor: color.withAlphaComponent(alpha)]
        )
        let line = CTLineCreateWithAttributedString(attributed)
        context.textPosition = CGPoint(x: x, y: height - y - size)
        CTLineDraw(line, context)
    }

    func logo(x: CGFloat, y: CGFloat, scale: CGFloat = 1, alpha: CGFloat = 1) {
        let dark = NSColor(hex: 0x182033)
        roundedRect(CGRect(x: x, y: CGFloat(height) - y - 76 * scale,
                           width: 76 * scale, height: 76 * scale),
                    radius: 17 * scale, color: dark, alpha: alpha)
        let top = height - y
        context.setLineCap(.round)
        context.setLineJoin(.round)
        context.setStrokeColor(NSColor(hex: 0xf7f9fc, alpha: alpha).cgColor)
        context.setLineWidth(7 * scale)
        context.move(to: CGPoint(x: x + 20 * scale, y: top - 22 * scale))
        context.addLine(to: CGPoint(x: x + 34 * scale, y: top - 38 * scale))
        context.addLine(to: CGPoint(x: x + 20 * scale, y: top - 54 * scale))
        context.strokePath()
        line(from: CGPoint(x: x + 43 * scale, y: top - 53 * scale),
             to: CGPoint(x: x + 59 * scale, y: top - 53 * scale),
             color: NSColor(hex: 0x50c878), width: 7 * scale, alpha: alpha)
    }
}

private let ink = NSColor(hex: 0x182033)
private let muted = NSColor(hex: 0x59657a)
private let panel = NSColor(hex: 0x111827)
private let panelTop = NSColor(hex: 0x182033)
private let terminalText = NSColor(hex: 0xcbd5e1)
private let white = NSColor(hex: 0xf7f9fc)
private let green = NSColor(hex: 0x50c878)

private func header(_ canvas: Canvas, step: String, alpha: CGFloat) {
    canvas.logo(x: 92, y: 58, scale: 0.7, alpha: alpha)
    canvas.text("cfx", x: 160, y: 73, size: 34, color: ink, weight: .bold, alpha: alpha)
    canvas.roundedRect(CGRect(x: 1552, y: 952, width: 276, height: 54), radius: 27,
                       color: NSColor(hex: 0xdce6f5), alpha: alpha)
    canvas.text(step, x: 1607, y: 76, size: 24, color: NSColor(hex: 0x33415c),
                weight: .semibold, alpha: alpha)
}

private func terminal(_ canvas: Canvas, alpha: CGFloat) {
    canvas.roundedRect(CGRect(x: 92, y: 122, width: 1736, height: 770), radius: 28,
                       color: panel, alpha: alpha)
    canvas.roundedRect(CGRect(x: 92, y: 830, width: 1736, height: 62), radius: 28,
                       color: panelTop, alpha: alpha)
    canvas.roundedRect(CGRect(x: 92, y: 830, width: 1736, height: 31), radius: 0,
                       color: panelTop, alpha: alpha)
    for (offset, color) in [(0, 0xf06a6a), (32, 0xf3c969), (64, 0x50c878)] {
        canvas.roundedRect(CGRect(x: 132 + offset, y: 855, width: 15, height: 15), radius: 8,
                           color: NSColor(hex: UInt32(color)), alpha: alpha)
    }
}

private func drawIntro(_ canvas: Canvas, time: Double) {
    let alpha = visible(time, from: 0, until: 3.0)
    canvas.logo(x: 880, y: 118, scale: 1.2, alpha: alpha)
    canvas.text("cfx", x: 990, y: 146, size: 62, color: ink, weight: .bold, alpha: alpha)
    canvas.text("the small, auditable,", x: 405, y: 350, size: 76, color: ink,
                weight: .bold, alpha: alpha)
    canvas.text("two-command Codeforces workflow.", x: 269, y: 445, size: 76,
                color: ink, weight: .bold, alpha: alpha)
    canvas.text("Local tests. Exact source. Your signed-in browser.", x: 524, y: 586,
                size: 35, color: muted, alpha: alpha)
    canvas.roundedRect(CGRect(x: 597, y: 259, width: 330, height: 76), radius: 18,
                       color: panel, alpha: alpha)
    canvas.text("$  cfx 71A", x: 632, y: 762, size: 30, color: white,
                weight: .medium, monospaced: true, alpha: alpha)
    canvas.roundedRect(CGRect(x: 947, y: 259, width: 376, height: 76), radius: 18,
                       color: panel, alpha: alpha)
    canvas.text("$  cfx submit", x: 982, y: 762, size: 30, color: white,
                weight: .medium, monospaced: true, alpha: alpha)
}

private func drawFetch(_ canvas: Canvas, time: Double) {
    let alpha = visible(time, from: 2.55, until: 7.7)
    header(canvas, step: "1  Fetch", alpha: alpha)
    terminal(canvas, alpha: alpha)
    canvas.text("problems/cf/71/A", x: 805, y: 155, size: 23, color: NSColor(hex: 0xaeb8ca),
                monospaced: true, alpha: alpha)
    let command = eased(time, from: 3.05)
    canvas.text("$", x: 145, y: 258, size: 34, color: green, monospaced: true,
                alpha: alpha * command)
    canvas.text(fetchCapture[0].replacingOccurrences(of: "$ ", with: ""),
                x: 188, y: 258, size: 34, color: white, weight: .medium,
                monospaced: true, alpha: alpha * command)
    for (index, row) in fetchCapture.dropFirst().prefix(3).enumerated() {
        let rowAlpha = alpha * eased(time, from: 4.00 + Double(index) * 0.75)
        canvas.text(row, x: 145, y: 332 + CGFloat(index * 58), size: 30,
                    color: terminalText, monospaced: true, alpha: rowAlpha)
    }
    let badge = alpha * eased(time, from: 6.15)
    canvas.roundedRect(CGRect(x: 1240, y: 238, width: 490, height: 120), radius: 20,
                       color: NSColor(hex: 0x1d293d), alpha: badge)
    canvas.roundedRect(CGRect(x: 1272, y: 287, width: 24, height: 24), radius: 12,
                       color: green, alpha: badge)
    canvas.text("Public samples imported", x: 1318, y: 706, size: 25, color: white,
                weight: .semibold, alpha: badge)
    canvas.text("No Codeforces password leaves Chrome", x: 1318, y: 748, size: 21,
                color: NSColor(hex: 0xaeb8ca), alpha: badge)
}

private func drawEditor(_ canvas: Canvas, time: Double) {
    let alpha = visible(time, from: 7.25, until: 10.75)
    header(canvas, step: "Write C++", alpha: alpha)
    canvas.roundedRect(CGRect(x: 92, y: 122, width: 1736, height: 770), radius: 28,
                       color: panel, alpha: alpha)
    canvas.roundedRect(CGRect(x: 92, y: 830, width: 1736, height: 62), radius: 28,
                       color: panelTop, alpha: alpha)
    canvas.roundedRect(CGRect(x: 92, y: 830, width: 1736, height: 31), radius: 0,
                       color: panelTop, alpha: alpha)
    canvas.text("solution.cpp", x: 145, y: 153, size: 23, color: NSColor(hex: 0xaeb8ca),
                monospaced: true, alpha: alpha)
    for (index, row) in sourceCapture.prefix(8).enumerated() {
        let color = index == 0 || index == min(sourceCapture.count, 8) - 1 ? white : terminalText
        canvas.text(row, x: 220, y: 246 + CGFloat(index * 62), size: 30,
                    color: color, monospaced: true,
                    alpha: alpha * eased(time, from: 7.55 + Double(index) * 0.12, over: 0.2))
    }
    let badge = alpha * eased(time, from: 8.75)
    canvas.roundedRect(CGRect(x: 1210, y: 575, width: 515, height: 180), radius: 22,
                       color: NSColor(hex: 0x1d293d), alpha: badge)
    canvas.text("Ordinary C++", x: 1260, y: 356, size: 32, color: white,
                weight: .bold, alpha: badge)
    canvas.text("Your editor. Your compiler.", x: 1260, y: 415, size: 24,
                color: terminalText, alpha: badge)
    canvas.text("Every source transformation is inspectable.", x: 1260, y: 460, size: 21,
                color: NSColor(hex: 0xaeb8ca), alpha: badge)
}

private func drawSubmit(_ canvas: Canvas, time: Double) {
    let alpha = visible(time, from: 10.25, until: 17.75)
    header(canvas, step: "2  Submit", alpha: alpha)
    terminal(canvas, alpha: alpha)
    canvas.text("problems/cf/71/A", x: 805, y: 155, size: 23, color: NSColor(hex: 0xaeb8ca),
                monospaced: true, alpha: alpha)
    let command = eased(time, from: 10.70)
    canvas.text("$", x: 145, y: 247, size: 34, color: green, monospaced: true,
                alpha: alpha * command)
    canvas.text(submitCapture[0].replacingOccurrences(of: "$ ", with: ""),
                x: 188, y: 247, size: 34, color: white, weight: .medium,
                monospaced: true, alpha: alpha * command)
    var rows = submitCapture.first {
        $0.range(of: #"^[0-9]+/[0-9]+ tests passed"#, options: .regularExpression) != nil
    }.map { [$0] } ?? []
    let prefixes = ["Checked build", "Submitted ", "Submission: ", "Verdict: "]
    rows += prefixes.compactMap { prefix in
        submitCapture.first { $0.hasPrefix(prefix) }
    }
    for (index, row) in rows.enumerated() {
        let isVerdict = row.hasPrefix("Verdict:")
        canvas.text(row, x: 145, y: 322 + CGFloat(index * 58), size: 30,
                    color: isVerdict ? green : terminalText,
                    weight: isVerdict ? .semibold : .regular, monospaced: true,
                    alpha: alpha * eased(time, from: 11.65 + Double(index) * 0.8))
    }
    let badge = alpha * eased(time, from: 15.2)
    canvas.roundedRect(CGRect(x: 1125, y: 225, width: 605, height: 230), radius: 22,
                       color: NSColor(hex: 0x1d293d), alpha: badge)
    canvas.roundedRect(CGRect(x: 1172, y: 371, width: 38, height: 38), radius: 19,
                       color: green, alpha: badge)
    canvas.text("✓", x: 1181, y: 672, size: 25, color: panel, weight: .bold, alpha: badge)
    canvas.text("Exact tested source", x: 1235, y: 653, size: 31, color: white,
                weight: .bold, alpha: badge)
    canvas.text("Bundled, hashed, then submitted", x: 1172, y: 720, size: 24,
                color: terminalText, alpha: badge)
    canvas.text("Codeforces session stays in Chrome", x: 1172, y: 765, size: 22,
                color: NSColor(hex: 0xaeb8ca), alpha: badge)
}

private func drawFinal(_ canvas: Canvas, time: Double) {
    let alpha = eased(time, from: 17.25, over: 0.65)
    canvas.logo(x: 164, y: 142, scale: 1.1, alpha: alpha)
    canvas.text("cfx", x: 270, y: 174, size: 56, color: ink, weight: .bold, alpha: alpha)
    canvas.text("the small, auditable,", x: 164, y: 350, size: 74, color: ink,
                weight: .bold, alpha: alpha)
    canvas.text("two-command Codeforces workflow.", x: 164, y: 444, size: 74,
                color: ink, weight: .bold, alpha: alpha)
    canvas.roundedRect(CGRect(x: 164, y: 318, width: 680, height: 104), radius: 20,
                       color: panel, alpha: alpha)
    canvas.text("$  cfx 71A", x: 208, y: 684, size: 38, color: white,
                weight: .medium, monospaced: true, alpha: alpha)
    canvas.roundedRect(CGRect(x: 876, y: 318, width: 780, height: 104), radius: 20,
                       color: panel, alpha: alpha)
    canvas.text("$  cfx submit", x: 920, y: 684, size: 38, color: white,
                weight: .medium, monospaced: true, alpha: alpha)
    canvas.text("No cfx account. No credential storage. No analytics.", x: 164, y: 832,
                size: 31, color: muted, alpha: alpha)
    canvas.roundedRect(CGRect(x: 1590, y: 94, width: 166, height: 54), radius: 27,
                       color: NSColor(hex: 0xdff5e7), alpha: alpha)
    canvas.text("Open source", x: 1614, y: 946, size: 23, color: NSColor(hex: 0x19733b),
                weight: .semibold, alpha: alpha)
}

private func drawFrame(_ buffer: CVPixelBuffer, time: Double) {
    CVPixelBufferLockBaseAddress(buffer, [])
    defer { CVPixelBufferUnlockBaseAddress(buffer, []) }
    guard let base = CVPixelBufferGetBaseAddress(buffer),
          let context = CGContext(
              data: base,
              width: width,
              height: height,
              bitsPerComponent: 8,
              bytesPerRow: CVPixelBufferGetBytesPerRow(buffer),
              space: CGColorSpaceCreateDeviceRGB(),
              bitmapInfo: CGBitmapInfo.byteOrder32Little.rawValue |
                  CGImageAlphaInfo.premultipliedFirst.rawValue
          ) else {
        fatalError("cannot create video frame")
    }
    let canvas = Canvas(context: context, width: CGFloat(width), height: CGFloat(height))
    canvas.fill(NSColor(hex: 0xeef2f7))
    drawIntro(canvas, time: time)
    drawFetch(canvas, time: time)
    drawEditor(canvas, time: time)
    drawSubmit(canvas, time: time)
    drawFinal(canvas, time: time)
}

private func sections(in capture: String) throws -> [String: [String]] {
    var result = [String: [String]]()
    var section: String?
    for line in capture.split(separator: "\n", omittingEmptySubsequences: false).map(String.init) {
        if line.hasPrefix("[") && line.hasSuffix("]") {
            section = String(line.dropFirst().dropLast())
            result[section!] = []
        } else if let section, !line.isEmpty {
            result[section, default: []].append(line)
        }
    }
    guard result["fetch"]?.count ?? 0 >= 4,
          result["submit"]?.count ?? 0 >= 6,
          result["source"]?.count ?? 0 >= 2 else {
        throw NSError(domain: "cfx-demo", code: 7,
                      userInfo: [NSLocalizedDescriptionKey: "capture is incomplete"])
    }
    return result
}

private func loadCapture(_ capture: URL) throws {
    let content = try String(contentsOf: capture, encoding: .utf8)
    let captured = try sections(in: content)
    fetchCapture = captured["fetch"]!
    submitCapture = captured["submit"]!
    sourceCapture = captured["source"]!
}

private func writePng(width pixelWidth: Int, height pixelHeight: Int, to output: URL,
                      draw: (Canvas) -> Void) throws {
    let bytesPerRow = pixelWidth * 4
    let storage = UnsafeMutableRawPointer.allocate(byteCount: bytesPerRow * pixelHeight,
                                                   alignment: 64)
    defer { storage.deallocate() }
    storage.initializeMemory(as: UInt8.self, repeating: 0, count: bytesPerRow * pixelHeight)
    guard let context = CGContext(
        data: storage,
        width: pixelWidth,
        height: pixelHeight,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGBitmapInfo.byteOrder32Little.rawValue |
            CGImageAlphaInfo.premultipliedFirst.rawValue
    ) else {
        throw NSError(domain: "cfx-demo", code: 8)
    }
    draw(Canvas(context: context, width: CGFloat(pixelWidth), height: CGFloat(pixelHeight)))
    guard let image = context.makeImage(),
          let data = NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])
    else {
        throw NSError(domain: "cfx-demo", code: 9)
    }
    try FileManager.default.createDirectory(at: output.deletingLastPathComponent(),
                                            withIntermediateDirectories: true)
    try data.write(to: output, options: .atomic)
}

private func drawCapturedStill(_ target: Canvas) {
    let sourceWidth = width
    let sourceHeight = height
    let bytesPerRow = sourceWidth * 4
    let storage = UnsafeMutableRawPointer.allocate(byteCount: bytesPerRow * sourceHeight,
                                                   alignment: 64)
    defer { storage.deallocate() }
    storage.initializeMemory(as: UInt8.self, repeating: 0, count: bytesPerRow * sourceHeight)
    guard let context = CGContext(
        data: storage,
        width: sourceWidth,
        height: sourceHeight,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGBitmapInfo.byteOrder32Little.rawValue |
            CGImageAlphaInfo.premultipliedFirst.rawValue
    ) else { return }
    let source = Canvas(context: context, width: CGFloat(sourceWidth), height: CGFloat(sourceHeight))
    source.fill(NSColor(hex: 0xeef2f7))
    drawSubmit(source, time: 16.7)
    guard let image = context.makeImage() else { return }
    target.fill(NSColor(hex: 0xeef2f7))
    target.context.interpolationQuality = .high
    target.context.draw(image, in: CGRect(x: 0, y: 40, width: 1280, height: 720))
}

private func writeStoreAssets(capture: URL, to directory: URL) throws {
    try loadCapture(capture)

    try writePng(width: 1280, height: 800,
                 to: directory.appendingPathComponent("screenshot-1280x800.png"),
                 draw: drawCapturedStill)
    try writePng(width: 440, height: 280,
                 to: directory.appendingPathComponent("promo-small-440x280.png")) { canvas in
        canvas.fill(ink)
        canvas.logo(x: 32, y: 27, scale: 0.58)
        canvas.text("cfx", x: 90, y: 39, size: 26, color: white, weight: .bold)
        canvas.text("Codeforces in", x: 32, y: 96, size: 31, color: white, weight: .bold)
        canvas.text("two commands.", x: 32, y: 134, size: 31, color: white, weight: .bold)
        canvas.roundedRect(CGRect(x: 32, y: 52, width: 166, height: 44), radius: 10,
                           color: panel)
        canvas.text("$ cfx 71A", x: 50, y: 187, size: 17, color: white,
                    weight: .medium, monospaced: true)
        canvas.roundedRect(CGRect(x: 210, y: 52, width: 198, height: 44), radius: 10,
                           color: panel)
        canvas.text("$ cfx submit", x: 228, y: 187, size: 17, color: white,
                    weight: .medium, monospaced: true)
        canvas.text("Local tests. Exact source. Signed-in Chrome.", x: 32, y: 248,
                    size: 14, color: NSColor(hex: 0xaeb8ca))
    }
    try writePng(width: 1400, height: 560,
                 to: directory.appendingPathComponent("promo-marquee-1400x560.png")) { canvas in
        canvas.fill(ink)
        canvas.logo(x: 80, y: 76, scale: 1.05)
        canvas.text("cfx", x: 180, y: 105, size: 48, color: white, weight: .bold)
        canvas.text("The small, auditable,", x: 80, y: 206, size: 46,
                    color: white, weight: .bold)
        canvas.text("two-command", x: 80, y: 268, size: 46,
                    color: white, weight: .bold)
        canvas.text("Codeforces workflow.", x: 80, y: 330, size: 46,
                    color: white, weight: .bold)
        canvas.roundedRect(CGRect(x: 880, y: 316, width: 390, height: 78), radius: 18,
                           color: panel)
        canvas.text("$  cfx 71A", x: 926, y: 184, size: 30, color: white,
                    weight: .medium, monospaced: true)
        canvas.roundedRect(CGRect(x: 880, y: 206, width: 390, height: 78), radius: 18,
                           color: panel)
        canvas.text("$  cfx submit", x: 926, y: 294, size: 30, color: white,
                    weight: .medium, monospaced: true)
        canvas.text("Local tests · exact bundle · signed-in browser", x: 80, y: 420,
                    size: 26, color: NSColor(hex: 0xaeb8ca))
    }
}

private func render(capture: URL, to output: URL) throws {
    try loadCapture(capture)
    try? FileManager.default.removeItem(at: output)
    try FileManager.default.createDirectory(at: output.deletingLastPathComponent(),
                                            withIntermediateDirectories: true)

    let writer = try AVAssetWriter(outputURL: output, fileType: .mp4)
    let input = AVAssetWriterInput(
        mediaType: .video,
        outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: width,
            AVVideoHeightKey: height,
            AVVideoCompressionPropertiesKey: [
                AVVideoAverageBitRateKey: 4_000_000,
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel
            ]
        ]
    )
    input.expectsMediaDataInRealTime = false
    let adaptor = AVAssetWriterInputPixelBufferAdaptor(
        assetWriterInput: input,
        sourcePixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height
        ]
    )
    guard writer.canAdd(input) else { throw NSError(domain: "cfx-demo", code: 1) }
    writer.add(input)
    guard writer.startWriting() else { throw writer.error ?? NSError(domain: "cfx-demo", code: 2) }
    writer.startSession(atSourceTime: .zero)

    let frameCount = framesPerSecond * duration
    for frame in 0..<frameCount {
        while !input.isReadyForMoreMediaData { Thread.sleep(forTimeInterval: 0.002) }
        guard let pool = adaptor.pixelBufferPool else {
            throw NSError(domain: "cfx-demo", code: 3)
        }
        var optionalBuffer: CVPixelBuffer?
        guard CVPixelBufferPoolCreatePixelBuffer(nil, pool, &optionalBuffer) == kCVReturnSuccess,
              let buffer = optionalBuffer else {
            throw NSError(domain: "cfx-demo", code: 4)
        }
        drawFrame(buffer, time: Double(frame) / Double(framesPerSecond))
        let timestamp = CMTime(value: CMTimeValue(frame), timescale: framesPerSecond)
        guard adaptor.append(buffer, withPresentationTime: timestamp) else {
            throw writer.error ?? NSError(domain: "cfx-demo", code: 5)
        }
    }

    writer.endSession(atSourceTime: CMTime(value: CMTimeValue(frameCount),
                                           timescale: framesPerSecond))
    input.markAsFinished()
    let semaphore = DispatchSemaphore(value: 0)
    writer.finishWriting { semaphore.signal() }
    semaphore.wait()
    guard writer.status == .completed else {
        throw writer.error ?? NSError(domain: "cfx-demo", code: 6)
    }
}

private func verifyPng(_ directory: URL, _ name: String, _ expected: NSSize) throws {
    let url = directory.appendingPathComponent(name)
    guard let image = try NSBitmapImageRep(data: Data(contentsOf: url)),
          image.pixelsWide == Int(expected.width), image.pixelsHigh == Int(expected.height)
    else {
        throw NSError(domain: "cfx-demo", code: 10,
                      userInfo: [NSLocalizedDescriptionKey: "invalid dimensions for \(name)"])
    }
}

private func verifyAssets(in directory: URL) async throws {
    try verifyPng(directory, "icon-128.png", NSSize(width: 128, height: 128))
    try verifyPng(directory, "screenshot-1280x800.png", NSSize(width: 1280, height: 800))
    try verifyPng(directory, "promo-small-440x280.png", NSSize(width: 440, height: 280))
    try verifyPng(directory, "promo-marquee-1400x560.png", NSSize(width: 1400, height: 560))

    let asset = AVURLAsset(url: directory.appendingPathComponent("demo-20s.mp4"))
    let videoDuration = try await asset.load(.duration)
    let tracks = try await asset.loadTracks(withMediaType: .video)
    let audioTracks = try await asset.loadTracks(withMediaType: .audio)
    guard CMTimeCompare(videoDuration, CMTime(value: 20, timescale: 1)) == 0,
          tracks.count == 1, audioTracks.isEmpty else {
        throw NSError(domain: "cfx-demo", code: 11,
                      userInfo: [NSLocalizedDescriptionKey:
                          "video must be exactly 20 seconds with no audio track"])
    }
    let videoSize = try await tracks[0].load(.naturalSize)
    guard videoSize == NSSize(width: 1920, height: 1080) else {
        throw NSError(domain: "cfx-demo", code: 12,
                      userInfo: [NSLocalizedDescriptionKey: "video is not 1920 x 1080"])
    }
}

@main
private struct DemoRenderer {
    static func main() async {
        do {
            if CommandLine.arguments.count == 3, CommandLine.arguments[1] == "--verify" {
                try await verifyAssets(
                    in: URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
                )
                print("verified: silent 20.000s at 1920x1080; listing image dimensions are exact")
            } else if CommandLine.arguments.count == 4, CommandLine.arguments[1] == "--assets" {
                try writeStoreAssets(
                    capture: URL(fileURLWithPath: CommandLine.arguments[2]),
                    to: URL(fileURLWithPath: CommandLine.arguments[3], isDirectory: true)
                )
            } else if CommandLine.arguments.count == 3 {
                try render(capture: URL(fileURLWithPath: CommandLine.arguments[1]),
                           to: URL(fileURLWithPath: CommandLine.arguments[2]))
            } else {
                fputs("usage: render [--assets CAPTURE | --verify] OUTPUT\n", stderr)
                exit(2)
            }
        } catch {
            fputs("demo render: \(error)\n", stderr)
            exit(1)
        }
    }
}
