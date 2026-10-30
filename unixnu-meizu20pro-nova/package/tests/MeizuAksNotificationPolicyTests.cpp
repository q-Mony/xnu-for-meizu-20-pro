// SPDX-License-Identifier: MIT
#include "../MeizuAksNotificationPolicy.h"
#include <assert.h>
#include <stdio.h>

int main()
{
    const unsigned char yes[4] = {1, 0, 0, 0};
    const unsigned char no[4] = {0, 0, 0, 0};
    assert(meizuAksVolatileData(0, 0, 0, yes, 4, 16));
    assert(!meizuAksVolatileData(1, 0, 0, yes, 4, 16));
    assert(!meizuAksVolatileData(0, 1, 0, yes, 4, 16));
    assert(!meizuAksVolatileData(0, 0, 3, yes, 4, 16));
    assert(!meizuAksVolatileData(0, 0, 0, nullptr, 4, 16));
    assert(!meizuAksVolatileData(0, 0, 0, yes, 3, 16));
    assert(!meizuAksVolatileData(0, 0, 0, no, 4, 16));
    assert(!meizuAksVolatileData(0, 0, 0, yes, 4, 0));
    assert(meizuAksExactRequest("AppleKeyStore", 2, true, false));
    assert(!meizuAksExactRequest("AppleKeyStore", 3, true, false));
    assert(!meizuAksExactRequest("AppleKeyStore", 2, false, false));
    assert(!meizuAksExactRequest("AppleKeyStore", 2, true, true));
    assert(!meizuAksExactRequest("AppleKeyStoreUserClient", 2, true, false));
    assert(!meizuAksExactRequest(nullptr, 2, true, false));
    puts("PASS: volatile Data and exact class-only lookup policy");
}
