#include "terrain.cpp"

#include <cmath>
#include <cstdio>
#include <cstring>

static int checksRun = 0;
static int checksFailed = 0;

static void check(bool condition, const char* name)
{
    checksRun += 1;

    if (condition)
    {
        printf("  pass  %s\n", name);
    }
    else
    {
        printf("  FAIL  %s\n", name);
        checksFailed += 1;
    }
}

static void testNoise()
{
    printf("noise\n");

    bool inRange = true;
    bool varies = false;
    float first = valueNoise(0.0f, 0.0f, 32.0f, 1);

    for (int y = 0; y < 200; y += 1)
    {
        for (int x = 0; x < 200; x += 1)
        {
            float v = valueNoise((float)x * 1.7f, (float)y * 1.3f, 32.0f, 1);

            if (v < 0.0f || v > 1.0f || std::isnan(v))
            {
                inRange = false;
            }

            if (fabsf(v - first) > 0.01f)
            {
                varies = true;
            }
        }
    }

    check(inRange, "value noise stays within [0, 1]");
    check(varies, "value noise actually varies");

    check(valueNoise(12.3f, 45.6f, 32.0f, 7) == valueNoise(12.3f, 45.6f, 32.0f, 7),
          "value noise is deterministic");

    check(valueNoise(12.3f, 45.6f, 32.0f, 7) != valueNoise(12.3f, 45.6f, 32.0f, 8),
          "value noise differs by seed");

    float a = valueNoise(100.0f, 100.0f, 64.0f, 3);
    float b = valueNoise(100.5f, 100.0f, 64.0f, 3);
    check(fabsf(a - b) < 0.1f, "neighbouring samples are correlated");

    bool fbmInRange = true;

    for (int i = 0; i < 5000; i += 1)
    {
        float v = fbm((float)i * 0.7f, (float)i * 1.1f, 64.0f, 5, 6, 0.5f, 2.0f);

        if (v < 0.0f || v > 1.0f || std::isnan(v))
        {
            fbmInRange = false;
        }
    }

    check(fbmInRange, "fbm stays within [0, 1] across octaves");
}

static void testGenerate()
{
    printf("generate\n");

    const int w = 128;
    const int h = 128;

    float* a = generate(w, h, 1234, 64.0f, 5, 0.5f, 2.0f);
    float* b = generate(w, h, 1234, 64.0f, 5, 0.5f, 2.0f);
    float* c = generate(w, h, 9999, 64.0f, 5, 0.5f, 2.0f);

    check(a != 0 && b != 0 && c != 0, "generate returns a buffer");
    check(memcmp(a, b, (size_t)w * h * sizeof(float)) == 0,
          "same seed produces an identical heightmap");
    check(memcmp(a, c, (size_t)w * h * sizeof(float)) != 0,
          "different seed produces a different heightmap");

    bool finite = true;

    for (int i = 0; i < w * h; i += 1)
    {
        if (std::isnan(a[i]) || std::isinf(a[i]))
        {
            finite = false;
        }
    }

    check(finite, "heightmap contains no NaN or infinity");

    free(a);
    free(b);
    free(c);
}

static const int GUARD = 64;
static const float SENTINEL = -123456.0f;

static float* allocateGuarded(int w, int h)
{
    float* block = (float*)malloc((size_t)(w * h + 2 * GUARD) * sizeof(float));

    for (int i = 0; i < GUARD; i += 1)
    {
        block[i] = SENTINEL;
        block[GUARD + w * h + i] = SENTINEL;
    }

    return block;
}

static bool guardsIntact(const float* block, int w, int h)
{
    for (int i = 0; i < GUARD; i += 1)
    {
        if (block[i] != SENTINEL || block[GUARD + w * h + i] != SENTINEL)
        {
            return false;
        }
    }

    return true;
}

static void testErosion()
{
    printf("erosion\n");

    const int w = 128;
    const int h = 128;
    const int droplets = 20000;

    float* base = generate(w, h, 42, 48.0f, 5, 0.5f, 2.0f);

    float* runA = (float*)malloc((size_t)w * h * sizeof(float));
    float* runB = (float*)malloc((size_t)w * h * sizeof(float));
    memcpy(runA, base, (size_t)w * h * sizeof(float));
    memcpy(runB, base, (size_t)w * h * sizeof(float));

    erode(runA, w, h, droplets, 42, 0.05f, 0.3f, 0.3f, 3);
    erode(runB, w, h, droplets, 42, 0.05f, 0.3f, 0.3f, 3);

    check(memcmp(runA, runB, (size_t)w * h * sizeof(float)) == 0,
          "erosion is deterministic for a given seed");

    check(memcmp(runA, base, (size_t)w * h * sizeof(float)) != 0,
          "erosion actually modifies the terrain");

    bool finite = true;
    bool nonNegative = true;

    for (int i = 0; i < w * h; i += 1)
    {
        if (std::isnan(runA[i]) || std::isinf(runA[i]))
        {
            finite = false;
        }

        if (runA[i] < 0.0f)
        {
            nonNegative = false;
        }
    }

    check(finite, "eroded terrain contains no NaN or infinity");
    check(nonNegative, "erosion never digs below zero");

    double before = 0.0;
    double after = 0.0;

    for (int i = 0; i < w * h; i += 1)
    {
        before += base[i];
        after += runA[i];
    }

    double lost = before - after;
    double lostFraction = lost / before;

    printf("        mass: %.4f before, %.4f after, %.3f%% carried off-map\n",
           before, after, lostFraction * 100.0);

    check(lost >= -1e-3, "erosion does not create sediment from nothing");
    check(lostFraction < 0.05, "off-map sediment loss stays under 5%");

    free(runA);
    free(runB);

    for (int radius = 1; radius <= 8; radius += 1)
    {
        float* guarded = allocateGuarded(w, h);
        float* map = guarded + GUARD;
        memcpy(map, base, (size_t)w * h * sizeof(float));

        erode(map, w, h, 5000, radius * 13, 0.05f, 0.3f, 0.3f, radius);

        char name[80];
        snprintf(name, sizeof(name), "erosion stays in bounds at radius %d", radius);
        check(guardsIntact(guarded, w, h), name);

        free(guarded);
    }

    float* tiny = generate(8, 8, 1, 4.0f, 2, 0.5f, 2.0f);
    erode(tiny, 8, 8, 100, 1, 0.05f, 0.3f, 0.3f, 8);
    check(true, "erosion survives a map smaller than the brush");
    free(tiny);

    float* zero = (float*)malloc((size_t)w * h * sizeof(float));
    memcpy(zero, base, (size_t)w * h * sizeof(float));
    erode(zero, w, h, 0, 1, 0.05f, 0.3f, 0.3f, 3);
    check(memcmp(zero, base, (size_t)w * h * sizeof(float)) == 0,
          "zero droplets leaves the terrain untouched");
    free(zero);

    float* flat = (float*)malloc((size_t)w * h * sizeof(float));

    for (int i = 0; i < w * h; i += 1)
    {
        flat[i] = 0.5f;
    }

    erode(flat, w, h, 2000, 7, 0.05f, 0.3f, 0.3f, 3);

    bool flatFinite = true;

    for (int i = 0; i < w * h; i += 1)
    {
        if (std::isnan(flat[i]) || std::isinf(flat[i]))
        {
            flatFinite = false;
        }
    }

    check(flatFinite, "perfectly flat terrain does not produce NaN");
    free(flat);

    free(base);
}

int main()
{
    printf("\n");
    testNoise();
    testGenerate();
    testErosion();

    printf("\n%d checks, %d failed\n\n", checksRun, checksFailed);

    if (checksFailed > 0)
    {
        return 1;
    }

    return 0;
}
