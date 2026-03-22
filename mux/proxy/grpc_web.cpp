#ifdef GRPC_ENABLED

#include "grpc_web.h"
#include <algorithm>
#include <cstring>

// ---- Base64 ----

static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const uint8_t b64decode_table[256] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
     52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
    255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
     15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
    255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

std::string base64Encode(const std::string& data) {
    std::string out;
    const auto* p = reinterpret_cast<const uint8_t*>(data.data());
    size_t len = data.size();
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < len) {
        uint32_t v = (p[i] << 16) | (p[i+1] << 8) | p[i+2];
        out.push_back(b64chars[(v >> 18) & 0x3F]);
        out.push_back(b64chars[(v >> 12) & 0x3F]);
        out.push_back(b64chars[(v >>  6) & 0x3F]);
        out.push_back(b64chars[(v      ) & 0x3F]);
        i += 3;
    }
    if (i + 1 == len) {
        uint32_t v = p[i] << 16;
        out.push_back(b64chars[(v >> 18) & 0x3F]);
        out.push_back(b64chars[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == len) {
        uint32_t v = (p[i] << 16) | (p[i+1] << 8);
        out.push_back(b64chars[(v >> 18) & 0x3F]);
        out.push_back(b64chars[(v >> 12) & 0x3F]);
        out.push_back(b64chars[(v >>  6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string base64Decode(const std::string& data) {
    std::string out;
    out.reserve(data.size() * 3 / 4);

    uint32_t buf = 0;
    int bits = 0;
    for (char c : data) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        uint8_t val = b64decode_table[static_cast<uint8_t>(c)];
        if (val == 255) continue;
        buf = (buf << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

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
        size_t contentLen = std::stoul(clIt->second);
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

static const char* CORS_HDRS =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, X-Grpc-Web, Authorization, X-User-Agent\r\n"
    "Access-Control-Expose-Headers: Grpc-Status, Grpc-Message, Grpc-Status-Details-Bin\r\n"
    "Access-Control-Max-Age: 86400\r\n";

std::string corsPreflightResponse() {
    return std::string("HTTP/1.1 204 No Content\r\n")
         + CORS_HDRS
         + "Content-Length: 0\r\n\r\n";
}

std::string corsHeaders() {
    return CORS_HDRS;
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
