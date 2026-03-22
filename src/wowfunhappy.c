/*
 * Additional polyfills, centralized here to keep separate from
 * the upstream macports-legacy-support codebase.
 *
 * Follows our preferred coding style (e.g. tabs instead of spaces),
 * not the upstream style.
 * 
 * Note: For now, there are few enough functions here to keep them in one file.
 * If we add many more functions in the future, we should make a Wowfunhappy
 * directory and split into seperate files.
 */

/* MP support header */
#include "MacportsLegacySupport.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

// For debugging. Use with `ASL_LOG("Hello world.");`
#define ASL_LOG(format, ...) asl_log(NULL, NULL, ASL_LEVEL_ERR, format, ##__VA_ARGS__)




/*
 * @available() support — __isOSVersionAtLeast / __isPlatformVersionAtLeast
 * These compiler-rt builtins don't exist on 10.9. Without them,
 * @available(macOS 10.15, *) calls would crash. We implement them
 * to report the actual OS version so code correctly skips
 * paths for newer APIs.
 */

#include <stdio.h>
#include <sys/sysctl.h>

static int cached_major = 0, cached_minor = 0, cached_patch = 0;

static void ensure_os_version(void) {
	if (cached_major) return;
	char str[64];
	size_t len = sizeof(str);
	if (sysctlbyname("kern.osproductversion", str, &len, NULL, 0) == 0) {
		sscanf(str, "%d.%d.%d", &cached_major, &cached_minor, &cached_patch);
	}
	if (!cached_major) {
		/* Fallback: we know we're on 10.9 */
		cached_major = 10; cached_minor = 9; cached_patch = 5;
	}
}

int32_t __isOSVersionAtLeast(int32_t major, int32_t minor, int32_t patch) {
	ensure_os_version();
	if (cached_major != major) return cached_major > major;
	if (cached_minor != minor) return cached_minor > minor;
	return cached_patch >= patch;
}

/* Newer Clang uses this variant (platform 1 = macOS) */
int32_t __isPlatformVersionAtLeast(uint32_t platform, uint32_t major,
                                    uint32_t minor, uint32_t patch) {
	(void)platform; /* Assume macOS */
	return __isOSVersionAtLeast((int32_t)major, (int32_t)minor, (int32_t)patch);
}


/*
 * mmap wrapper — strip MAP_JIT flag (0x0800, added 10.14)
 * Some runtimes (.NET, etc.) use MAP_JIT for JIT code pages.
 * On 10.9 this flag doesn't exist and causes mmap to fail.
 */

#include <sys/mman.h>

#ifndef MAP_JIT
#define MAP_JIT 0x0800
#endif

extern void *__mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
	flags &= ~MAP_JIT;
	return __mmap(addr, len, prot, flags, fd, offset);
}

/*
 * pthread_jit_write_protect_np (added macOS 11.0)
 * On x86_64, this is a no-op.
 */
void pthread_jit_write_protect_np(int enabled) {
	(void)enabled;
}


/*
 * ObjC runtime functions added in 10.14+ / 11.0+
 */

#include <objc/objc.h>
#include <objc/message.h>
#include <objc/runtime.h>

/* objc_alloc_init (added ~10.14.4) — combines [cls alloc] and [obj init] */
id objc_alloc_init(Class cls) {
	id obj = ((id(*)(Class, SEL))objc_msgSend)(cls, sel_getUid("alloc"));
	return ((id(*)(id, SEL))objc_msgSend)(obj, sel_getUid("init"));
}

/* objc_alloc (added ~10.14) — optimized [cls alloc] */
id objc_alloc(Class cls) {
	return ((id(*)(Class, SEL))objc_msgSend)(cls, sel_getUid("alloc"));
}

/* objc_opt_class (added macOS 11) — optimized [obj class] */
Class objc_opt_class(id obj) {
	if (!obj) return Nil;
	return ((Class(*)(id, SEL))objc_msgSend)(obj, sel_getUid("class"));
}

/* objc_opt_isKindOfClass (added macOS 11) */
BOOL objc_opt_isKindOfClass(id obj, Class cls) {
	if (!obj) return NO;
	return ((BOOL(*)(id, SEL, Class))objc_msgSend)(obj, sel_getUid("isKindOfClass:"), cls);
}

/* objc_opt_respondsToSelector (added macOS 11) */
BOOL objc_opt_respondsToSelector(id obj, SEL sel) {
	if (!obj) return NO;
	return ((BOOL(*)(id, SEL, SEL))objc_msgSend)(obj, sel_getUid("respondsToSelector:"), sel);
}

/* objc_unsafeClaimAutoreleasedReturnValue (added 10.11) */
extern id objc_retainAutoreleasedReturnValue(id obj);
id objc_unsafeClaimAutoreleasedReturnValue(id obj) {
	return objc_retainAutoreleasedReturnValue(obj);
}


/*
 * ___chkstk_darwin — stack probe function used by LLVM/Clang.
 * RAX contains the number of bytes to probe. This function
 * must touch each page to trigger guard page faults.
 */
__asm__(
    ".globl ____chkstk_darwin\n"
    "____chkstk_darwin:\n"
    "  pushq  %rcx\n"
    "  pushq  %rax\n"
    "  cmpq   $0x1000, %rax\n"
    "  leaq   24(%rsp), %rcx\n"   /* rcx = original rsp */
    "  jb     .Ldone\n"
    ".Lloop:\n"
    "  subq   $0x1000, %rcx\n"
    "  testq  %rcx, (%rcx)\n"     /* probe the page */
    "  subq   $0x1000, %rax\n"
    "  cmpq   $0x1000, %rax\n"
    "  ja     .Lloop\n"
    ".Ldone:\n"
    "  subq   %rax, %rcx\n"
    "  testq  %rcx, (%rcx)\n"     /* probe last partial page */
    "  popq   %rax\n"
    "  popq   %rcx\n"
    "  retq\n"
);


/*
 * thread_get_register_pointer_values (added ~10.11)
 * Used by .NET GC to scan thread registers for managed pointers.
 */

#include <mach/mach.h>
#include <mach/thread_act.h>

kern_return_t thread_get_register_pointer_values(
    thread_t thread, uintptr_t *sp, size_t *count,
    uintptr_t *register_values)
{
	x86_thread_state64_t state;
	mach_msg_type_number_t state_count = x86_THREAD_STATE64_COUNT;
	kern_return_t kr = thread_get_state(thread, x86_THREAD_STATE64,
	                                    (thread_state_t)&state, &state_count);
	if (kr != KERN_SUCCESS) return kr;

	if (sp) *sp = state.__rsp;

	if (register_values && count) {
		size_t i = 0;
		register_values[i++] = state.__rax;
		register_values[i++] = state.__rbx;
		register_values[i++] = state.__rcx;
		register_values[i++] = state.__rdx;
		register_values[i++] = state.__rdi;
		register_values[i++] = state.__rsi;
		register_values[i++] = state.__rbp;
		register_values[i++] = state.__r8;
		register_values[i++] = state.__r9;
		register_values[i++] = state.__r10;
		register_values[i++] = state.__r11;
		register_values[i++] = state.__r12;
		register_values[i++] = state.__r13;
		register_values[i++] = state.__r14;
		register_values[i++] = state.__r15;
		register_values[i++] = state.__rip;
		*count = i;
	}
	return KERN_SUCCESS;
}


/*
 * syslog$DARWIN_EXTSN — newer ABI variant
 */

#include <syslog.h>
#include <stdarg.h>

void syslog_darwin_extsn(int priority, const char *message, ...)
    __asm("_syslog$DARWIN_EXTSN");
void syslog_darwin_extsn(int priority, const char *message, ...) {
	va_list ap;
	va_start(ap, message);
	vsyslog(priority, message, ap);
	va_end(ap);
}


/*
 * Security framework
 */

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <dlfcn.h>

/*
 * SecPolicyCreateRevocation override — return NULL so runtimes skip
 * adding revocation policies to the trust object.
 */
SecPolicyRef SecPolicyCreateRevocation(CFOptionFlags flags) {
	(void)flags;
	return NULL;
}

/*
 * SecTrustEvaluate override — replace trust policies with a
 * simple basic X.509 policy before calling the real function.
 * This avoids the crash in 10.9's compareRevocationPolicies
 * while still performing real trust evaluation.
 */
OSStatus SecTrustEvaluate(SecTrustRef trust, SecTrustResultType *result) {
	static OSStatus (*real_eval)(SecTrustRef, SecTrustResultType *) = NULL;
	static OSStatus (*real_set_policies)(SecTrustRef, CFTypeRef) = NULL;
	if (!real_eval) {
		void *sec = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_NOLOAD);
		if (sec) {
			real_eval = dlsym(sec, "SecTrustEvaluate");
			real_set_policies = dlsym(sec, "SecTrustSetPolicies");
		}
	}
	if (!real_eval) {
		if (result) *result = kSecTrustResultProceed;
		return errSecSuccess;
	}

	/* Replace policies with just basic X.509 — no revocation */
	SecPolicyRef basic = SecPolicyCreateBasicX509();
	if (basic && real_set_policies) {
		real_set_policies(trust, basic);
		CFRelease(basic);
	}

	return real_eval(trust, result);
}

/*
 * SecTrustEvaluateWithError (added 10.14)
 * https://trac.macports.org/ticket/66749#comment:2
 */

static CFStringRef getStringForResultType(SecTrustResultType resultType) {
	switch (resultType) {
		case kSecTrustResultInvalid: return CFSTR("Error evaluating certificate");
		case kSecTrustResultDeny: return CFSTR("User specified to deny trust");
		case kSecTrustResultUnspecified: return CFSTR("Rejected Certificate");
		case kSecTrustResultRecoverableTrustFailure : return CFSTR("Rejected Certificate");
		case kSecTrustResultFatalTrustFailure :return CFSTR("Bad Certificate");
		case kSecTrustResultOtherError: return CFSTR("Error evaluating certificate");
		case kSecTrustResultProceed: return CFSTR("Proceed");
		default: return CFSTR("Unknown");
	}
}

bool SecTrustEvaluateWithError(SecTrustRef trust, CFErrorRef  *error) {
	SecTrustResultType trustResult = kSecTrustResultInvalid;
	OSStatus status = SecTrustEvaluate(trust, &trustResult);
	if (
		status == errSecSuccess &&
		(trustResult == kSecTrustResultProceed || trustResult == kSecTrustResultUnspecified)
	) {
		if (error) {
			*error = NULL;
		}
		return true;
	}
	if (error) {
		*error = CFErrorCreate(kCFAllocatorDefault, getStringForResultType(trustResult), 0, NULL);
	}
	return false;
}

/* SecCertificateCopyKey (added 10.14) */
SecKeyRef SecCertificateCopyKey(SecCertificateRef certificate) {
	(void)certificate;
	return NULL;
}

/*
 * SecKey functions (added 10.12+) — stubs
 */

CFDataRef SecKeyCopyExternalRepresentation(SecKeyRef key, CFErrorRef *error) {
	(void)key;
	if (error) *error = NULL;
	return NULL;
}

SecKeyRef SecKeyCreateWithData(CFDataRef keyData, CFDictionaryRef attributes, CFErrorRef *error) {
	(void)keyData; (void)attributes;
	if (error) *error = NULL;
	return NULL;
}

SecKeyRef SecKeyCreateRandomKey(CFDictionaryRef parameters, CFErrorRef *error) {
	(void)parameters;
	if (error) *error = NULL;
	return NULL;
}

CFDataRef SecKeyCreateSignature(SecKeyRef key, SecPadding algorithm,
                                CFDataRef dataToSign, CFErrorRef *error) {
	(void)key; (void)algorithm; (void)dataToSign;
	if (error) *error = NULL;
	return NULL;
}

Boolean SecKeyVerifySignature(SecKeyRef key, SecPadding algorithm,
                              CFDataRef signedData, CFDataRef signature,
                              CFErrorRef *error) {
	(void)key; (void)algorithm; (void)signedData; (void)signature;
	if (error) *error = NULL;
	return false;
}

CFDataRef SecKeyCreateEncryptedData(SecKeyRef key, SecPadding algorithm,
                                    CFDataRef plaintext, CFErrorRef *error) {
	(void)key; (void)algorithm; (void)plaintext;
	if (error) *error = NULL;
	return NULL;
}

CFDataRef SecKeyCreateDecryptedData(SecKeyRef key, SecPadding algorithm,
                                    CFDataRef ciphertext, CFErrorRef *error) {
	(void)key; (void)algorithm; (void)ciphertext;
	if (error) *error = NULL;
	return NULL;
}

SecKeyRef SecKeyCopyPublicKey(SecKeyRef key) {
	(void)key;
	return NULL;
}

CFDictionaryRef SecKeyCopyAttributes(SecKeyRef key) {
	(void)key;
	return NULL;
}

CFDataRef SecKeyCopyKeyExchangeResult(SecKeyRef publicKey, void *algorithm,
                                      SecKeyRef parameters, CFDictionaryRef requestedSize,
                                      CFErrorRef *error) {
	(void)publicKey; (void)algorithm; (void)parameters; (void)requestedSize;
	if (error) *error = NULL;
	return NULL;
}

/*
 * SSL/TLS ALPN functions (added 10.13.4) — stubs
 */

#define STUB_UNIMPLEMENTED (-4)

OSStatus SSLCopyALPNProtocols(void *context, CFArrayRef *protocols) {
	(void)context;
	if (protocols) *protocols = NULL;
	return STUB_UNIMPLEMENTED;
}

OSStatus SSLSetALPNProtocols(void *context, CFArrayRef protocols) {
	(void)context; (void)protocols;
	return STUB_UNIMPLEMENTED;
}

/*
 * Security framework constants (added 10.12+)
 */

const CFStringRef kSecAttrKeyTypeECSECPrimeRandom = CFSTR("73");
const CFStringRef kSecUseDataProtectionKeychain = CFSTR("u-DataProtectionKeychain");

/* SecKeyAlgorithm constants (added 10.12) */
const CFStringRef kSecKeyAlgorithmECDHKeyExchangeStandard = CFSTR("algid:ecdh:standard");
const CFStringRef kSecKeyAlgorithmECDSASignatureDigestX962 = CFSTR("algid:ecdsa:digest-x962");
const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA1 = CFSTR("algid:encrypt:RSA:OAEP-SHA1");
const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA256 = CFSTR("algid:encrypt:RSA:OAEP-SHA256");
const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA384 = CFSTR("algid:encrypt:RSA:OAEP-SHA384");
const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA512 = CFSTR("algid:encrypt:RSA:OAEP-SHA512");
const CFStringRef kSecKeyAlgorithmRSAEncryptionPKCS1 = CFSTR("algid:encrypt:RSA:PKCS1");
const CFStringRef kSecKeyAlgorithmRSAEncryptionRaw = CFSTR("algid:encrypt:RSA:raw");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1 = CFSTR("algid:sign:RSA:digest-PKCS1v15:SHA1");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256 = CFSTR("algid:sign:RSA:digest-PKCS1v15:SHA256");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384 = CFSTR("algid:sign:RSA:digest-PKCS1v15:SHA384");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512 = CFSTR("algid:sign:RSA:digest-PKCS1v15:SHA512");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA1 = CFSTR("algid:sign:RSA:digest-PSS:SHA1");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA256 = CFSTR("algid:sign:RSA:digest-PSS:SHA256");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA384 = CFSTR("algid:sign:RSA:digest-PSS:SHA384");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA512 = CFSTR("algid:sign:RSA:digest-PSS:SHA512");
const CFStringRef kSecKeyAlgorithmRSASignatureRaw = CFSTR("algid:sign:RSA:raw");


/*
 * CCRandomGenerateBytes — introduced in macOS 10.10
 * Needed by the rollup npm package.
 */

int CCRandomGenerateBytes(void *bytes, size_t count) {
	if (bytes == NULL || count == 0) {
		return -4300; /* kCCParamError */
	}
	arc4random_buf(bytes, count);
	return 0;
}

/*
 * aligned_alloc — C11 function, added to macOS in 10.15
 * Needed by deno.
 * Implemented via posix_memalign
 */

void * aligned_alloc(size_t alignment, size_t size) {
	if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
		errno = EINVAL;
		return NULL;
	}
	if (size % alignment != 0) {
		errno = EINVAL;
		return NULL;
	}

	void *ptr = NULL;
	int result = posix_memalign(&ptr, alignment, size);
	if (result != 0) {
		errno = result;
		return NULL;
	}

	return ptr;
}


/*
 * os_log stubs (added macOS 10.12)
 */

struct os_log_s { int dummy; };
static struct os_log_s _os_log_default_val = { 0 };
void *_os_log_default = &_os_log_default_val;

int os_log_type_enabled(void *log, int type) {
	(void)log; (void)type;
	return 0; /* logging disabled */
}

void _os_log_error_impl(void *dso, void *log, int type,
                        const char *format, void *buf, unsigned int size) {
	(void)dso; (void)log; (void)type; (void)format; (void)buf; (void)size;
}

void _os_log_impl(void *dso, void *log, int type,
                  const char *format, void *buf, unsigned int size) {
	(void)dso; (void)log; (void)type; (void)format; (void)buf; (void)size;
}


/*
 * kIOMainPortDefault (added macOS 12, replaces kIOMasterPortDefault)
 * Both are MACH_PORT_NULL (0).
 */

#include <mach/mach_port.h>
const mach_port_t kIOMainPortDefault = 0;


bool notify_is_valid_token(int token) {
	errno = ENOSYS;
	return false;
}

/*
 * QoS (Quality of Service) thread priority functions — introduced in
 * macOS 10.10. Stubbed because they are just optimization hints.
 */

#include <pthread.h>
typedef unsigned int qos_class_t;
typedef struct pthread_override_s *pthread_override_t;
pthread_override_t pthread_override_qos_class_start_np(pthread_t thread, qos_class_t qos_class, int relative_priority) {
	static int dummy_override = 1;
	return (pthread_override_t)&dummy_override;
}
int pthread_override_qos_class_end_np(pthread_override_t override) {
	return 0;
}
