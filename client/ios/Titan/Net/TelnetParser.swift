import Foundation

// MARK: - Telnet Constants

enum TelnetCommand: UInt8 {
    case se   = 240  // End of subnegotiation
    case nop  = 241  // No operation
    case dm   = 242  // Data mark
    case brk  = 243  // Break
    case ip   = 244  // Interrupt process
    case ao   = 245  // Abort output
    case ayt  = 246  // Are you there
    case ec   = 247  // Erase character
    case el   = 248  // Erase line
    case ga   = 249  // Go ahead
    case sb   = 250  // Subnegotiation begin
    case will = 251
    case wont = 252
    case `do` = 253
    case dont = 254
    case iac  = 255
}

enum TelnetOption: UInt8 {
    case binary    = 0
    case echo      = 1
    case sga       = 3
    case ttype     = 24
    case naws      = 31
    case charset   = 42
    case compress  = 85   // MCCP v1
    case compress2 = 86   // MCCP v2
    case gmcp      = 201
}

// MARK: - Telnet Parser

struct TelnetParser {
    var onText: ((Data) -> Void)?
    var onLine: ((String) -> Void)?
    var onPrompt: ((String) -> Void)?
    var sendRaw: ((Data) -> Void)?

    var nawsWidth: Int = 80
    var nawsHeight: Int = 24
    var remoteEcho: Bool = false

    private enum State {
        case normal
        case iac
        case will, wont, `do`, dont
        case sb
        case sbData
        case sbIAC
    }

    private var state: State = .normal
    private var textBuffer = Data()
    private var lineBuffer = Data()
    private var subOption: UInt8 = 0
    private var subData = Data()

    // MARK: - Process incoming bytes

    mutating func process(_ data: Data) {
        for byte in data {
            switch state {
            case .normal:
                if byte == TelnetCommand.iac.rawValue {
                    state = .iac
                } else {
                    textBuffer.append(byte)
                    if byte == 0x0A { // newline
                        flushLine()
                    }
                }

            case .iac:
                switch byte {
                case TelnetCommand.iac.rawValue:
                    // Escaped 0xFF — literal byte
                    textBuffer.append(byte)
                    state = .normal
                case TelnetCommand.will.rawValue:
                    state = .will
                case TelnetCommand.wont.rawValue:
                    state = .wont
                case TelnetCommand.do.rawValue:
                    state = .do
                case TelnetCommand.dont.rawValue:
                    state = .dont
                case TelnetCommand.sb.rawValue:
                    state = .sb
                case TelnetCommand.ga.rawValue, 239: // GA or EOR
                    flushPrompt()
                    state = .normal
                case TelnetCommand.nop.rawValue:
                    state = .normal
                default:
                    state = .normal
                }

            case .will:
                handleWill(option: byte)
                state = .normal

            case .wont:
                handleWont(option: byte)
                state = .normal

            case .do:
                handleDo(option: byte)
                state = .normal

            case .dont:
                handleDont(option: byte)
                state = .normal

            case .sb:
                subOption = byte
                subData = Data()
                state = .sbData

            case .sbData:
                if byte == TelnetCommand.iac.rawValue {
                    state = .sbIAC
                } else {
                    subData.append(byte)
                }

            case .sbIAC:
                if byte == TelnetCommand.se.rawValue {
                    handleSubnegotiation(option: subOption, data: subData)
                    state = .normal
                } else if byte == TelnetCommand.iac.rawValue {
                    // Escaped 0xFF within subnegotiation
                    subData.append(byte)
                    state = .sbData
                } else {
                    state = .normal
                }
            }
        }

        // Flush any remaining text
        if !textBuffer.isEmpty {
            onText?(textBuffer)
            textBuffer.removeAll()
        }
    }

    // MARK: - Line handling

    private mutating func flushLine() {
        var line = textBuffer
        textBuffer.removeAll()

        // Strip trailing CR/LF
        while let last = line.last, last == 0x0A || last == 0x0D {
            line.removeLast()
        }

        if let str = String(data: line, encoding: .utf8)
            ?? String(data: line, encoding: .isoLatin1) {
            onLine?(str)
        }
    }

    private mutating func flushPrompt() {
        if textBuffer.isEmpty { return }
        let prompt = textBuffer
        textBuffer.removeAll()

        if let str = String(data: prompt, encoding: .utf8)
            ?? String(data: prompt, encoding: .isoLatin1) {
            onPrompt?(str)
        }
    }

    // MARK: - Option negotiation

    private mutating func handleWill(option: UInt8) {
        switch option {
        case TelnetOption.echo.rawValue:
            remoteEcho = true
            send(command: .do, option: option)
        case TelnetOption.sga.rawValue:
            send(command: .do, option: option)
        case TelnetOption.gmcp.rawValue:
            send(command: .do, option: option)
        default:
            send(command: .dont, option: option)
        }
    }

    private mutating func handleWont(option: UInt8) {
        if option == TelnetOption.echo.rawValue {
            remoteEcho = false
        }
        send(command: .dont, option: option)
    }

    private func handleDo(option: UInt8) {
        switch option {
        case TelnetOption.naws.rawValue:
            send(command: .will, option: option)
            sendNaws()
        case TelnetOption.ttype.rawValue:
            send(command: .will, option: option)
        case TelnetOption.charset.rawValue:
            send(command: .will, option: option)
        default:
            send(command: .wont, option: option)
        }
    }

    private func handleDont(option: UInt8) {
        send(command: .wont, option: option)
    }

    // MARK: - Subnegotiation

    private func handleSubnegotiation(option: UInt8, data: Data) {
        switch option {
        case TelnetOption.ttype.rawValue:
            if data.first == 1 { // SEND
                let name = "Titan".data(using: .ascii)!
                var response = Data([TelnetCommand.iac.rawValue, TelnetCommand.sb.rawValue,
                                     TelnetOption.ttype.rawValue, 0]) // IS
                response.append(name)
                response.append(contentsOf: [TelnetCommand.iac.rawValue, TelnetCommand.se.rawValue])
                sendRaw?(response)
            }

        case TelnetOption.charset.rawValue:
            if data.first == 1 { // REQUEST
                // Accept UTF-8 if offered
                let offered = String(data: data.dropFirst(), encoding: .ascii) ?? ""
                if offered.uppercased().contains("UTF-8") {
                    var response = Data([TelnetCommand.iac.rawValue, TelnetCommand.sb.rawValue,
                                         TelnetOption.charset.rawValue, 2]) // ACCEPTED
                    response.append("UTF-8".data(using: .ascii)!)
                    response.append(contentsOf: [TelnetCommand.iac.rawValue, TelnetCommand.se.rawValue])
                    sendRaw?(response)
                }
            }

        default:
            break
        }
    }

    // MARK: - Send helpers

    private func send(command: TelnetCommand, option: UInt8) {
        sendRaw?(Data([TelnetCommand.iac.rawValue, command.rawValue, option]))
    }

    func sendNaws() {
        var data = Data([TelnetCommand.iac.rawValue, TelnetCommand.sb.rawValue,
                         TelnetOption.naws.rawValue])
        // Width (big-endian, escape 0xFF)
        let wh = UInt8(nawsWidth >> 8)
        let wl = UInt8(nawsWidth & 0xFF)
        let hh = UInt8(nawsHeight >> 8)
        let hl = UInt8(nawsHeight & 0xFF)
        for byte in [wh, wl, hh, hl] {
            data.append(byte)
            if byte == 0xFF { data.append(0xFF) }
        }
        data.append(contentsOf: [TelnetCommand.iac.rawValue, TelnetCommand.se.rawValue])
        sendRaw?(data)
    }

    func sendLine(_ text: String) {
        guard var data = text.data(using: .utf8) else { return }
        data.append(contentsOf: [0x0D, 0x0A])
        sendRaw?(data)
    }
}
