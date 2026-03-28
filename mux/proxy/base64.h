#ifndef HYDRA_BASE64_H
#define HYDRA_BASE64_H

#include <cstddef>
#include <cstdint>
#include <string>

// Base64 encode raw bytes.
std::string base64Encode(const uint8_t* data, size_t len);

// Base64 encode a string.
std::string base64Encode(const std::string& data);

// Base64 decode a string.
std::string base64Decode(const std::string& data);

#endif // HYDRA_BASE64_H
