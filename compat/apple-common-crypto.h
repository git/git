/* suppress inclusion of conflicting openssl functions */
#define OPENSSL_NO_MD5
#define HEADER_HMAC_H
#define HEADER_SHA_H
#include <CommonCrypto/CommonHMAC.h>
#define HMAC_CTX CCHmacContext
#define HMAC_Init(hmac, key, len, algo) CCHmacInit(hmac, algo, key, len)
#define HMAC_Update CCHmacUpdate
#define HMAC_Final(hmac, hash, ptr) CCHmacFinal(hmac, hash)
#define HMAC_CTX_cleanup(ignore)
#define EVP_md5(...) kCCHmacAlgMD5
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
#define APPLE_LION_OR_NEWER
#include <Security/Security.h>
/* Apple's TYPE_BOOL conflicts with config.c */
#undef TYPE_BOOL
#endif

#ifdef APPLE_LION_OR_NEWER
#define git_CC_error_check(pattern, err) \
	do { \
		if (err) { \
			die(pattern, (long)CFErrorGetCode(err)); \
		} \
	} while(0)

#define EVP_EncodeBlock git_CC_EVP_EncodeBlock
static inline int git_CC_EVP_EncodeBlock(unsigned char *out,
		const unsigned char *in, int inlen)
{
	CFErrorRef err;
	SecTransformRef encoder;
	CFDataRef input, output;
	CFIndex length;

	encoder = SecEncodeTransformCreate(kSecBase64Encoding, &err);
	git_CC_error_check("SecEncodeTransformCreate failed: %ld", err);

	input = CFDataCreate(kCFAllocatorDefault, in, inlen);
	SecTransformSetAttribute(encoder, kSecTransformInputAttributeName,
			input, &err);
	git_CC_error_check("SecTransformSetAttribute failed: %ld", err);

	output = SecTransformExecute(encoder, &err);
	git_CC_error_check("SecTransformExecute failed: %ld", err);

	length = CFDataGetLength(output);
	CFDataGetBytes(output, CFRangeMake(0, length), out);

	CFRelease(output);
	CFRelease(input);
	CFRelease(encoder);

	return (int)strlen((const char *)out);
}

#define EVP_DecodeBlock git_CC_EVP_DecodeBlock
static int inline git_CC_EVP_DecodeBlock(unsigned char *out,
		const unsigned char *in, int inlen)
{
	CFErrorRef err;
	SecTransformRef decoder;
	CFDataRef input, output;
	CFIndex length;

	decoder = SecDecodeTransformCreate(kSecBase64Encoding, &err);
	git_CC_error_check("SecEncodeTransformCreate failed: %ld", err);

	input = CFDataCreate(kCFAllocatorDefault, in, inlen);
	SecTransformSetAttribute(decoder, kSecTransformInputAttributeName,
			input, &err);
	git_CC_error_check("SecTransformSetAttribute failed: %ld", err);

	output = SecTransformExecute(decoder, &err);
	git_CC_error_check("SecTransformExecute failed: %ld", err);

	length = CFDataGetLength(output);
	CFDataGetBytes(output, CFRangeMake(0, length), out);

	CFRelease(output);
	CFRelease(input);
	CFRelease(decoder);

	return (int)strlen((const char *)out);
}
#endif /* APPLE_LION_OR_NEWER */
