#ifndef NATURE_RUNTIME_UV_COMPAT_H
#define NATURE_RUNTIME_UV_COMPAT_H

#ifdef __WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Nature has instruction-width and helper macros whose names overlap Win32
// typedefs and compatibility macros. Preserve their exact previous state while
// libuv and the Windows SDK headers are parsed.
#pragma push_macro("WORD")
#pragma push_macro("DWORD")
#pragma push_macro("VOID")
#pragma push_macro("min")
#pragma push_macro("max")
#pragma push_macro("interface")
#undef WORD
#undef DWORD
#undef VOID
#undef min
#undef max
#undef interface
#endif

#include <include/uv.h>
#ifdef __WINDOWS
// Load the remaining Win32 headers used by libuv's Windows implementation
// before restoring Nature's overlapping macros. Later includes are then no-ops
// through the Windows SDK include guards.
#include <userenv.h>
#include <iphlpapi.h>
#endif

#ifdef __WINDOWS
#pragma pop_macro("interface")
#pragma pop_macro("max")
#pragma pop_macro("min")
#pragma pop_macro("VOID")
#pragma pop_macro("DWORD")
#pragma pop_macro("WORD")
#endif

#endif // NATURE_RUNTIME_UV_COMPAT_H
