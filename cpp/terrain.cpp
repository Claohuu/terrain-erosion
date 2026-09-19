// Terrain generation and erosion.
//
// Phase 0: prove the toolchain works end to end. This file currently contains
// one trivial function whose only job is to be called successfully from
// JavaScript. Once that round trip works, the real code replaces it.

#include <emscripten/emscripten.h>

// extern "C" turns off C++ name mangling.
//
// C++ encodes argument types into the symbol name so that overloads can
// coexist -- add(int, int) might become _Z3addii in the compiled output.
// JavaScript has to look the function up by name, so it needs the plain,
// predictable spelling. extern "C" gives us that.
//
// EMSCRIPTEN_KEEPALIVE tells the compiler not to delete this function.
// Nothing inside C++ calls it, so the optimizer would otherwise conclude it
// is dead code and strip it out.

extern "C"
{
    EMSCRIPTEN_KEEPALIVE
    int add(int a, int b)
    {
        return a + b;
    }
}
