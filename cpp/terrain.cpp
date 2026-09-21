// Terrain generation and hydraulic erosion.
//
// Compiled to WebAssembly with emcc (see build.sh) and driven from React.
// Everything here operates on one flat array of floats, row-major, indexed
// as (x + y * width).

// Emscripten only exists in the WASM build. Compiling natively (for the test
// harness) needs the macro to vanish rather than the header to be found.
#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#else
  #define EMSCRIPTEN_KEEPALIVE
#endif
#include <cstdlib>   // malloc, free
#include <cmath>     // floorf, sqrtf

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Deterministic pseudo-random value in [0, 1] for a given grid corner.
//
// This must be a pure function of its inputs, not a stream like rand(). Every
// pixel inside a grid cell asks for the same four corners, so corner (1, 0)
// must return the same number every single time it is asked for.
//
// The large primes and xor-shifts mix the bits, so inputs one apart produce
// completely unrelated outputs.
float hash(int x, int y, int seed)
{
    int h = x * 374761393 + y * 668265263 + seed * 362437;
    h = (h ^ (h >> 13)) * 1274126177;
    h = h ^ (h >> 16);
    return (float)(h & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

// Smoothstep. Maps 0 to 0 and 1 to 1, but flattens the slope to zero at both
// ends. Interpolating with a raw t leaves a visible diamond grid pattern,
// because the slope changes abruptly at every cell boundary.
float smoothCurve(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float minf(float a, float b)
{
    if (a < b)
    {
        return a;
    }

    return b;
}

float maxf(float a, float b)
{
    if (a > b)
    {
        return a;
    }

    return b;
}

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------

// Value noise: random values on a coarse lattice, smoothly blended between.
//
// Pure random per pixel would be TV static -- no relationship between
// neighbours. Placing random values only at grid corners and interpolating
// between them means nearby points get similar values, which is what makes it
// read as landscape rather than noise.
float valueNoise(float x, float y, float gridSize, int seed)
{
    float gx = x / gridSize;
    float gy = y / gridSize;

    int x0 = (int)floorf(gx);
    int y0 = (int)floorf(gy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    // How far into this cell we are, on each axis, in [0, 1).
    float fx = gx - (float)x0;
    float fy = gy - (float)y0;

    // Curve them so cells join without a visible crease.
    float sx = smoothCurve(fx);
    float sy = smoothCurve(fy);

    float c00 = hash(x0, y0, seed);
    float c10 = hash(x1, y0, seed);
    float c01 = hash(x0, y1, seed);
    float c11 = hash(x1, y1, seed);

    // Bilinear blend: mix along x twice, then mix those results along y.
    float top = lerp(c00, c10, sx);
    float bottom = lerp(c01, c11, sx);

    return lerp(top, bottom, sy);
}

// Fractal Brownian motion: sum several octaves of noise.
//
// Each successive octave has higher frequency (finer features) and lower
// amplitude (less influence). One octave alone is smooth blobs. Stacking them
// adds roughness at every scale, which is what natural terrain looks like --
// big landforms with small detail riding on top.
//
//   lacunarity  = how much frequency multiplies per octave (2.0 = double)
//   persistence = how much amplitude multiplies per octave (0.5 = halve)
float fbm(float x, float y, float gridSize, int seed,
          int octaves, float persistence, float lacunarity)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;   // used to normalise back into [0, 1]

    for (int i = 0; i < octaves; i += 1)
    {
        // Offsetting the seed per octave keeps layers from correlating --
        // otherwise every octave would peak in the same places.
        total += valueNoise(x * frequency, y * frequency, gridSize, seed + i * 7919) * amplitude;

        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

// ---------------------------------------------------------------------------
// Erosion
// ---------------------------------------------------------------------------

// Simulation constants that are not worth exposing as sliders. These were
// chosen by tuning; the ones that visibly matter are parameters of erode().
const int   MAX_LIFETIME      = 30;     // steps before a droplet gives up
const float CAPACITY_FACTOR   = 4.0f;   // scales how much sediment water holds
const float MIN_CAPACITY      = 0.01f;  // stops division-by-tiny on flat ground
const float EVAPORATE_SPEED   = 0.02f;  // fraction of water lost per step
const float GRAVITY           = 4.0f;   // converts height drop into speed
const float INITIAL_SPEED     = 1.0f;
const float INITIAL_WATER     = 1.0f;

// A droplet sits at a fractional position, so both the height under it and the
// slope it feels are interpolated from the four surrounding cells.
struct HeightAndGradient
{
    float height;
    float gradientX;
    float gradientY;
};

HeightAndGradient sampleHeightAndGradient(const float* heights, int width,
                                          float posX, float posY)
{
    int x0 = (int)posX;
    int y0 = (int)posY;

    float u = posX - (float)x0;   // offset within the cell, 0..1
    float v = posY - (float)y0;

    int index = x0 + y0 * width;

    float nw = heights[index];
    float ne = heights[index + 1];
    float sw = heights[index + width];
    float se = heights[index + width + 1];

    HeightAndGradient result;

    // The gradient is the rate of change of height. Differencing the corner
    // values along each axis, then blending by the offset on the other axis,
    // gives the slope the droplet actually feels at this exact point rather
    // than at the nearest cell centre.
    result.gradientX = (ne - nw) * (1.0f - v) + (se - sw) * v;
    result.gradientY = (sw - nw) * (1.0f - u) + (se - ne) * u;

    result.height = nw * (1.0f - u) * (1.0f - v)
                  + ne * u * (1.0f - v)
                  + sw * (1.0f - u) * v
                  + se * u * v;

    return result;
}

// Height only, no gradient. The droplet loop needs the full gradient at its
// current position to steer, but only the height at the position it moved to.
// Computing both and discarding half is wasted work in the hottest loop.
float sampleHeight(const float* heights, int width, float posX, float posY)
{
    int x0 = (int)posX;
    int y0 = (int)posY;

    float u = posX - (float)x0;
    float v = posY - (float)y0;

    int index = x0 + y0 * width;

    float nw = heights[index];
    float ne = heights[index + 1];
    float sw = heights[index + width];
    float se = heights[index + width + 1];

    return nw * (1.0f - u) * (1.0f - v)
         + ne * u * (1.0f - v)
         + sw * (1.0f - u) * v
         + se * u * v;
}

extern "C"
{
    // Allocates and fills a heightmap. Caller (JavaScript) owns the memory and
    // must call free() on the returned pointer.
    //
    // The returned value is a byte offset into WebAssembly's linear memory --
    // JS reads the same bytes through HEAPF32 without copying.
    EMSCRIPTEN_KEEPALIVE
    float* generate(int width, int height, int seed, float gridSize,
                    int octaves, float persistence, float lacunarity)
    {
        float* heights = (float*)malloc((size_t)width * (size_t)height * sizeof(float));

        if (heights == 0)
        {
            return 0;
        }

        for (int y = 0; y < height; y += 1)
        {
            for (int x = 0; x < width; x += 1)
            {
                float value = fbm((float)x, (float)y, gridSize, seed,
                                  octaves, persistence, lacunarity);

                heights[x + y * width] = value;
            }
        }

        return heights;
    }

    // Runs hydraulic erosion in place on an existing heightmap.
    //
    //   inertia       0 = water follows the slope exactly, 1 = it never turns.
    //                 Low values carve tight ravines, high values carve
    //                 sweeping valleys that cut across contours.
    //   erodeSpeed    fraction of the capacity deficit scraped up per step
    //   depositSpeed  fraction of the excess dropped per step
    //   radius        how wide the erosion brush is; 1 gives single-pixel
    //                 scratches, larger values give believable valley walls
    EMSCRIPTEN_KEEPALIVE
    void erode(float* heights, int width, int height,
               int numDroplets, int seed,
               float inertia, float erodeSpeed, float depositSpeed, int radius)
    {
        if (radius < 1)
        {
            radius = 1;
        }

        // ------------------------------------------------------------------
        // Precompute the erosion brush.
        //
        // Removing height from a single cell leaves pixel-wide scratches. A
        // droplet should scrape a small neighbourhood, weighted by distance,
        // so valley walls slope instead of stepping. The brush is the same for
        // every droplet, so it is built once here rather than per step.
        // ------------------------------------------------------------------
        int maxBrush = (2 * radius + 1) * (2 * radius + 1);

        // Store each brush cell as a single flat index offset (dx + dy*width)
        // rather than a separate dx and dy. Applying the brush then becomes one
        // addition per cell instead of two multiplies and an index computation.
        int* brushOffset = (int*)malloc((size_t)maxBrush * sizeof(int));
        float* brushWeight = (float*)malloc((size_t)maxBrush * sizeof(float));

        if (brushOffset == 0 || brushWeight == 0)
        {
            free(brushOffset);
            free(brushWeight);
            return;
        }

        int brushCount = 0;
        float weightSum = 0.0f;

        for (int dy = -radius; dy <= radius; dy += 1)
        {
            for (int dx = -radius; dx <= radius; dx += 1)
            {
                float distance = sqrtf((float)(dx * dx + dy * dy));

                if (distance <= (float)radius)
                {
                    float weight = 1.0f - distance / (float)radius;

                    brushOffset[brushCount] = dx + dy * width;
                    brushWeight[brushCount] = weight;
                    weightSum += weight;
                    brushCount += 1;
                }
            }
        }

        // Normalise so one erosion event removes exactly the intended amount,
        // spread across the brush, regardless of radius.
        for (int i = 0; i < brushCount; i += 1)
        {
            brushWeight[i] /= weightSum;
        }

        // Simple linear congruential generator. Deterministic for a given
        // seed, which is what makes every run reproducible.
        // Confine droplets to an interior margin wide enough that the brush
        // always lands fully inside the map. That lets the innermost loop drop
        // its bounds check entirely -- the check was four comparisons per cell,
        // running roughly a hundred million times.
        int margin = radius + 1;

        if (width <= 2 * margin + 2 || height <= 2 * margin + 2)
        {
            free(brushOffset);
            free(brushWeight);
            return;
        }

        int spawnW = width - 2 * margin;
        int spawnH = height - 2 * margin;

        unsigned int rng = (unsigned int)seed * 747796405u + 2891336453u;

        for (int droplet = 0; droplet < numDroplets; droplet += 1)
        {
            rng = rng * 1664525u + 1013904223u;
            float posX = (float)(rng % (unsigned int)spawnW) + (float)margin;

            rng = rng * 1664525u + 1013904223u;
            float posY = (float)(rng % (unsigned int)spawnH) + (float)margin;

            float dirX = 0.0f;
            float dirY = 0.0f;
            float speed = INITIAL_SPEED;
            float water = INITIAL_WATER;
            float sediment = 0.0f;

            for (int step = 0; step < MAX_LIFETIME; step += 1)
            {
                int nodeX = (int)posX;
                int nodeY = (int)posY;
                int dropletIndex = nodeX + nodeY * width;

                // Offset within the current cell, needed to spread deposits.
                float cellOffsetX = posX - (float)nodeX;
                float cellOffsetY = posY - (float)nodeY;

                HeightAndGradient hg = sampleHeightAndGradient(heights, width, posX, posY);

                // Steer: keep some of the old direction, bend the rest toward
                // downhill. Pure gradient-following produces jittery paths that
                // never cut across terrain; pure inertia ignores the landscape.
                dirX = dirX * inertia - hg.gradientX * (1.0f - inertia);
                dirY = dirY * inertia - hg.gradientY * (1.0f - inertia);

                float length = sqrtf(dirX * dirX + dirY * dirY);

                if (length > 0.0001f)
                {
                    dirX /= length;
                    dirY /= length;
                }
                else
                {
                    break;   // sitting in a pit with nowhere to go
                }

                posX += dirX;
                posY += dirY;

                // Leaving the map, or stuck. Any sediment still carried is
                // simply lost, which is a deliberate simplification.
                if (posX < (float)margin || posX >= (float)(width - margin - 1) ||
                    posY < (float)margin || posY >= (float)(height - margin - 1))
                {
                    break;
                }

                float newHeight = sampleHeight(heights, width, posX, posY);
                float deltaHeight = newHeight - hg.height;

                // How much sediment this droplet can hold right now. Fast water
                // on a steep descent carries a lot; slow water on the flat
                // carries almost none, which is why deltas fill in.
                float capacity = maxf(-deltaHeight * speed * water * CAPACITY_FACTOR, MIN_CAPACITY);

                if (sediment > capacity || deltaHeight > 0.0f)
                {
                    // Deposit. Going uphill means the droplet has hit a wall,
                    // so it drops enough to fill the dip it just climbed --
                    // never more than it carries.
                    float amountToDeposit = 0.0f;

                    if (deltaHeight > 0.0f)
                    {
                        amountToDeposit = minf(deltaHeight, sediment);
                    }
                    else
                    {
                        amountToDeposit = (sediment - capacity) * depositSpeed;
                    }

                    sediment -= amountToDeposit;

                    // Deposition is bilinear onto the four cells under the
                    // droplet, not brushed. Silt settles where the water is.
                    heights[dropletIndex] +=
                        amountToDeposit * (1.0f - cellOffsetX) * (1.0f - cellOffsetY);
                    heights[dropletIndex + 1] +=
                        amountToDeposit * cellOffsetX * (1.0f - cellOffsetY);
                    heights[dropletIndex + width] +=
                        amountToDeposit * (1.0f - cellOffsetX) * cellOffsetY;
                    heights[dropletIndex + width + 1] +=
                        amountToDeposit * cellOffsetX * cellOffsetY;
                }
                else
                {
                    // Erode. Never remove more than the height difference just
                    // descended, or the droplet would dig a hole beneath
                    // itself and trap the next one.
                    float amountToErode = minf((capacity - sediment) * erodeSpeed, -deltaHeight);

                    for (int i = 0; i < brushCount; i += 1)
                    {
                        // No bounds check: the spawn margin guarantees this is
                        // inside the map.
                        int brushIndex = dropletIndex + brushOffset[i];
                        float weighted = amountToErode * brushWeight[i];

                        // Do not scrape below zero.
                        float removed = minf(weighted, heights[brushIndex]);

                        heights[brushIndex] -= removed;
                        sediment += removed;
                    }
                }

                // Falling converts height into speed. The max() guards against
                // a negative under the root when climbing.
                speed = sqrtf(maxf(0.0f, speed * speed + deltaHeight * -GRAVITY));
                water *= (1.0f - EVAPORATE_SPEED);
            }
        }

        free(brushOffset);
        free(brushWeight);
    }
}
