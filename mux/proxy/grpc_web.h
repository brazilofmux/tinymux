#ifndef HYDRA_GRPC_WEB_H
#define HYDRA_GRPC_WEB_H

#ifdef GRPC_ENABLED

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ---- grpc-web frame codec ----

// Decode a grpc-web request body: strip the 5-byte frame header,
// return the raw protobuf bytes.
// Handles both binary (grpc-web+proto) and text (grpc-web-text+proto).
bool grpcWebDecodeRequest(const std::string& body, bool isText,
                          std::string& protoOut);

// Encode a protobuf response as a grpc-web data frame (flag=0x00).
std::string grpcWebEncodeDataFrame(const std::string& proto);

// Encode a grpc-web trailer frame (flag=0x80) with status and message.
std::string grpcWebEncodeTrailerFrame(int grpcStatus,
                                       const std::string& message = "");

// Encode a complete unary response: data frame + trailer frame.
std::string grpcWebEncodeUnaryResponse(const std::string& proto,
                                        int grpcStatus,
                                        const std::string& message = "");

// ---- HTTP request parsing ----

struct HttpRequest {
    std::string method;     // GET, POST, OPTIONS
    std::string path;       // /hydra.HydraService/Authenticate
    std::map<std::string, std::string> headers;
    std::string body;
    bool complete{false};   // true when headers + body fully received
};

// Parse an HTTP/1.1 request. Returns true if complete (headers + body).
// Accumulates across multiple calls if partial.
bool parseHttpRequest(const std::string& raw, HttpRequest& req);

// ---- CORS ----

// Generate CORS headers for the given request Origin, checking against
// the allowed origins list.  If origins is empty, no Access-Control-Allow-Origin
// header is emitted (deny by default).
std::string corsHeaders(const std::string& requestOrigin,
                        const std::vector<std::string>& allowedOrigins);
std::string corsPreflightResponse(const std::string& requestOrigin,
                                   const std::vector<std::string>& allowedOrigins);

// ---- grpc-web content type detection ----

bool isGrpcWebContentType(const std::string& contentType);
bool isGrpcWebTextContentType(const std::string& contentType);

#endif // GRPC_ENABLED
#endif // HYDRA_GRPC_WEB_H
