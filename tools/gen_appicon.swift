// Renders the TrailDashLink iOS app icon (1024x1024, opaque PNG).
//
//   swift tools/gen_appicon.swift ios/TrailDashLink/Assets.xcassets/AppIcon.appiconset/AppIcon.png
//
// Same visual language as the TrailDash UI: dark dial, green/amber/red tilt
// scale, cyan pointer and the tilted rear-view SUV.
import Foundation
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

let S: CGFloat = 1024
let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "AppIcon.png"

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: a)
}
let space = CGColorSpaceCreateDeviceRGB()
let ctx = CGContext(data: nil, width: Int(S), height: Int(S), bitsPerComponent: 8, bytesPerRow: 0,
                    space: space, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
// y down, like the firmware
ctx.translateBy(x: 0, y: S)
ctx.scaleBy(x: 1, y: -1)

let GREEN: UInt32 = 0x2EE6A6, AMBER: UInt32 = 0xFFB020, RED: UInt32 = 0xFF3B5C, ACCENT: UInt32 = 0x38D9F5

// ---- background ----
let bg = CGGradient(colorsSpace: space, colors: [rgb(0x1C2D3F), rgb(0x0B121B), rgb(0x04070B)] as CFArray,
                    locations: [0, 0.6, 1])!
ctx.drawRadialGradient(bg, startCenter: CGPoint(x: 512, y: 420), startRadius: 0,
                       endCenter: CGPoint(x: 512, y: 420), endRadius: 820, options: [.drawsAfterEndLocation])

// ---- dial: angle 0 = 12 o'clock, clockwise ----
let C = CGPoint(x: 512, y: 560)
func pt(_ r: CGFloat, _ deg: CGFloat) -> CGPoint {
    let a = deg * .pi / 180
    return CGPoint(x: C.x + r * sin(a), y: C.y - r * cos(a))
}
func band(_ r0: CGFloat, _ r1: CGFloat, _ a0: CGFloat, _ a1: CGFloat, _ c: CGColor) {
    let p = CGMutablePath()
    let n = max(2, Int(abs(a1 - a0) / 1.5))
    for i in 0...n { let a = a0 + (a1 - a0) * CGFloat(i) / CGFloat(n); i == 0 ? p.move(to: pt(r1, a)) : p.addLine(to: pt(r1, a)) }
    for i in (0...n).reversed() { p.addLine(to: pt(r0, a0 + (a1 - a0) * CGFloat(i) / CGFloat(n))) }
    p.closeSubpath()
    ctx.addPath(p); ctx.setFillColor(c); ctx.fillPath()
}
let R0: CGFloat = 372, R1: CGFloat = 404
band(R0, R1, -112, 112, rgb(0x1A2633))                 // track
band(R0, R1, -30, 30, rgb(GREEN, 0.85))
band(R0, R1, 30, 62, rgb(AMBER, 0.9)); band(R0, R1, -62, -30, rgb(AMBER, 0.9))
band(R0, R1, 62, 112, rgb(RED, 0.9)); band(R0, R1, -112, -62, rgb(RED, 0.9))
// ticks
ctx.setLineCap(.round)
for a in stride(from: -110, through: 110, by: 10) {
    let major = a % 30 == 0
    ctx.setStrokeColor(rgb(major ? 0xA9B6C3 : 0x52606E))
    ctx.setLineWidth(major ? 9 : 5)
    ctx.move(to: pt(major ? 318 : 334, CGFloat(a))); ctx.addLine(to: pt(356, CGFloat(a)))
    ctx.strokePath()
}

// ---- pointer at the current tilt ----
let tilt: CGFloat = 18
do {
    let p = CGMutablePath()
    p.move(to: pt(352, tilt)); p.addLine(to: pt(420, tilt - 4.6)); p.addLine(to: pt(420, tilt + 4.6)); p.closeSubpath()
    ctx.saveGState()
    ctx.setShadow(offset: .zero, blur: 28, color: rgb(ACCENT, 0.9))
    ctx.addPath(p); ctx.setFillColor(rgb(ACCENT)); ctx.fillPath()
    ctx.restoreGState()
}

// ---- vehicle (rear view, local coords: ground y=0, y down), tilted with the pointer ----
func rr(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat, _ r: CGFloat, _ c: CGColor) {
    ctx.addPath(CGPath(roundedRect: CGRect(x: x, y: y, width: w, height: h), cornerWidth: r, cornerHeight: r, transform: nil))
    ctx.setFillColor(c); ctx.fillPath()
}
func poly(_ pts: [(CGFloat, CGFloat)], _ c: CGColor) {
    ctx.move(to: CGPoint(x: pts[0].0, y: pts[0].1)); for p in pts.dropFirst() { ctx.addLine(to: CGPoint(x: p.0, y: p.1)) }
    ctx.closePath(); ctx.setFillColor(c); ctx.fillPath()
}
func vgrad(_ pts: [(CGFloat, CGFloat)], _ y0: CGFloat, _ c0: UInt32, _ y1: CGFloat, _ c1: UInt32) {
    ctx.saveGState()
    ctx.move(to: CGPoint(x: pts[0].0, y: pts[0].1)); for p in pts.dropFirst() { ctx.addLine(to: CGPoint(x: p.0, y: p.1)) }
    ctx.closePath(); ctx.clip()
    let g = CGGradient(colorsSpace: space, colors: [rgb(c0), rgb(c1)] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(g, start: CGPoint(x: 0, y: y0), end: CGPoint(x: 0, y: y1), options: [])
    ctx.restoreGState()
}
func circle(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat, _ c: CGColor) {
    ctx.addEllipse(in: CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r)); ctx.setFillColor(c); ctx.fillPath()
}

ctx.saveGState()
ctx.translateBy(x: C.x, y: C.y + 34)
ctx.rotate(by: tilt * .pi / 180)
ctx.scaleBy(x: 2.75, y: 2.75)
ctx.translateBy(x: 0, y: 52)        // pivot (0,-52) -> dial centre
// slope under the wheels
do {
    ctx.saveGState()
    ctx.addRect(CGRect(x: -92, y: 3, width: 184, height: 16)); ctx.clip()
    let g = CGGradient(colorsSpace: space, colors: [rgb(ACCENT, 0.55), rgb(ACCENT, 0)] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(g, start: CGPoint(x: 0, y: 3), end: CGPoint(x: 0, y: 19), options: [])
    ctx.restoreGState()
    rr(-92, 1.2, 184, 3, 1.5, rgb(ACCENT))
}
// soft shadow behind the body
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: 6), blur: 18, color: rgb(0x000000, 0.6))
// tyres
for x0: CGFloat in [-64, 38] {
    rr(x0, -32, 26, 34, 7, rgb(0x16191E))
    rr(x0 + 3, -29, 20, 28, 5, rgb(0x262B33))
}
ctx.restoreGState()
rr(-40, -21, 80, 6, 2, rgb(0x2A3139))
circle(0, -18, 9, rgb(0x343B44))
// lower body + cabin
vgrad([(-68, -60), (68, -60), (68, -24), (-68, -24)], -60, 0xF2F6FA, -24, 0xA3AEBA)
rr(-70, -31, 140, 9, 3, rgb(0x262C33))
vgrad([(-60, -59), (-49, -100), (49, -100), (60, -59)], -100, 0xFFFFFF, -59, 0xCBD4DD)
vgrad([(-50, -65), (-42, -94), (42, -94), (50, -65)], -94, 0x35587A, -65, 0x132030)
poly([(-30, -94), (-18, -94), (-34, -65), (-46, -65)], rgb(0xFFFFFF, 0.12))
// roof rack
rr(-46, -107, 92, 5, 1.5, rgb(0x2A3139))
rr(-40, -103, 4, 4, 1, rgb(0x2A3139)); rr(36, -103, 4, 4, 1, rgb(0x2A3139))
// tail lights
rr(-66, -56, 11, 16, 3, rgb(0xE5173F)); rr(55, -56, 11, 16, 3, rgb(0xE5173F))
rr(-64, -54, 4, 6, 1.5, rgb(0xFF8A9E)); rr(60, -54, 4, 6, 1.5, rgb(0xFF8A9E))
// spare wheel
circle(0, -42, 15, rgb(0x16191E))
circle(0, -42, 8, rgb(0x8D99A6))
circle(0, -42, 3, rgb(0x2A3139))
ctx.restoreGState()

// ---- write PNG ----
let img = ctx.makeImage()!
let url = URL(fileURLWithPath: out)
let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dest, img, nil)
guard CGImageDestinationFinalize(dest) else { fatalError("write failed") }
print("wrote \(out)")
