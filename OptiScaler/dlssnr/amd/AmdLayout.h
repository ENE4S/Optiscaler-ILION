// Copyright (c) 2026 3zwr1 (AMDNR)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace AmdPreSr
{
// SHA256 as a fixed 32-byte digest. Prefer Sha256FromHex() over a raw
// {0x..} list: a hand-copied byte array that is one nibble short still
// compiles (the rest zero-fills) and then never matches at runtime.
struct Sha256
{
    unsigned char bytes[32];
};

constexpr unsigned char HexNibble(char c)
{
    if (c >= '0' && c <= '9')
        return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f')
        return static_cast<unsigned char>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return static_cast<unsigned char>(c - 'A' + 10);
    return 0xFF;
}

constexpr bool IsHexDigit(char c) { return HexNibble(c) <= 0x0F; }

// Exactly 64 hex digits + NUL. Wrong length is a compile error, which is
// the guard for the 0.3.1 digest that was hand-copied one character short.
template <std::size_t N>
constexpr Sha256 Sha256FromHex(const char (&hex)[N])
{
    static_assert(N == 65, "SHA256 hex literal must be exactly 64 hex digits (plus NUL)");
    Sha256 out {};
    for (std::size_t i = 0; i < 32; ++i)
    {
        const char h = hex[i * 2];
        const char l = hex[i * 2 + 1];
        if (!IsHexDigit(h) || !IsHexDigit(l))
            return Sha256 {}; // invalid digit → all-zero digest (will not match)
        out.bytes[i] = static_cast<unsigned char>((HexNibble(h) << 4) | HexNibble(l));
    }
    return out;
}

struct AmdLayout
{
    const char* name;
    std::size_t size;
    Sha256 sha256;
    std::uint32_t d3dCompileIat; // 0 if the runtime has no D3DCompile import
    std::uint32_t init;
    std::uint32_t record;
    std::uint32_t notify;
    std::uint32_t shutdown;
    std::uint32_t trampoline;
    std::uint32_t device;
    std::uint32_t queue;
    std::uint32_t engine;
    std::uint32_t historyView;
    std::uint32_t historyValid;
    std::uint32_t initDone;
    std::uint32_t nativeFailure;
    std::uint32_t configuredInline;
    std::uint32_t jobDone;
    std::uint32_t timeoutCount;
    std::uint32_t watchdog;
    std::uint32_t interop;
    std::uint32_t pendingList;
    std::uint32_t jobId;
    std::uint32_t depthInverted;
    std::uint32_t explicitDepth;
    std::uint32_t enabled;
    std::uint32_t temporal;
    std::uint32_t fsrInputs;
    std::uint32_t depthPresent;
    std::uint32_t tonemap;
    std::uint32_t tone;
    std::uint32_t structure;
    std::uint32_t skin;
    std::uint32_t charMask;
    std::uint32_t toneChannels;
    std::uint32_t hipOrdinal;
    // Sticky "staging must be re-created" byte. Set when the runtime detects a
    // resize, a re-created upscaler context, or an INI change; cleared only
    // after it has drained the game's queue and joined its workers. Record
    // tests it as its first act, so 0 means "this call will not rebuild".
    std::uint32_t recreate;
    // Address of the mutex guarding Record. The verified 0.3.1 entry
    // acquires it through a blocking SRW-lock path. +0x4c is its ownership /
    // recursion count, not a waiter count or worker-busy indicator. It cannot
    // explain a refused Record by itself. 0 means the field is not mapped.
    std::uint32_t recordLock;
    // Diagnostic-only fields checked by Record's non-inline admission path.
    // These jns gates are bypassed by normal inline admission; gate68 also
    // stores the singleton job waiting for Notify. They are not independent
    // inline refusal reasons. counter78 advances after the packet check and
    // is only a coarse indication of how far a call got.
    // 0.2.17's three are inferred from the same jns pair and the same +0x1C and
    // +0x10 spacing as 0.3.0's, not read off a 0.2.17 window - treat as unverified.
    std::uint32_t gate4c;
    std::uint32_t gate68;
    std::uint32_t counter78;
    // 0.3.1 only: inline-wait spin implementation. 0 = compute dispatch spin;
    // non-zero = predicated 1-pixel graphics draws. The compute path also has
    // 0.3.1-specific slicing, so SpinDraw=0 is not identical to the 0.3.0 wait.
    // 0 means "runtime does not expose this flag".
    std::uint32_t spinDraw;
    // (0.3.4, DANIEL-033-SET) The runtime's own dlssnr_on_amd.ini knobs, parsed once in its DllMain and read
    // live on every job (Style, ToneCurve, ToneLift) or frame (UseGameExposure). The host drives them only while
    // [DlssNr] AmdRuntimeStyle / AmdToneCurve / AmdToneLift / AmdUseGameExposure hold a value (-1 = auto = no
    // write). Widths from the runtime's own loads and stores: style int32 0..2 (fed as Style/128), toneCurve
    // int32 (0 Reinhard, 1 ACES), toneLift float 0..0.25, useGameExposure ONE byte (0/1; never write 4 bytes
    // there). Mapped for the public 0.3.3 and 0.4.0, (0.3.4, danielblnc support) the 8aa2dcc5 and d1e32086 rows and (0.3.5, danielblnc support) the cddfb09e row; 0 = not mapped (every other row zero-fills), and an unmapped knob is never read or
    // written.
    std::uint32_t style;
    std::uint32_t toneCurve;
    std::uint32_t toneLift;
    std::uint32_t useGameExposure;
    // (0.3.4, danielblnc support) The runtime's quality mode, ONE byte right after useGameExposure: 1 = Fast, 0 = Reference.
    // Its DllMain sets it from dlssnr_on_amd.ini [DlssNrOnAmd] Quality= (default "fast": Fast unless the ini says
    // otherwise), and the job function Record calls copies it into the engine on every job, so a host write before
    // Record acts on that job (live, like the four knobs above; nothing reads it at Init). One copy per pass module.
    // Driven only while [DlssNr] AmdDanielFastMode holds a value (unset = auto = no write). 0 = the build has no such
    // mode (every row but the three that map it: 8aa2dcc5, d1e32086 and cddfb09e); never write 4 bytes there.
    std::uint32_t quality;
};

// A row name that is a version number ("0.4.0"): digits and dots, starting and ending with a digit, at least one dot.
// Rows of builds AMDNR does not ship are named by their digest prefix instead, so no such version is written into
// AMDNR: what the menu and the logs show for them is read from the runtime file itself at run time
// (RuntimeVersionIn).
constexpr bool IsVersionName(const char* s)
{
    if (s == nullptr || !(*s >= '0' && *s <= '9'))
        return false;
    bool dot = false;
    char last = 0;
    for (; *s; ++s)
    {
        if (*s == '.')
        {
            if (last == '.')
                return false;
            dot = true;
        }
        else if (!(*s >= '0' && *s <= '9'))
            return false;
        last = *s;
    }
    return dot && last != '.';
}

// The version a danielblnc runtime file carries in its own overlay title: "DLSS-NR on AMD v<x.y.z>" (every build since
// 0.3.2; the newest ones have no fixed "(End to close)" tail any more), else the digits and dots right before the older
// title tail "   (End to close)". Empty when neither holds a version. Only a candidate that IsVersionName accepts
// counts, so a copy of either literal with no version beside it (OptiScaler.dll carries both) is skipped.
inline std::string RuntimeVersionIn(const unsigned char* data, std::size_t size)
{
    if (data == nullptr || size == 0)
        return {};
    const unsigned char* const end = data + size;
    const auto same = [](unsigned char a, char b) { return a == static_cast<unsigned char>(b); };
    const auto versionChar = [](unsigned char c) { return (c >= '0' && c <= '9') || c == '.'; };
    static constexpr char kMarker[] = "DLSS-NR on AMD v";
    for (const unsigned char* p = data;;)
    {
        p = std::search(p, end, kMarker, kMarker + sizeof(kMarker) - 1, same);
        if (p == end)
            break;
        const unsigned char* b = p + sizeof(kMarker) - 1;
        const unsigned char* e = b;
        while (e != end && versionChar(*e) && e - b < 16)
            ++e;
        const std::string version(b, e);
        if (IsVersionName(version.c_str()))
            return version;
        p = b;
    }
    static constexpr char kTail[] = "   (End to close)";
    for (const unsigned char* p = data;;)
    {
        p = std::search(p, end, kTail, kTail + sizeof(kTail) - 1, same);
        if (p == end)
            break;
        const unsigned char* b = p;
        while (b != data && versionChar(*(b - 1)) && p - b < 16)
            --b;
        const std::string version(b, p);
        if (IsVersionName(version.c_str()))
            return version;
        p += sizeof(kTail) - 1;
    }
    return {};
}

// 0.2.17 pass DLL, SHA256 bc97f3b0...
inline constexpr AmdLayout kAmd0217 {
    "0.2.17",
    7248384,
    Sha256FromHex("bc97f3b06718e19042acaf227bfe15d1e43d4977f9dc2e39994fcc511445ff4e"),
    0x80e48, 0x19240, 0xf600, 0x9170, 0x12690, 0x8daf8,
    0x8cee8, 0x8cef0, 0x8cef8, 0x8d010, 0x8d018, 0x8d218, 0x8d21a,
    0x8d6c0, 0x8d6f4, 0x8d6f8, 0x8d724, 0x8d82c, 0x8d908, 0x8d914,
    0x8d9b0, 0x8d9b4, 0x8d9bc, 0x8d9bd, 0x8d9be, 0x8d9bf, 0x8d9c0,
    0x8d9d0, 0x8d9d4, 0x8d9d8, 0x8d9e0, 0x8d9e4, 0x8dad0,
    0x8daa8, 0x8da30, 0x8d8f4, 0x8d910, 0x8d920,
    0
};

// Alpha 0.3.0 version.dll. Fields from unique 0.2.17 instruction windows;
// Record 0x12640 from pendingList/jobId xchg owner; Notify+0x13 still calls trampoline.
inline constexpr AmdLayout kAmd03 {
    "0.3.0",
    7290880,
    Sha256FromHex("8321cae728d28cb7632d0d58d3d913e91132bf7645c126505698fbe4cd5a0138"),
    0, 0x1fe80, 0x12640, 0x9460, 0x161e0, 0x97c70,
    0x96f68, 0x96f70, 0x96f78, 0x97090, 0x97098, 0x97298, 0x9729a,
    0x977a0, 0x977d4, 0x977d8, 0x97804, 0x97984, 0x97a60, 0x97a6c,
    0x97b10, 0x97b14, 0x97b1c, 0x97b1d, 0x97b1e, 0x97b1f, 0x97b20,
    0x97b30, 0x97b34, 0x97b38, 0x97b40, 0x97b44, 0x97c30,
    0x97c08, 0x97b90, 0x97a4c, 0x97a68, 0x97a78,
    0
};

// 0.3.1 version.dll (SHA b108d640). Mapped from 0.3.0 via unique instruction
// windows (analysis/map_a031_rva.py); Record/Notify/shutdown heads and the
// recreate sticky-bit xrefs match 0.3.0 role-for-role. Data section moved
// ~+0x3180 and .text grew — every RVA below is 0.3.1-specific.
inline constexpr AmdLayout kAmd031 {
    "0.3.1",
    7304192,
    Sha256FromHex("b108d6407eb7f094a4f9111edd778eee7b978b648d413a9fc7aeedfdd914c154"),
    0, 0x21720, 0x13540, 0x9720, 0x17150, 0x9ae68,
    0x9a0e8, 0x9a0f0, 0x9a100, 0x9a218, 0x9a220, 0x9a420, 0x9a422,
    0x9a928, 0x9a95c, 0x9a960, 0x9a98c, 0x9ab58, 0x9ac38, 0x9ac44,
    0x9ace8, 0x9acec, 0x9acf4, 0x9acf5, 0x9acf6, 0x9acf7, 0x9acf8,
    0x9ad08, 0x9ad0c, 0x9ad10, 0x9ad18, 0x9ad1c, 0x9ae08,
    0x9ade0, 0x9ad68, 0x9ac24, 0x9ac40, 0x9ac50,
    0x9ab14
};

// 0.3.2 version.dll (SHA b92f7481), from danielblnc's v0.3.2 setup overlay. .data identical to 0.3.1
// (same VA 0x96000/size 0x6590, same raw bytes apart from relocated pointers); every global plus
// Record/Notify/shutdown/trampoline at the 0.3.1 RVA. Only Init moved (-0x30): the HIP kernel-
// registration initializer before it (0x1f930) lost the k_swin_var<32,false> entry. No D3DCompile import.
inline constexpr AmdLayout kAmd032 {
    "0.3.2",
    6788096,
    Sha256FromHex("b92f7481bc03fa41f443b1e1e54b502789df2bbbcefe48c680df7bb02a33fc1e"),
    0, 0x216f0, 0x13540, 0x9720, 0x17150, 0x9ae68,
    0x9a0e8, 0x9a0f0, 0x9a100, 0x9a218, 0x9a220, 0x9a420, 0x9a422,
    0x9a928, 0x9a95c, 0x9a960, 0x9a98c, 0x9ab58, 0x9ac38, 0x9ac44,
    0x9ace8, 0x9acec, 0x9acf4, 0x9acf5, 0x9acf6, 0x9acf7, 0x9acf8,
    0x9ad08, 0x9ad0c, 0x9ad10, 0x9ad18, 0x9ad1c, 0x9ae08,
    0x9ade0, 0x9ad68, 0x9ac24, 0x9ac40, 0x9ac50,
    0x9ab14
};

// 0.3.3 version.dll (SHA 907b30a6), carved from danielblnc's v0.3.3 setup (5) at 0x2609c7. .data moved
// 0x96000 -> 0x9d000 and grew (new Style/ToneCurve/ToneLift/UseGameExposure globals at 0xa2510..0xa251c,
// driven by the host only through the 0.3.4 knob keys, last row), so every RVA moved. Derived twice
// independently (mapped from 0.3.2, and from scratch via ini-parser writes and call sites); both agree on
// every value. Init/Record/Notify/
// shutdown bodies are instruction-for-instruction 0.3.2's; packet still 0x60 bytes. No D3DCompile import.
inline constexpr AmdLayout kAmd033 {
    "0.3.3",
    7607296,
    Sha256FromHex("907b30a61644a6d7e43e58a43a9d97a04a24b1a764a88bdef3954ac807e8d112"),
    0, 0x23be0, 0x149c0, 0x9b00, 0x185d0, 0xa2680,
    0xa18c0, 0xa18c8, 0xa18d8, 0xa19f8, 0xa1a00, 0xa1c10, 0xa1c12,
    0xa2118, 0xa214c, 0xa2150, 0xa217c, 0xa2348, 0xa2428, 0xa2434,
    0xa24d8, 0xa24dc, 0xa24e4, 0xa24e5, 0xa24e6, 0xa24e7, 0xa24e8,
    0xa24f8, 0xa24fc, 0xa2500, 0xa2508, 0xa250c, 0xa2608,
    0xa25e0, 0xa2568, 0xa2414, 0xa2430, 0xa2440,
    0xa2304,
    0xa2510, 0xa2514, 0xa2518, 0xa251c
};

// 0.4.0 version.dll (SHA d62be3d8), danielblnc's public 0.4.0 (setup carved at
// 0x2609c7). .data moved to 0xa3000; the engine object grew 0x28 (historyView/historyValid +0x6028, the
// rest +0x6000/+0x6100/+0x6118/+0x6120). New PollSpacing ini dword at spinDraw+4 (default 0, not driven).
// Derived twice independently; both agree. Init/Record/Notify/shutdown are 0.3.3's instruction for
// instruction (packet still 0x60 bytes, recordLock +0x4c still the ownership count).
inline constexpr AmdLayout kAmd040 {
    "0.4.0",
    10027008,
    Sha256FromHex("d62be3d8b9fbb3c6c81982c4ddb3dfa00eb9662e3206925cbe5b7e1bc6798b80"),
    0, 0x26110, 0x14cd0, 0x9e10, 0x188e0, 0xa87a0,
    0xa78c0, 0xa78c8, 0xa78d8, 0xa7a20, 0xa7a28, 0xa7d10, 0xa7d12,
    0xa8218, 0xa824c, 0xa8250, 0xa827c, 0xa8468, 0xa8548, 0xa8554,
    0xa85f8, 0xa85fc, 0xa8604, 0xa8605, 0xa8606, 0xa8607, 0xa8608,
    0xa8618, 0xa861c, 0xa8620, 0xa8628, 0xa862c, 0xa8728,
    0xa8700, 0xa8688, 0xa8534, 0xa8550, 0xa8560,
    0xa841c,
    0xa8630, 0xa8634, 0xa8638, 0xa863c
};

// version.dll SHA 823063eb: a build AMDNR does not ship, recognised only so a player's own copy keeps working.
// .data at 0xa5000. Derived twice independently; both agree; same host contract as 0.4.0.
inline constexpr AmdLayout kAmd041 {
    "0.4.1",
    9916928,
    Sha256FromHex("823063eb4c76b1334fd1800c41798873ae61d4016af0406f1f0b9dce57b1d376"),
    0, 0x26130, 0x14c40, 0x9d80, 0x188d0, 0xaa7d8,
    0xa98e0, 0xa98e8, 0xa98f8, 0xa9a40, 0xa9a48, 0xa9d48, 0xa9d4a,
    0xaa250, 0xaa284, 0xaa288, 0xaa2b4, 0xaa4a0, 0xaa580, 0xaa58c,
    0xaa630, 0xaa634, 0xaa63c, 0xaa63d, 0xaa63e, 0xaa63f, 0xaa640,
    0xaa650, 0xaa654, 0xaa658, 0xaa660, 0xaa664, 0xaa760,
    0xaa738, 0xaa6c0, 0xaa56c, 0xaa588, 0xaa598,
    0xaa454
};

// version.dll SHA 8aa2dcc5: a build AMDNR does not ship, recognised only so a player's own copy keeps working.
// .data at 0xaa000; the engine object grew 0x10. Derived twice independently; both agree. Init/Notify/shutdown
// match the entry above instruction for instruction; Record differs only in internal offsets.
// (0.3.4, danielblnc support) Named by its digest prefix (the menu and the logs read its version from the file). The four
// knobs sit right after toneChannels as in 0.3.3 / 0.4.0 (found from the ini parser's stores and the job function's
// loads, three methods agree), and this build has the quality byte right after useGameExposure.
inline constexpr AmdLayout kAmd042 {
    "8aa2dcc5",
    12981760,
    Sha256FromHex("8aa2dcc5b6596aca97995dbfd4e0a9790d8c15108495e0ed154dd15dbb5b465a"),
    0, 0x28170, 0x15040, 0x9db0, 0x18cf0, 0xaf9b0,
    0xaeaa0, 0xaeaa8, 0xaeab8, 0xaec00, 0xaec08, 0xaef18, 0xaef1a,
    0xaf420, 0xaf454, 0xaf458, 0xaf484, 0xaf678, 0xaf758, 0xaf764,
    0xaf808, 0xaf80c, 0xaf814, 0xaf815, 0xaf816, 0xaf817, 0xaf818,
    0xaf828, 0xaf82c, 0xaf830, 0xaf838, 0xaf83c, 0xaf938,
    0xaf910, 0xaf898, 0xaf744, 0xaf760, 0xaf770,
    0xaf624,
    0xaf840, 0xaf844, 0xaf848, 0xaf84c,
    0xaf84d
};

// version.dll SHA d1e32086 (0.3.4, danielblnc support): a build AMDNR does not ship, recognised only so a player's own copy
// keeps working; named by its digest prefix (the menu and the logs read its version from the file). .data at
// 0xac000; against the entry above: device..historyValid +0x2170, initDone..quality +0x2178, trampoline +0x21a0 (the
// engine object grew 8 bytes past +0x190). Derived three times independently (mapped from the entry above, from
// scratch, and per reference: same instruction ordinal, mnemonic and width as the entry above's); all agree. Init /
// Record / Notify / shutdown are the entry above's instruction for instruction apart from engine-internal offsets;
// packet still 0x60 bytes, recordLock +0x4c still the ownership count, DllMain still starts one thread. Its overlay
// key (OverlayKey=) is polled only from the Present hook its bootstrap installs, which the host isolates.
inline constexpr AmdLayout kAmd043 {
    "d1e32086",
    12749824,
    Sha256FromHex("d1e320862a8763ac39e7ce194536d4b6c55ba61bae9e8a92753cec32df67a457"),
    0, 0x28a20, 0x157f0, 0xa200, 0x194a0, 0xb1b50,
    0xb0c10, 0xb0c18, 0xb0c28, 0xb0d70, 0xb0d78, 0xb1090, 0xb1092,
    0xb1598, 0xb15cc, 0xb15d0, 0xb15fc, 0xb17f0, 0xb18d0, 0xb18dc,
    0xb1980, 0xb1984, 0xb198c, 0xb198d, 0xb198e, 0xb198f, 0xb1990,
    0xb19a0, 0xb19a4, 0xb19a8, 0xb19b0, 0xb19b4, 0xb1ab0,
    0xb1a88, 0xb1a10, 0xb18bc, 0xb18d8, 0xb18e8,
    0xb179c,
    0xb19b8, 0xb19bc, 0xb19c0, 0xb19c4,
    0xb19c5
};

// version.dll SHA cddfb09e (0.3.5, danielblnc support): a build AMDNR does not ship, recognised only so a player's own copy keeps
// working; named by its digest prefix (the menu and the logs read its version from the file). .data at 0xb1000; against
// the entry above: device..engine +0x5008, historyView / historyValid +0x5018 (the engine head grew 0x10), trampoline
// and initDone..quality +0x5038 (the engine object grew 0x30). Derived twice independently (mapped from the entry above
// through masked instruction windows, and from scratch through the ini parser's stores, the call sites and per-field
// reference signatures); both agree on every value. Init / Notify / shutdown are the entry above's instruction for
// instruction apart from engine-internal offsets; Record adds one 13-instruction one-time log block in its warm-up
// branch and reads the packet exactly as before (still 0x60 bytes); recordLock +0x4c still the ownership count; DllMain
// still starts one thread. The four knobs and the quality byte keep their widths (int32, int32, float, one byte, one
// byte) and sit right after toneChannels as in the two rows above. No D3DCompile import. The file is 38.7 MB, under
// the 64 MiB read cap of IdentifyRuntime / DescribeRuntimeFile.
inline constexpr AmdLayout kAmd050 {
    "cddfb09e",
    38703616,
    Sha256FromHex("cddfb09e019347957bf7b96c95c0e900e8d3062dfaed697a8a96b0a039aec31a"),
    0, 0x29870, 0x15640, 0xa000, 0x19340, 0xb6b88,
    0xb5c18, 0xb5c20, 0xb5c30, 0xb5d88, 0xb5d90, 0xb60c8, 0xb60ca,
    0xb65d0, 0xb6604, 0xb6608, 0xb6634, 0xb6828, 0xb6908, 0xb6914,
    0xb69b8, 0xb69bc, 0xb69c4, 0xb69c5, 0xb69c6, 0xb69c7, 0xb69c8,
    0xb69d8, 0xb69dc, 0xb69e0, 0xb69e8, 0xb69ec, 0xb6ae8,
    0xb6ac0, 0xb6a48, 0xb68f4, 0xb6910, 0xb6920,
    0xb67d4,
    0xb69f0, 0xb69f4, 0xb69f8, 0xb69fc,
    0xb69fd
};

// ILION: Daniel 0.6.0 map from OpenNR 8098b8e; checked against the user-supplied runtime.
inline constexpr AmdLayout kAmd060 {
    .name = "0.6.0",
    .size = 56677888,
    .sha256 = Sha256FromHex("195c4a891b6eac4c1cb7671e10ff62bbbe2b17f1dfae1344dc5a6714e4775721"),
    .d3dCompileIat = 0,
    .init = 0x2cfb0,
    .record = 0x17680,
    .notify = 0xb4a0,
    .shutdown = 0x1b420,
    .trampoline = 0xbed08,
    .device = 0xbdc88,
    .queue = 0xbdc90,
    .engine = 0xbdca0,
    .historyView = 0xbde10,
    .historyValid = 0xbde18,
    .initDone = 0xbe1e8,
    .nativeFailure = 0xbe1ea,
    .configuredInline = 0xbe6f0,
    .jobDone = 0xbe724,
    .timeoutCount = 0xbe728,
    .watchdog = 0xbe754,
    .interop = 0xbe948,
    .pendingList = 0xbea28,
    .jobId = 0xbea34,
    .depthInverted = 0xbead8,
    .explicitDepth = 0xbeadc,
    .enabled = 0xbeaed,
    .temporal = 0xbeaee,
    .fsrInputs = 0xbeaef,
    .depthPresent = 0xbeaf0,
    .tonemap = 0xbeaf4,
    .tone = 0xbeb04,
    .structure = 0xbeb08,
    .skin = 0xbeb0c,
    .charMask = 0xbeb14,
    .toneChannels = 0xbeb18,
    .hipOrdinal = 0xbec30,
    .recreate = 0xbec08,
    .recordLock = 0xbeb70,
    .gate4c = 0xbea14,
    .gate68 = 0xbea30,
    .counter78 = 0xbea40,
    .spinDraw = 0xbe8f4,
    .style = 0xbeb1c,
    .toneCurve = 0xbeb20,
    .toneLift = 0xbeb24,
    .useGameExposure = 0xbeb28,
    .quality = 0xbeb29,
};

inline constexpr const AmdLayout* kAmdLayouts[] = { &kAmd0217, &kAmd03, &kAmd031, &kAmd032, &kAmd033,
                                                    &kAmd040, &kAmd041, &kAmd042, &kAmd043, &kAmd050, &kAmd060 };

// Compile-time sanity: the hex helper must land on the first/last digest byte
// of each known runtime. A wrong-length literal already fails Sha256FromHex;
// these catch a copy-paste that swapped two mid-string bytes.
static_assert(kAmd0217.sha256.bytes[0] == 0xbc && kAmd0217.sha256.bytes[31] == 0x4e);
static_assert(kAmd03.sha256.bytes[0] == 0x83 && kAmd03.sha256.bytes[31] == 0x38);
static_assert(kAmd031.sha256.bytes[0] == 0xb1 && kAmd031.sha256.bytes[31] == 0x54);
static_assert(kAmd032.sha256.bytes[0] == 0xb9 && kAmd032.sha256.bytes[31] == 0x1e);
static_assert(kAmd033.sha256.bytes[0] == 0x90 && kAmd033.sha256.bytes[31] == 0x12);
static_assert(kAmd040.sha256.bytes[0] == 0xd6 && kAmd040.sha256.bytes[31] == 0x80);
static_assert(kAmd041.sha256.bytes[0] == 0x82 && kAmd041.sha256.bytes[31] == 0x76);
static_assert(kAmd042.sha256.bytes[0] == 0x8a && kAmd042.sha256.bytes[31] == 0x5a);
static_assert(kAmd043.sha256.bytes[0] == 0xd1 && kAmd043.sha256.bytes[31] == 0x57);
static_assert(kAmd050.sha256.bytes[0] == 0xcd && kAmd050.sha256.bytes[31] == 0x1a);

// (0.3.4) The runtime knobs: 0.4.0's four sit at 0.3.3's + 0x6120, as toneChannels does (0xa250c -> 0xa862c), and
// right after toneChannels in both; the rows of 0.2.17 .. 0.3.2 and the 823063eb row leave them unmapped.
constexpr bool KnobsUnmapped(const AmdLayout& l)
{
    return l.style == 0 && l.toneCurve == 0 && l.toneLift == 0 && l.useGameExposure == 0;
}
// (0.3.4, danielblnc support) The quality mode byte (Fast / Reference). NOT implied by the four knobs: the public 0.3.3 and
// 0.4.0 map the knobs and have no such mode.
constexpr bool QualityMapped(const AmdLayout& l) { return l.quality != 0; }
static_assert(kAmd042.style == kAmd042.toneChannels + 4 && kAmd042.toneCurve == kAmd042.style + 4 &&
              kAmd042.toneLift == kAmd042.style + 8 && kAmd042.useGameExposure == kAmd042.style + 12 &&
              kAmd042.quality == kAmd042.useGameExposure + 1);
static_assert(kAmd043.style == kAmd043.toneChannels + 4 && kAmd043.toneCurve == kAmd043.style + 4 &&
              kAmd043.toneLift == kAmd043.style + 8 && kAmd043.useGameExposure == kAmd043.style + 12 &&
              kAmd043.quality == kAmd043.useGameExposure + 1);
static_assert(kAmd043.device == kAmd042.device + 0x2170 && kAmd043.historyValid == kAmd042.historyValid + 0x2170 &&
              kAmd043.initDone == kAmd042.initDone + 0x2178 && kAmd043.spinDraw == kAmd042.spinDraw + 0x2178 &&
              kAmd043.quality == kAmd042.quality + 0x2178 && kAmd043.trampoline == kAmd042.trampoline + 0x21a0);
// (0.3.5, danielblnc support) The cddfb09e row against d1e32086: three delta groups (the engine head grew 0x10 before historyView
// and 0x30 in all); the knobs and the quality byte keep their spacing.
static_assert(kAmd050.style == kAmd050.toneChannels + 4 && kAmd050.toneCurve == kAmd050.style + 4 &&
              kAmd050.toneLift == kAmd050.style + 8 && kAmd050.useGameExposure == kAmd050.style + 12 &&
              kAmd050.quality == kAmd050.useGameExposure + 1);
static_assert(kAmd050.device == kAmd043.device + 0x5008 && kAmd050.queue == kAmd043.queue + 0x5008 &&
              kAmd050.engine == kAmd043.engine + 0x5008 && kAmd050.historyView == kAmd043.historyView + 0x5018 &&
              kAmd050.historyValid == kAmd043.historyValid + 0x5018 && kAmd050.initDone == kAmd043.initDone + 0x5038 &&
              kAmd050.spinDraw == kAmd043.spinDraw + 0x5038 && kAmd050.hipOrdinal == kAmd043.hipOrdinal + 0x5038 &&
              kAmd050.quality == kAmd043.quality + 0x5038 && kAmd050.trampoline == kAmd043.trampoline + 0x5038);
static_assert(QualityMapped(kAmd042) && QualityMapped(kAmd043) && QualityMapped(kAmd050));
static_assert(!QualityMapped(kAmd0217) && !QualityMapped(kAmd03) && !QualityMapped(kAmd031) &&
              !QualityMapped(kAmd032) && !QualityMapped(kAmd033) && !QualityMapped(kAmd040) && !QualityMapped(kAmd041));
// Rows of builds AMDNR does not ship carry no version in their name (the menu and the logs read it from the file).
static_assert(!IsVersionName(kAmd042.name) && !IsVersionName(kAmd043.name) && !IsVersionName(kAmd050.name));
static_assert(IsVersionName(kAmd0217.name) && IsVersionName(kAmd03.name) && IsVersionName(kAmd031.name) &&
              IsVersionName(kAmd032.name) && IsVersionName(kAmd033.name) && IsVersionName(kAmd040.name));
static_assert(kAmd033.style == kAmd033.toneChannels + 4 && kAmd033.toneCurve == kAmd033.style + 4 &&
              kAmd033.toneLift == kAmd033.style + 8 && kAmd033.useGameExposure == kAmd033.style + 12);
static_assert(kAmd040.style == kAmd033.style + 0x6120 && kAmd040.toneCurve == kAmd033.toneCurve + 0x6120 &&
              kAmd040.toneLift == kAmd033.toneLift + 0x6120 &&
              kAmd040.useGameExposure == kAmd033.useGameExposure + 0x6120 &&
              kAmd040.toneChannels == kAmd033.toneChannels + 0x6120);
static_assert(KnobsUnmapped(kAmd0217) && KnobsUnmapped(kAmd03) && KnobsUnmapped(kAmd031) && KnobsUnmapped(kAmd032) &&
              KnobsUnmapped(kAmd041));
static_assert(!KnobsUnmapped(kAmd033) && !KnobsUnmapped(kAmd040) && !KnobsUnmapped(kAmd042) &&
              !KnobsUnmapped(kAmd043) && !KnobsUnmapped(kAmd050));
}
