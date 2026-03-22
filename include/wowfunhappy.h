#ifndef _WOWFUNHAPPY_LEGACY_SUPPORT_H_
#define _WOWFUNHAPPY_LEGACY_SUPPORT_H_

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <stdbool.h>
#include <stddef.h>

bool SecTrustEvaluateWithError(SecTrustRef trust, CFErrorRef *error);

int CCRandomGenerateBytes(void *bytes, size_t count);

void *aligned_alloc(size_t alignment, size_t size);

#endif /* _WOWFUNHAPPY_LEGACY_SUPPORT_H_ */
