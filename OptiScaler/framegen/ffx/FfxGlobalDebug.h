#pragma once

#include <ffx_api.h>

// ILION (AMD FSR SDK 2.4): ffxConfigureDescGlobalDebug1 is deprecated. ILION builds against the older ffx-api headers,
// which don't have its replacement yet: the descriptor below is AMD's, as in the SDK 2.4 ffx_api.h
#ifndef FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG
#define FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG 7u
struct ffxConfigureDescGlobalDebug
{
    ffxConfigureDescHeader header;
    uint64_t effectId;
    ffxApiMessage fpMessage; ///< A pointer to a function that can receive messages from the runtime. May be null.
    uint32_t debugLevel;
};
#endif
