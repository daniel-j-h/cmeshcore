#ifndef CMESHCORE_UTILS_H
#define CMESHCORE_UTILS_H

#include <assert.h>

#ifndef CMESHCORE_ASSERT
#ifndef NDEBUG
#define CMESHCORE_ASSERT(x) (assert(x))
#else
#define CMESHCORE_ASSERT(x)
#endif
#else
#endif

#ifndef CMESHCORE_WARN_UNUSED
#define CMESHCORE_WARN_UNUSED __attribute__((warn_unused_result))
#endif

#endif
