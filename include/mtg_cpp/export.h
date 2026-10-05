// Symbol visibility macro for the engine shared library (Phase 1).
//
// The engine is built as a single shared library (`mtg_cpp_engine`). On
// ELF/Mach-O we default to exporting everything unless the build is configured
// with hidden visibility; on Windows we must annotate every exported symbol.
// `MTG_CPP_EXPORT` is the single knob for that. By default (desktop, debug) it
// expands to nothing so existing headers keep working unchanged; the GDExtension
// build (Phase 2) will turn on hidden visibility + explicit exports.
#pragma once

#if defined(_WIN32)
  #if defined(MTG_CPP_BUILDING_ENGINE)
    #define MTG_CPP_EXPORT __declspec(dllexport)
  #else
    #define MTG_CPP_EXPORT __declspec(dllimport)
  #endif
#else
  #define MTG_CPP_EXPORT __attribute__((visibility("default")))
#endif
