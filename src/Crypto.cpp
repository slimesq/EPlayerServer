#include "Crypto.h"
#include <openssl/evp.h>
#include <stdexcept>

Buffer Crypto::MD5(Buffer const& text)
{
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned length = 0;
    if (EVP_Digest(text.data(), text.size(), digest, &length, EVP_md5(), nullptr) != 1)
        throw std::runtime_error("MD5 calculation failed");
    Buffer result;
    char byte[3]{};
    for (unsigned i = 0; i < length; ++i)
    {
        snprintf(byte, sizeof(byte), "%02x", unsigned(digest[i]));
        result += byte;
    }
    return result;
}
