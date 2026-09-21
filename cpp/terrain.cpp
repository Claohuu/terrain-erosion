#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#else
  #define EMSCRIPTEN_KEEPALIVE
#endif
#include <cstdlib>
#include <cmath>

float hash(int x, int y, int seed)
{
    int h = x * 374761393 + y * 668265263 + seed * 362437;
    h = (h ^ (h >> 13)) * 1274126177;
    h = h ^ (h >> 16);
    return (float)(h & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

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

float valueNoise(float x, float y, float gridSize, int seed)
{
    float gx = x / gridSize;
    float gy = y / gridSize;

    int x0 = (int)floorf(gx);
    int y0 = (int)floorf(gy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx = gx - (float)x0;
    float fy = gy - (float)y0;

    float sx = smoothCurve(fx);
    float sy = smoothCurve(fy);

    float c00 = hash(x0, y0, seed);
    float c10 = hash(x1, y0, seed);
    float c01 = hash(x0, y1, seed);
    float c11 = hash(x1, y1, seed);

    float top = lerp(c00, c10, sx);
    float bottom = lerp(c01, c11, sx);

    return lerp(top, bottom, sy);
}

float fbm(float x, float y, float gridSize, int seed,
          int octaves, float persistence, float lacunarity)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i += 1)
    {

        total += valueNoise(x * frequency, y * frequency, gridSize, seed + i * 7919) * amplitude;

        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

const int   MAX_LIFETIME      = 30;
const float CAPACITY_FACTOR   = 4.0f;
const float MIN_CAPACITY      = 0.01f;
const float EVAPORATE_SPEED   = 0.02f;
const float GRAVITY           = 4.0f;
const float INITIAL_SPEED     = 1.0f;
const float INITIAL_WATER     = 1.0f;

struct Gradient
{
    float x;
    float y;
};

Gradient sampleGradient(const float* heights, int width, float posX, float posY)
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

    Gradient result;

    result.x = (ne - nw) * (1.0f - v) + (se - sw) * v;
    result.y = (sw - nw) * (1.0f - u) + (se - ne) * u;

    return result;
}

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

    EMSCRIPTEN_KEEPALIVE
    void erode(float* heights, int width, int height,
               int numDroplets, int seed,
               float inertia, float erodeSpeed, float depositSpeed, int radius)
    {
        if (radius < 1)
        {
            radius = 1;
        }

        int maxBrush = (2 * radius + 1) * (2 * radius + 1);

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

        for (int i = 0; i < brushCount; i += 1)
        {
            brushWeight[i] /= weightSum;
        }

        int margin = radius + 1;

        if (width <= 2 * margin + 2 || height <= 2 * margin + 2)
        {
            free(brushOffset);
            free(brushWeight);
            return;
        }

        int spawnW = width - 2 * margin;
        int spawnH = height - 2 * margin;

        float steerWeight = 1.0f - inertia;

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

            float currentHeight = sampleHeight(heights, width, posX, posY);

            for (int step = 0; step < MAX_LIFETIME; step += 1)
            {
                int nodeX = (int)posX;
                int nodeY = (int)posY;
                int dropletIndex = nodeX + nodeY * width;

                float cellOffsetX = posX - (float)nodeX;
                float cellOffsetY = posY - (float)nodeY;

                Gradient gradient = sampleGradient(heights, width, posX, posY);

                dirX = dirX * inertia - gradient.x * steerWeight;
                dirY = dirY * inertia - gradient.y * steerWeight;

                float length = sqrtf(dirX * dirX + dirY * dirY);

                if (length > 0.0001f)
                {

                    float inverseLength = 1.0f / length;
                    dirX *= inverseLength;
                    dirY *= inverseLength;
                }
                else
                {
                    break;
                }

                posX += dirX;
                posY += dirY;

                if (posX < (float)margin || posX >= (float)(width - margin - 1) ||
                    posY < (float)margin || posY >= (float)(height - margin - 1))
                {
                    break;
                }

                float newHeight = sampleHeight(heights, width, posX, posY);
                float deltaHeight = newHeight - currentHeight;

                float capacity = maxf(-deltaHeight * speed * water * CAPACITY_FACTOR, MIN_CAPACITY);

                if (sediment > capacity || deltaHeight > 0.0f)
                {

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

                    float amountToErode = minf((capacity - sediment) * erodeSpeed, -deltaHeight);

                    for (int i = 0; i < brushCount; i += 1)
                    {

                        int brushIndex = dropletIndex + brushOffset[i];
                        float weighted = amountToErode * brushWeight[i];

                        float removed = minf(weighted, heights[brushIndex]);

                        heights[brushIndex] -= removed;
                        sediment += removed;
                    }
                }

                speed = sqrtf(maxf(0.0f, speed * speed + deltaHeight * -GRAVITY));
                water *= (1.0f - EVAPORATE_SPEED);
                currentHeight = newHeight;
            }
        }

        free(brushOffset);
        free(brushWeight);
    }
}
