#pragma once

// OptiScaler is built against the headers of the official NVIDIA DLSS SDK, which are not part of this repository
// (see README.md). Those headers are written for a game that calls NGX; OptiScaler is on the other side and
// implements the entry points the game calls. This file holds the little that has to be added for that.
//
// Nothing here is taken from the SDK.

// The SDK declares a few entry points (the Init functions that take NVSDK_NGX_FeatureCommonInfo) without its export
// attribute, because for a game they live in the SDK's static library. OptiScaler has to export them, and the
// compiler does not accept adding the attribute at the definition. Written right before such a definition, this
// exports the function under its plain name, which is what the attribute did.
#define NVNGX_COMPAT_EXPORT(name) __pragma(comment(linker, "/EXPORT:" #name))
