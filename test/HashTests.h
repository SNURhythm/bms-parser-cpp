#pragma once

// Generated with Python hashlib from the deterministic binary pattern below.
inline int runHashBoundaryTests() {
  struct Vector { size_t size; const char *md5; const char *sha256; };
  const Vector vectors[] = {
    {0, "d41d8cd98f00b204e9800998ecf8427e", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {1, "47ed733b8d10be225eceba344d533586", "4a64a107f0cb32536e5bce6c98c393db21cca7f4ea187ba8c4dca8b51d4ea80a"},
    {2, "e91fa551f34014c7490f9222d8772b3e", "525f7c3d4fdcb81a330a1622d03010924a226dade36d159f9f1ad2725e5fc17d"},
    {3, "755975aff1ed9b90c75ca30267c991ae", "2205790923f0b1097839d7f9802f21d284e1a7cdbed8b3334d9464a5c0eb1c26"},
    {7, "791e4a2066272326cd35a63bce1b72fe", "3a81c2e096ac2af932290f618bb8a855f58b8537abaedfebc74f294195affb02"},
    {55, "83937061fa00943933e257f763522a17", "2b502e7ca389a4ac7fb4a76632870344436b40478644571a8d41490bc996b265"},
    {56, "620ca9be98f8b25a00882b68673870e2", "2a90b8bea48975d3d9e9a4ec2bf54b8cfbf37bc880ee8701fe2cb0d1d85cde9b"},
    {57, "90f796973771c579f8c4abb41529cbc3", "4fdd038449884701c428b73dfc31cdcca2a95568ae0a4fa8e9e5d9c2a5ef7d26"},
    {63, "290202371627f7c3b8351cf228c62d87", "7ff8db66a46c78c93c4e3ebba13066b8bbc9627c38c3799d0a5f6058afa48ef9"},
    {64, "f39ac9cca3b1b6bf68b9c54063beecae", "1e38723be87184bb3846e46ae5f5a626c3059d5c19d4027a1f693b34c23d54ea"},
    {65, "ad4a887b2bc8cab6bad5e9359ae3da61", "043a2509f538be86e8e22c4d30a57762b2b8a6d01a8a7c63238071806adae718"},
    {119, "685328b819769f7f0d8b5fe64c60250f", "6acca153c3c08720197767bb5230b6f409793b3ff2f7c745ecd82a92ecd2dc7f"},
    {120, "f9156e337f848ec8a6e5940df0cf36d4", "3cf9f2a3a825eb58f09af93bc16091096f30565001cf7265950618ea46c8d330"},
    {127, "5adead85c9ca9be433aeb56c1934a6cb", "80e9d3c1a057b71f39d9a97bf38d7000a16b245c574016f284e468513791ec4c"},
    {128, "e4ed71ab3f406706524470b987db2df0", "eb9b1c22c5175f99519dc747dea419c79e02376465ace331a1fee95b9239b7f0"},
    {129, "2e3081d23e2860e59b1addc85fa5b74a", "7e3b1eda9fac6f287e17c91e129ff6b9bed1109bf9cf082ec5d691f768818efb"},
    {255, "752c0d7ec3f2299677801aaf53e36181", "bf5469cb3297f096f013d83e24dc01775d713de6859212662561010f28569d69"},
    {256, "dff09a05f65931b3348ca8bc08cec7be", "724c0f27c36e11effd5ed433b94826c1917e4a8eb8151d301ec800f365c5b575"},
    {257, "8cf2f24bc5c48fcf438d28fcca3f8559", "dda058f13defb928abcdcb5a692316cd9c3e542403c0c8ef9e3bf4e2f7fbf3c2"},
    {1024, "a784ff7b9988fe2f0dcf83884ab5ee61", "bfc5ce3ab28c4d4cdbc2e9747d8b4c5ddf5256ddd3ab8857a1e209964e607b2e"},
    {65537, "112017bbacf12ded9568115e4314f845", "41c1db7309a667b8a07dafc92c26474567c113ad9ca5611882dddfacdf36c905"},
    {1000000, "de2a51c4be8e0aae909172f828a071c5", "83e577f46cb2f98c83862ed6715e3fca72972a45dc0c2412749582594d532c22"},
  };
  for (const auto &test : vectors) {
    std::vector<unsigned char> bytes(test.size);
    for (size_t i = 0; i < bytes.size(); ++i)
      bytes[i] = static_cast<unsigned char>((i * 37 + (i >> 3) * 11 + 17) & 255);
    ASSERT_EQ(std::string(test.sha256), bms_parser::sha256(bytes), "binary SHA-256");
    for (const size_t chunk : {size_t{1}, size_t{7}, size_t{55}, size_t{64}, size_t{129}, size_t{4096}}) {
      bms_parser::SHA256 sha;
      sha.init();
      bms_parser::MD5 md5;
      for (size_t offset = 0; offset < bytes.size(); offset += chunk) {
        const auto length = static_cast<unsigned int>(std::min(chunk, bytes.size() - offset));
        sha.update(bytes.data() + offset, length);
        md5.update(bytes.data() + offset, length);
      }
      unsigned char digest[32];
      sha.final(digest);
      std::string hex;
      constexpr char digits[] = "0123456789abcdef";
      for (const auto byte : digest) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
      md5.finalize();
      ASSERT_EQ(std::string(test.sha256), hex, "chunked SHA-256");
      ASSERT_EQ(std::string(test.md5), md5.hexdigest(), "chunked MD5");
    }
    if (test.size == 129 || test.size == 257 || test.size == 65537) {
      // SHA update accepts byte-aligned input. Exact-sized buffers also let
      // sanitizers catch vector loads that read beyond the final full block.
      for (const size_t offset : {size_t{1}, size_t{3}, size_t{15}}) {
        const auto storage = std::make_unique<unsigned char[]>(offset + bytes.size());
        std::copy(bytes.begin(), bytes.end(), storage.get() + offset);
        bms_parser::SHA256 sha;
        sha.init();
        sha.update(storage.get() + offset, static_cast<unsigned int>(bytes.size()));
        unsigned char digest[32];
        sha.final(digest);
        std::string hex;
        constexpr char digits[] = "0123456789abcdef";
        for (const auto byte : digest) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
        ASSERT_EQ(std::string(test.sha256), hex, "unaligned SHA-256");
      }
    }
  }
  return 0;
}

// Exercise the parser's selected hash backend, including original NUL/high bytes
// in an ignored trailing line. Expected digests were generated with hashlib.
inline int runParserHashBoundaryTests() {
  struct Vector { size_t size; const char *md5; const char *sha256; };
  const Vector vectors[] = {
    {55, "aa0bddab8a5f1667e4c9c28ec063b63e", "9a670e4bbe23167a7e3da974453d0aad7e8d469bbd63d8c04f703ce940a96b4a"},
    {56, "36290d513f2190eb6abf5ba2fa9e3467", "c5a9ac9a03501c1dda85d9b3db869ead90e315b8109992abe0e6db0541086f85"},
    {57, "1eed32e06e42cb0066bf5e1b1d992318", "33d96e77b0d6c8104ccdf7b3d944eb3e20dbbb39f32a7e8930e435466e2672b7"},
    {63, "2ca02cbd3c8c6320588e1bf6fc9806ba", "ae3fdb204a76eb0ee4db4a0a756f345702534160893cdd1be05c97b1679b3f46"},
    {64, "e7e578d73b184c3f86e2e689fb332414", "9f732ae724fb4d315bf9a962ed82f117d3d0e03cbdd14ed7f47bebaea35ad8c6"},
    {65, "59f93494d2d8fa6c5334f5a1494ef7e9", "a14d62c43ebd73c86bb3448a800459bbffde88fc4ca676a8f4e5d82b65451307"},
    {119, "b2e79527816fa1829e5e58067348eab4", "142f530768c62062be5ec873c039313e39bd2f4fed239a50dc2426d024cc73ab"},
    {120, "33e75664d1c75638821b92e38e6a4c68", "e6cfd80139667e2512ac46f27001fec6b3ca7f1e46b930310e1d852b5adcf7ca"},
    {127, "5ad2ae8875965f7b4525e6267425d11e", "781423db0473250042d65567c1556df792912426e14a9ee70abfab8de543e6f7"},
    {128, "674f3894b8983654952a2e1617bc3152", "fa12a1ab16256b23064ec81eafebce9997eda95ec304512683ed7b4e952e5bd1"},
    {129, "c3b1661b076c9d7eb8973a012843ac91", "0d30d5c853239a360f545c47058fbcd112895a08e6f1bdb9b41c42d22ade6ab9"},
    {255, "82d8c078a43986813c3145f6636e8053", "ac02f473baae7a45d9e4c2706768b3571179a299fdeff359380457714ff6cf2f"},
    {256, "4f5dd3be69e5fffdf6d182c11ca18082", "86790c92aac46fce7a2af65071292705ddeb1d25f1b9d118c95ffb40b7cbe1ec"},
    {257, "4b6ed6ed2a07ff96405b09625f87b23c", "a2aae0ce8d80aa51f21c28b09efafc54aaabe894dfabae7b62bbd5a4f0208e76"},
    {1024, "ea68f70bf1d888051d8cce02419fdd39", "f3cb55b8f9b10b3a1328cd95c4719ae197a2ae0c438bf9a14e39b321fc06ba49"},
    {65537, "fbe7c4f16aefc7cabc138cbad1a36a6c", "d877f14067db1ad854f7316fd6515228420d49fb1e731eaefede5dc90861e266"},
  };
  for (const auto &test : vectors) {
    const std::string prefix = "#BPM 120\n#00111:01\n*";
    std::vector<unsigned char> bytes(prefix.begin(), prefix.end());
    for (size_t i = bytes.size(); i < test.size; ++i) {
      const auto byte = static_cast<unsigned char>((i * 37 + (i >> 3) * 11 + 17) & 255);
      bytes.push_back(byte == '\n' || byte == '\r' ? 0 : byte);
    }
    std::atomic_bool cancelled{false};
    for (const bool metadataOnly : {false, true}) {
      bms_parser::Parser parser;
      bms_parser::Chart *raw = nullptr;
      parser.Parse(bytes, &raw, false, metadataOnly, cancelled);
      const std::unique_ptr<bms_parser::Chart> chart(raw);
      ASSERT_EQ(true, chart != nullptr, "binary hash fixture parses");
      ASSERT_EQ(std::string(test.md5), chart->Meta.MD5, "parser original-byte MD5");
      ASSERT_EQ(std::string(test.sha256), chart->Meta.SHA256, "parser original-byte SHA-256");
    }
    bms_parser::Parser parser;
    const auto scan = parser.Scan(bytes, cancelled);
    ASSERT_EQ(true, scan.has_value(), "binary hash fixture scans");
    ASSERT_EQ(std::string(test.md5), scan->Meta.MD5, "scan original-byte MD5");
    ASSERT_EQ(std::string(test.sha256), scan->Meta.SHA256, "scan original-byte SHA-256");
  }
  return 0;
}
