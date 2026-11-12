#include <assert.h>
#define app_assert_status(status) assert((status) == SL_STATUS_OK)
#define app_assert(condition, ...) assert(condition)
