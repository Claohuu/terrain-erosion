// Native scaling benchmark.
//
// The in-browser benchmark answers "how fast is it right now". This answers
// the more useful question: how does throughput behave as the work grows, and
// where does it stop being linear.
//
// Build and run with ./run_bench.sh

#include "terrain.cpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

typedef std::chrono::steady_clock Clock;

// Median of several runs. A single timing moves by tens of percent with CPU
// frequency scaling, so it is not a measurement.
static double medianErodeMs(int width, int height, int droplets, int radius, int runs)
{
    std::vector<double> samples;

    float* base = generate(width, height, 1337, 140.0f, 6, 0.5f, 2.0f);
    size_t bytes = (size_t)width * (size_t)height * sizeof(float);
    float* scratch = (float*)malloc(bytes);

    for (int i = 0; i < runs; i += 1)
    {
        // Erode a fresh copy every time -- eroding already-eroded terrain is a
        // different workload, because flatter ground means fewer brush writes.
        memcpy(scratch, base, bytes);

        Clock::time_point start = Clock::now();
        erode(scratch, width, height, droplets, 1337, 0.05f, 0.3f, 0.3f, radius);
        Clock::time_point end = Clock::now();

        samples.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(samples.begin(), samples.end());

    free(scratch);
    free(base);

    return samples[samples.size() / 2];
}

// Runs the simulation continuously before any measurement is taken.
//
// Without this, early numbers are recorded while the CPU is still on its boost
// clock and later ones after it has settled to its sustained frequency. That
// produced a 2x spread on identical work purely from measurement order.
// Median-of-N does not help, because every sample within a group sits at the
// same point on the thermal curve.
static void warmUp(double seconds)
{
    float* base = generate(512, 512, 1, 140.0f, 6, 0.5f, 2.0f);
    size_t bytes = 512 * 512 * sizeof(float);
    float* scratch = (float*)malloc(bytes);

    Clock::time_point start = Clock::now();

    while (std::chrono::duration<double>(Clock::now() - start).count() < seconds)
    {
        memcpy(scratch, base, bytes);
        erode(scratch, 512, 512, 50000, 1, 0.05f, 0.3f, 0.3f, 3);
    }

    free(scratch);
    free(base);
}

// One fixed configuration, measured at the start and again at the end. If the
// two disagree, the machine drifted and every number between them is suspect.
static double control(int runs)
{
    return medianErodeMs(512, 512, 100000, 3, runs);
}

int main()
{
    const int runs = 5;

    printf("\n");
    printf("warming up to steady state...\n");
    warmUp(8.0);

    double controlStart = control(runs);
    printf("control (512x512, 100k droplets, r3): %.1f ms\n", controlStart);

    printf("\n");
    printf("Resolution sweep -- 100,000 droplets, brush radius 3\n");
    printf("%-12s %10s %14s %14s %12s\n",
           "size", "heightmap", "median ms", "droplets/sec", "vs 256");

    int sizes[] = { 256, 512, 1024, 2048 };
    double baselineRate = 0.0;

    for (int i = 0; i < 4; i += 1)
    {
        int n = sizes[i];
        double ms = medianErodeMs(n, n, 100000, 3, runs);
        double rate = 100000.0 / (ms / 1000.0);

        if (i == 0)
        {
            baselineRate = rate;
        }

        char sizeLabel[32];
        snprintf(sizeLabel, sizeof(sizeLabel), "%dx%d", n, n);

        char memLabel[32];
        double mb = (double)n * (double)n * sizeof(float) / (1024.0 * 1024.0);
        snprintf(memLabel, sizeof(memLabel), "%.1f MB", mb);

        printf("%-12s %10s %14.1f %14.0f %11.0f%%\n",
               sizeLabel, memLabel, ms, rate, (rate / baselineRate) * 100.0);
    }

    printf("\n");
    printf("Droplet sweep -- 512x512, brush radius 3\n");
    printf("%-12s %14s %14s %14s\n", "droplets", "median ms", "droplets/sec", "ms/10k");

    int counts[] = { 25000, 50000, 100000, 200000, 400000 };

    for (int i = 0; i < 5; i += 1)
    {
        int d = counts[i];
        double ms = medianErodeMs(512, 512, d, 3, runs);
        double rate = (double)d / (ms / 1000.0);
        double per10k = ms / ((double)d / 10000.0);

        printf("%-12d %14.1f %14.0f %14.2f\n", d, ms, rate, per10k);
    }

    printf("\n");
    printf("Brush radius sweep -- 512x512, 100,000 droplets\n");
    printf("%-12s %10s %14s %14s\n", "radius", "cells", "median ms", "droplets/sec");

    for (int radius = 1; radius <= 6; radius += 1)
    {
        // Cells inside the circular brush, which is what the inner loop costs.
        int cells = 0;

        for (int dy = -radius; dy <= radius; dy += 1)
        {
            for (int dx = -radius; dx <= radius; dx += 1)
            {
                if (sqrtf((float)(dx * dx + dy * dy)) <= (float)radius)
                {
                    cells += 1;
                }
            }
        }

        double ms = medianErodeMs(512, 512, 100000, radius, runs);
        double rate = 100000.0 / (ms / 1000.0);

        printf("%-12d %10d %14.1f %14.0f\n", radius, cells, ms, rate);
    }

    double controlEnd = control(runs);
    double drift = (controlEnd - controlStart) / controlStart * 100.0;

    printf("\n");
    printf("control re-measured: %.1f ms (drift %+.1f%%)\n", controlEnd, drift);

    if (drift > 5.0 || drift < -5.0)
    {
        printf("WARNING: the machine drifted more than 5%% during this run.\n");
        printf("         Treat the numbers above as unreliable.\n");
    }
    else
    {
        printf("Drift under 5%%, so the numbers above are comparable.\n");
    }

    printf("\n");
    return 0;
}
