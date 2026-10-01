#include "../../src/ShiftJISConverter.h"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
  unsigned char header[4];
  while (std::cin.read(reinterpret_cast<char *>(header), sizeof(header))) {
    const uint32_t length = (uint32_t(header[0]) << 24) |
                            (uint32_t(header[1]) << 16) |
                            (uint32_t(header[2]) << 8) | header[3];
    std::vector<unsigned char> bytes(length);
    if (!std::cin.read(reinterpret_cast<char *>(bytes.data()), length)) return 1;
    std::string decoded;
    bms_parser::ShiftJISConverter::BytesToUTF8(bytes.data(), bytes.size(), decoded);
    const auto size = static_cast<uint32_t>(decoded.size());
    for (int i = 0; i < 4; ++i) header[i] = (size >> (24 - i * 8)) & 0xff;
    std::cout.write(reinterpret_cast<char *>(header), sizeof(header));
    std::cout.write(decoded.data(), decoded.size());
  }
  return std::cin.eof() && std::cin.gcount() == 0 ? 0 : 1;
}
