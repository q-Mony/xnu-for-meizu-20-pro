// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// No state or keybag answer is encoded here; these are publication and lookup
// constraints for the one disposable packed RAM Data experiment.
inline bool meizuAksVolatileData(int dataError, unsigned storeMode, unsigned persistState,
                                 const void *ephemeral, size_t ephemeralBytes, size_t packedBytes)
{
    const uint8_t one[4] = {1, 0, 0, 0};
    return !dataError && storeMode == 0 && persistState == 0 &&
        ephemeral && ephemeralBytes == sizeof(one) &&
        memcmp(ephemeral, one, sizeof(one)) == 0 && packedBytes == 16;
}

inline bool meizuAksExactRequest(const char *requested, unsigned count,
                                 bool notificationUser, bool compatibility)
{
    return requested && strcmp(requested, "AppleKeyStore") == 0 &&
        count == 2 && notificationUser && !compatibility;
}
