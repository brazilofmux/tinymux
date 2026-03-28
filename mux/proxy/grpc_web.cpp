#ifdef GRPC_ENABLED

#include "grpc_web.h"
#include "base64.h"
#include <algorithm>
#include <cstring>

// ---- grpc-web frame codec ----

// grpc-web frame: flag(1) + length_be32(4) + payload(length)
// flag 0x00 = data, 0x80 = trailer

bool grpcWebDecodeRequest(const std::string& body, bool isText,
                          std::string& protoOut) {
    std::string decoded = isText ? base64Decode(body) : body;

    if (decoded.size() < 5) {
        // Empty request (e.g. ListGames with no fields) is valid
        protoOut.clear();
        return true;
    }

    // uint8_t flag = static_cast<uint8_t>(decoded[0]);
    uint32_t len = (static_cast<uint8_t>(decoded[1]) << 24)
                 | (static_cast<uint8_t>(decoded[2]) << 16)
                 | (static_cast<uint8_t>(decoded[3]) << 8)
                 | (static_cast<uint8_t>(decoded[4]));

    if (decoded.size() < 5 + len) {
        protoOut.clear();
        return false;  // incomplete
    }

    protoOut = decoded.substr(5, len);
    return true;
}

std::string grpcWebEncodeDataFrame(const std::string& proto) {
    std::string frame;
    frame.reserve(5 + proto.size());
    frame.push_back('\x00');  // flag: data
    uint32_t len = static_cast<uint32_t>(proto.size());
    frame.push_back(static_cast<char>((len >> 24) & 0xFF));
    frame.push_back(static_cast<char>((len >> 16) & 0xFF));
    frame.push_back(static_cast<char>((len >>  8) & 0xFF));
    frame.push_back(static_cast<char>((len      ) & 0xFF));
    frame.append(proto);
    return frame;
}

std::string grpcWebEncodeTrailerFrame(int grpcStatus,
                                       const std::string& message) {
    // Trailers encoded as HTTP header text
    std::string trailers = "grpc-status:" + std::to_string(grpcStatus) + "\r\n";
    if (!message.empty()) {
        trailers += "grpc-message:" + message + "\r\n";
    }

    std::string frame;
    frame.reserve(5 + trailers.size());
    frame.push_back('\x80');  // flag: trailer
    uint32_t len = static_cast<uint32_t>(trailers.size());
    frame.push_back(static_cast<char>((len >> 24) & 0xFF));
    frame.push_back(static_cast<char>((len >> 16) & 0xFF));
    frame.push_back(static_cast<char>((len >>  8) & 0xFF));
    frame.push_back(static_cast<char>((len      ) & 0xFF));
    frame.append(trailers);
    return frame;
}

std::string grpcWebEncodeUnaryResponse(const std::string& proto,
                                        int grpcStatus,
                                        const std::string& message) {
    return grpcWebEncodeDataFrame(proto)
         + grpcWebEncodeTrailerFrame(grpcStatus, message);
}

// ---- HTTP parsing ----

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool parseHttpRequest(const std::string& raw, HttpRequest& req) {
    // Find end of headers
    auto hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) {
        req.complete = false;
        return false;
    }

    // Parse request line
    auto firstLine = raw.substr(0, raw.find("\r\n"));
    auto sp1 = firstLine.find(' ');
    auto sp2 = firstLine.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        req.complete = false;
        return false;
    }
    req.method = firstLine.substr(0, sp1);
    req.path = firstLine.substr(sp1 + 1, sp2 - sp1 - 1);

    // Parse headers
    req.headers.clear();
    size_t pos = raw.find("\r\n") + 2;
    while (pos < hdrEnd) {
        auto lineEnd = raw.find("\r\n", pos);
        if (lineEnd == std::string::npos || lineEnd > hdrEnd) break;
        auto colon = raw.find(':', pos);
        if (colon != std::string::npos && colon < lineEnd) {
            std::string name = toLower(raw.substr(pos, colon - pos));
            std::string value = raw.substr(colon + 1, lineEnd - colon - 1);
            // trim leading whitespace
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
                value.erase(0, 1);
            req.headers[name] = value;
        }
        pos = lineEnd + 2;
    }

    // Check content-length for body
    size_t bodyStart = hdrEnd + 4;
    auto clIt = req.headers.find("content-length");
    if (clIt != req.headers.end()) {
        size_t contentLen = 0;
        try { contentLen = std::stoul(clIt->second); }
        catch (...) {
            req.complete = false;
            return false;
        }
        if (raw.size() < bodyStart + contentLen) {
            req.complete = false;
            return false;  // body not fully received
        }
        req.body = raw.substr(bodyStart, contentLen);
    } else {
        req.body = raw.substr(bodyStart);
    }

    req.complete = true;
    return true;
}

// ---- CORS ----

// Check if requestOrigin is in the allowed list.  Returns the matching
// origin string, or empty if not allowed.
static std::string matchOrigin(const std::string& requestOrigin,
                               const std::vector<std::string>& allowed) {
    if (requestOrigin.empty()) return "";
    for (const auto& o : allowed) {
        if (o == "*" || o == requestOrigin) return requestOrigin;
    }
    return "";
}

std::string corsHeaders(const std::string& requestOrigin,
                        const std::vector<std::string>& allowedOrigins) {
    std::string origin = matchOrigin(requestOrigin, allowedOrigins);
    if (origin.empty()) {
        // No CORS headers — browser will reject the response
        return "";
    }
    return "Access-Control-Allow-Origin: " + origin + "\r\n"
           "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
           "Access-Control-Allow-Headers: Content-Type, X-Grpc-Web, Authorization, X-User-Agent\r\n"
           "Access-Control-Expose-Headers: Grpc-Status, Grpc-Message, Grpc-Status-Details-Bin\r\n"
           "Access-Control-Max-Age: 86400\r\n"
           "Vary: Origin\r\n";
}

std::string corsPreflightResponse(const std::string& requestOrigin,
                                   const std::vector<std::string>& allowedOrigins) {
    return std::string("HTTP/1.1 204 No Content\r\n")
         + corsHeaders(requestOrigin, allowedOrigins)
         + "Content-Length: 0\r\n\r\n";
}

// ---- Content type detection ----

bool isGrpcWebContentType(const std::string& ct) {
    return ct.find("application/grpc-web") != std::string::npos
        && ct.find("text") == std::string::npos;
}

bool isGrpcWebTextContentType(const std::string& ct) {
    return ct.find("application/grpc-web-text") != std::string::npos;
}

#endif // GRPC_ENABLED
