import Foundation
import Network
import Security

// MARK: - Certificate Info (for TOFU)

struct CertInfo {
    let host: String
    let port: Int
    let fingerprint: String
    let subject: String
    let issuer: String
    let savedFingerprint: String?
}

// MARK: - MUD Connection

@MainActor
class MudConnection: ObservableObject {
    let name: String
    let host: String
    let port: Int
    let useSsl: Bool
    let certStore: TofuCertStore?

    @Published var connected = false
    var telnet: TelnetParser

    var onLine: ((String) -> Void)?
    var onConnect: (() -> Void)?
    var onDisconnect: (() -> Void)?
    var onCertVerify: ((CertInfo) async -> Bool)?

    private var connection: NWConnection?
    private var readTask: Task<Void, Never>?

    init(name: String, host: String, port: Int, useSsl: Bool, certStore: TofuCertStore? = nil) {
        self.name = name
        self.host = host
        self.port = port
        self.useSsl = useSsl
        self.certStore = certStore
        self.telnet = TelnetParser()
    }

    func connect() {
        let tlsParams: NWParameters
        if useSsl {
            let host = self.host
            let port = self.port
            let certStore = self.certStore
            let onCertVerify = self.onCertVerify

            tlsParams = NWParameters(tls: {
                let options = NWProtocolTLS.Options()
                sec_protocol_options_set_verify_block(
                    options.securityProtocolOptions,
                    { metadata, trust, completion in
                        // Extract certificate for TOFU
                        let secTrust = sec_trust_copy_ref(trust).takeRetainedValue()
                        guard let certChain = SecTrustCopyCertificateChain(secTrust) as? [SecCertificate],
                              let cert = certChain.first else {
                            completion(true) // No cert — allow (non-TLS fallback)
                            return
                        }

                        let certData = SecCertificateCopyData(cert) as Data
                        let fingerprint = TofuCertStore.fingerprint(certData)
                        let saved = certStore?.getFingerprint(host: host, port: port)

                        if let saved, saved == fingerprint {
                            // Known cert, matches — proceed silently
                            completion(true)
                            return
                        }

                        // Unknown or changed cert — ask user
                        let subject = SecCertificateCopySubjectSummary(cert) as String? ?? "Unknown"
                        // Issuer not easily extractable from SecCertificate; use subject as fallback
                        let info = CertInfo(
                            host: host, port: port,
                            fingerprint: fingerprint,
                            subject: subject,
                            issuer: subject,
                            savedFingerprint: saved
                        )

                        Task { @MainActor in
                            let accepted = await onCertVerify?(info) ?? true
                            if accepted {
                                certStore?.saveFingerprint(host: host, port: port, fingerprint: fingerprint)
                            }
                            completion(accepted)
                        }
                    },
                    DispatchQueue.global()
                )
                return options
            }())
        } else {
            tlsParams = NWParameters.tcp
        }

        let endpoint = NWEndpoint.hostPort(
            host: NWEndpoint.Host(host),
            port: NWEndpoint.Port(rawValue: UInt16(port))!
        )
        let conn = NWConnection(to: endpoint, using: tlsParams)
        self.connection = conn

        // Wire telnet callbacks
        telnet.sendRaw = { [weak self] data in
            self?.connection?.send(content: data, completion: .contentProcessed { _ in })
        }

        telnet.onLine = { [weak self] line in
            Task { @MainActor in
                self?.onLine?(line)
            }
        }

        telnet.onPrompt = { [weak self] prompt in
            Task { @MainActor in
                self?.onLine?(prompt)
            }
        }

        conn.stateUpdateHandler = { [weak self] state in
            Task { @MainActor in
                guard let self else { return }
                switch state {
                case .ready:
                    self.connected = true
                    self.onConnect?()
                    self.startReading()
                    // Initial telnet negotiations
                    self.telnet.send(command: .will, option: TelnetOption.naws.rawValue)
                    self.telnet.send(command: .will, option: TelnetOption.ttype.rawValue)
                    self.telnet.send(command: .will, option: TelnetOption.charset.rawValue)
                    self.telnet.send(command: .do, option: TelnetOption.sga.rawValue)
                    self.telnet.send(command: .do, option: TelnetOption.echo.rawValue)
                case .failed, .cancelled:
                    self.connected = false
                    self.onDisconnect?()
                default:
                    break
                }
            }
        }

        conn.start(queue: .global())
    }

    private func startReading() {
        readTask = Task.detached { [weak self] in
            while let self, let conn = await self.connection {
                do {
                    let (data, _, isComplete, _) = try await withCheckedThrowingContinuation {
                        (continuation: CheckedContinuation<(Data?, NWConnection.ContentContext?, Bool, NWError?), Error>) in
                        conn.receive(minimumIncompleteLength: 1, maximumLength: 8192) { data, context, isComplete, error in
                            continuation.resume(returning: (data, context, isComplete, error))
                        }
                    }
                    if let data, !data.isEmpty {
                        await MainActor.run {
                            self.telnet.process(data)
                        }
                    }
                    if isComplete { break }
                } catch {
                    break
                }
            }
            await MainActor.run { [weak self] in
                self?.connected = false
                self?.onDisconnect?()
            }
        }
    }

    func disconnect() {
        connected = false
        readTask?.cancel()
        connection?.cancel()
        connection = nil
    }

    func sendLine(_ text: String) {
        guard connected else { return }
        telnet.sendLine(text)
    }
}

