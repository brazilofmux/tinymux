import SwiftUI

// MARK: - Certificate Verification Dialog

struct CertVerifySheet: View {
    let certInfo: CertInfo
    var onAccept: () -> Void
    var onReject: () -> Void

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    if certInfo.savedFingerprint != nil {
                        warningBanner
                    } else {
                        firstConnectionBanner
                    }

                    Group {
                        labeledField("Host", "\(certInfo.host):\(certInfo.port)")
                        labeledField("Subject", certInfo.subject)
                        labeledField("Issuer", certInfo.issuer)

                        Text("SHA-256 Fingerprint")
                            .font(.caption).fontWeight(.bold)
                        Text(certInfo.fingerprint)
                            .font(.system(size: 10, design: .monospaced))
                            .textSelection(.enabled)

                        if let saved = certInfo.savedFingerprint {
                            Text("Previous Fingerprint")
                                .font(.caption).fontWeight(.bold)
                            Text(saved)
                                .font(.system(size: 10, design: .monospaced))
                                .textSelection(.enabled)
                        }
                    }
                    .padding(.horizontal)
                }
                .padding(.vertical)
            }
            .navigationTitle(certInfo.savedFingerprint != nil ? "Certificate Changed!" : "Unknown Certificate")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Reject") { onReject() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(certInfo.savedFingerprint != nil ? "Accept Anyway" : "Trust") {
                        onAccept()
                    }
                }
            }
        }
        .presentationDetents([.medium, .large])
    }

    private var warningBanner: some View {
        HStack {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundColor(.red)
            Text("The certificate for this server has changed since your last connection. This could indicate a man-in-the-middle attack.")
                .font(.callout)
        }
        .padding()
        .background(Color.red.opacity(0.15))
        .cornerRadius(8)
        .padding(.horizontal)
    }

    private var firstConnectionBanner: some View {
        HStack {
            Image(systemName: "lock.shield")
                .foregroundColor(.blue)
            Text("First SSL/TLS connection to this server. Review the certificate before trusting.")
                .font(.callout)
        }
        .padding()
        .background(Color.blue.opacity(0.15))
        .cornerRadius(8)
        .padding(.horizontal)
    }

    private func labeledField(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.caption).foregroundColor(.secondary)
            Text(value).font(.body)
        }
    }
}
