// Terrain generation and erosion.

#include <emscripten/emscripten.h>
#include <cstdlib>   // malloc

// ---------------------------------------------------------------------------
// Helpers (boilerplate -- take these as given)
// ---------------------------------------------------------------------------

// Deterministic pseudo-random value in [0, 1] for a given grid corner.
//
// This must be a function of its inputs, not a stream like rand(). Every pixel
// inside a grid cell asks for the same four corners, so asking for corner
// (1, 0) must return the same number every single time.
//
// The large primes and xor-shifts exist to mix the bits, so that inputs one
// apart produce completely unrelated outputs.
float hash(int x, int y, int seed)
{
    int h = x * 374761393 + y * 668265263 + seed * 362437;
    h = (h ^ (h >> 13)) * 1274126177;
    h = h ^ (h >> 16);
    return (float)(h & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

// Smoothstep. Maps 0 to 0 and 1 to 1, but flattens the slope to zero at both
// ends. Interpolating with a straight t leaves a visible diamond grid pattern,
// because the slope changes abruptly at every cell boundary.
float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

// Linear interpolation: at t=0 returns a, at t=1 returns b.
float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

// ---------------------------------------------------------------------------
// Your code goes below
// ---------------------------------------------------------------------------

// TODO: bilinear value noise.
//
//   1. Convert (x, y) into grid space by dividing by gridSize
//   2. x0 = floor, x1 = x0 + 1, fx = the fractional part  (same for y)
//   3. Fetch hash() at the four corners
//   4. Blend the top two by fx, blend the bottom two by fx,
//      then blend those two results by fy
//   5. Run fx and fy through smooth() before using them
//
// Try it with raw fx/fy first and look at the output -- the grid artifact is
// worth seeing once.
float valueNoise(float x, float y, float gridSize, int seed)
{
    return 0.0f;
}

extern "C"
{
    // TODO: allocate width*height floats, fill every pixel with valueNoise,
    // return the pointer. Store at index (x + y * width) -- row-major.
    //
    // Returning 0 (null) is what the page reports as "not written yet".
    EMSCRIPTEN_KEEPALIVE
    float* generate(int width, int height, int seed, float gridSize)
    {
        return 0;
    }
}
