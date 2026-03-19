import Foundation
import SwiftUI

// MARK: - ANSI Color Table

enum AnsiParser {
    static let colors16: [Color] = [
        Color(red: 0, green: 0, blue: 0),           // 0 black
        Color(red: 0.67, green: 0, blue: 0),         // 1 red
        Color(red: 0, green: 0.67, blue: 0),         // 2 green
        Color(red: 0.67, green: 0.33, blue: 0),      // 3 yellow/brown
        Color(red: 0, green: 0, blue: 0.67),         // 4 blue
        Color(red: 0.67, green: 0, blue: 0.67),      // 5 magenta
        Color(red: 0, green: 0.67, blue: 0.67),      // 6 cyan
        Color(red: 0.67, green: 0.67, blue: 0.67),   // 7 white
        Color(red: 0.33, green: 0.33, blue: 0.33),   // 8 bright black
        Color(red: 1, green: 0.33, blue: 0.33),      // 9 bright red
        Color(red: 0.33, green: 1, blue: 0.33),      // 10 bright green
        Color(red: 1, green: 1, blue: 0.33),         // 11 bright yellow
        Color(red: 0.33, green: 0.33, blue: 1),      // 12 bright blue
        Color(red: 1, green: 0.33, blue: 1),         // 13 bright magenta
        Color(red: 0.33, green: 1, blue: 1),         // 14 bright cyan
        Color(red: 1, green: 1, blue: 1),            // 15 bright white
    ]

    static func xterm256(_ idx: Int) -> Color {
        if idx < 16 { return colors16[idx] }
        if idx < 232 {
            let n = idx - 16
            let r = Double(n / 36) / 5.0
            let g = Double((n / 6) % 6) / 5.0
            let b = Double(n % 6) / 5.0
            return Color(red: r, green: g, blue: b)
        }
        let v = Double(idx - 232) * 10.0 / 255.0 + 8.0 / 255.0
        return Color(red: v, green: v, blue: v)
    }

    // MARK: - Strip ANSI codes

    static func stripAnsi(_ text: String) -> String {
        text.replacing(/\x1b\[[0-9;]*[A-Za-z]/, with: "")
    }

    // MARK: - URL detection

    private static let urlPattern = /https?:\/\/[^\s<>"']+/

    // MARK: - Parse ANSI → AttributedString

    static func parse(_ text: String) -> AttributedString {
        var result = AttributedString()
        var fg: Color? = nil
        var bg: Color? = nil
        var bold = false
        var underline = false
        var inverse = false

        var i = text.startIndex

        func currentFG() -> Color {
            if inverse { return bg ?? Color(red: 0, green: 0, blue: 0) }
            return fg ?? Color(red: 0.75, green: 0.75, blue: 0.75)
        }

        func currentBG() -> Color? {
            if inverse { return fg ?? Color(red: 0.75, green: 0.75, blue: 0.75) }
            return bg
        }

        var pending = ""

        func flushPending() {
            if pending.isEmpty { return }
            var segment = AttributedString(pending)
            segment.foregroundColor = currentFG()
            if let b = currentBG() { segment.backgroundColor = b }
            if bold { segment.font = .system(.body, design: .monospaced).bold() }
            else { segment.font = .system(.body, design: .monospaced) }
            if underline { segment.underlineStyle = .single }
            result.append(segment)
            pending = ""
        }

        while i < text.endIndex {
            if text[i] == "\u{1b}" {
                let next = text.index(after: i)
                if next < text.endIndex && text[next] == "[" {
                    flushPending()

                    // Parse CSI sequence
                    var j = text.index(after: next)
                    var params = ""
                    while j < text.endIndex && (text[j].isNumber || text[j] == ";") {
                        params.append(text[j])
                        j = text.index(after: j)
                    }
                    if j < text.endIndex { j = text.index(after: j) } // skip final byte

                    let codes = params.isEmpty ? [0] : params.split(separator: ";").map { Int($0) ?? 0 }
                    var k = 0
                    while k < codes.count {
                        let c = codes[k]
                        switch c {
                        case 0: fg = nil; bg = nil; bold = false; underline = false; inverse = false
                        case 1: bold = true
                        case 4: underline = true
                        case 7: inverse = true
                        case 22: bold = false
                        case 24: underline = false
                        case 27: inverse = false
                        case 30...37: fg = colors16[c - 30 + (bold ? 8 : 0)]
                        case 38:
                            if k + 1 < codes.count && codes[k + 1] == 5 && k + 2 < codes.count {
                                fg = xterm256(codes[k + 2]); k += 2
                            } else if k + 1 < codes.count && codes[k + 1] == 2 && k + 4 < codes.count {
                                fg = Color(red: Double(codes[k+2])/255, green: Double(codes[k+3])/255, blue: Double(codes[k+4])/255); k += 4
                            }
                        case 39: fg = nil
                        case 40...47: bg = colors16[c - 40]
                        case 48:
                            if k + 1 < codes.count && codes[k + 1] == 5 && k + 2 < codes.count {
                                bg = xterm256(codes[k + 2]); k += 2
                            } else if k + 1 < codes.count && codes[k + 1] == 2 && k + 4 < codes.count {
                                bg = Color(red: Double(codes[k+2])/255, green: Double(codes[k+3])/255, blue: Double(codes[k+4])/255); k += 4
                            }
                        case 49: bg = nil
                        case 90...97: fg = colors16[c - 90 + 8]
                        case 100...107: bg = colors16[c - 100 + 8]
                        default: break
                        }
                        k += 1
                    }
                    i = j
                    continue
                }
            }
            pending.append(text[i])
            i = text.index(after: i)
        }
        flushPending()

        // Add URL annotations
        let plain = String(result.characters)
        for match in plain.matches(of: urlPattern) {
            let url = String(match.output)
            if let range = result.range(of: url) {
                result[range].link = URL(string: url)
                result[range].foregroundColor = Color(red: 0.4, green: 0.6, blue: 1.0)
                result[range].underlineStyle = .single
            }
        }

        return result
    }
}
