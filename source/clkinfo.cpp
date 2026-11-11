#include "clkinfo.h"

#define CLK_MAX_RATES 128

typedef struct {
    u32 rates[CLK_MAX_RATES];
    s32 count;
    s32 index;
} ClkTable;

static bool g_initialized = false;
static ClkTable g_gpu = {};
static ClkTable g_cpu = {};
static ClkTable g_mem = {};

static PcvModuleId getModuleId(PcvModule module) {
    PcvModuleId id = (PcvModuleId)0;
    Result rc = pcvGetModuleId(&id, module);
    if (R_FAILED(rc)) {
        switch (module) {
            case PcvModule_GPU:     id = PcvModuleId_GPU; break;
            case PcvModule_CpuBus:  id = PcvModuleId_CpuBus; break;
            case PcvModule_EMC:     id = PcvModuleId_EMC; break;
            default:                id = (PcvModuleId)0; break;
        }
    }
    return id;
}

static bool loadTable(ClkTable* t, PcvModule module) {
    t->count = 0;
    t->index = 0;
    if (!t) return false;

    PcvModuleId id = getModuleId(module);
    if ((u32)id == 0) return false;

    ClkrstSession session = {};
    Result rc = clkrstOpenSession(&session, id, 3);
    if (R_FAILED(rc)) return false;

    PcvClockRatesListType type = PcvClockRatesListType_Invalid;
    s32 outCount = 0;
    rc = clkrstGetPossibleClockRates(&session, t->rates, CLK_MAX_RATES, &type, &outCount);
    clkrstCloseSession(&session);

    if (R_FAILED(rc) || outCount <= 0) return false;

    s32 count = outCount;
    if (count > CLK_MAX_RATES) count = CLK_MAX_RATES;
    if (count < 0) count = 0;

    if (type == PcvClockRatesListType_Range && count == 2 && t->rates[1] > t->rates[0]) {
        const u32 minHz = t->rates[0];
        const u32 maxHz = t->rates[1];
        const u32 stepHz = 100000000u;

        s32 n = 0;
        u64 hz = minHz;
        while (hz < (u64)maxHz && n < CLK_MAX_RATES - 1) {
            t->rates[n++] = (u32)hz;
            hz += stepHz;
        }
        if (n < CLK_MAX_RATES) {
            t->rates[n++] = maxHz;
        } else {
            t->rates[CLK_MAX_RATES - 1] = maxHz;
            n = CLK_MAX_RATES;
        }
        count = n;
    }

    t->count = count;
    t->index = 0;
    return true;
}

static u32 setHz(PcvModule module, u32 hz) {
    PcvModuleId id = getModuleId(module);
    if ((u32)id == 0) return 0;

    ClkrstSession session = {};
    Result rc = clkrstOpenSession(&session, id, 3);
    if (R_FAILED(rc)) return 0;

    rc = clkrstSetClockRate(&session, hz);
    clkrstCloseSession(&session);
    return R_SUCCEEDED(rc) ? hz : 0;
}

static u32 getHz(PcvModule module) {
    PcvModuleId id = getModuleId(module);
    if ((u32)id == 0) return 0;

    ClkrstSession session = {};
    Result rc = clkrstOpenSession(&session, id, 3);
    if (R_FAILED(rc)) return 0;

    u32 hz = 0;
    rc = clkrstGetClockRate(&session, &hz);
    clkrstCloseSession(&session);
    if (R_FAILED(rc)) return 0;
    return hz;
}

void clkInfoInit(void) {
    if (g_initialized) return;

    Result rc = clkrstInitialize();
    if (R_FAILED(rc)) return;
    g_initialized = true;

    loadTable(&g_gpu, PcvModule_GPU);
    loadTable(&g_cpu, PcvModule_CpuBus);
    loadTable(&g_mem, PcvModule_EMC);

    auto syncIndex = [](ClkTable* t, PcvModule module) {
        if (!t || t->count <= 0) return;
        u32 actual = getHz(module);
        if (actual == 0) {
            t->index = 0;
            return;
        }
        s32 best = 0;
        u32 bestDiff = (actual > t->rates[0]) ? (actual - t->rates[0]) : (t->rates[0] - actual);
        for (s32 i = 1; i < t->count; i++) {
            u32 diff = (actual > t->rates[i]) ? (actual - t->rates[i]) : (t->rates[i] - actual);
            if (diff < bestDiff) {
                bestDiff = diff;
                best = i;
            }
        }
        t->index = best;
    };
    syncIndex(&g_gpu, PcvModule_GPU);
    syncIndex(&g_cpu, PcvModule_CpuBus);
    syncIndex(&g_mem, PcvModule_EMC);

    // Start GPU at max table entry.
    if (g_gpu.count > 0) {
        g_gpu.index = g_gpu.count - 1;
        setHz(PcvModule_GPU, g_gpu.rates[g_gpu.index]);
    }
}

void clkInfoExit(void) {
    if (g_initialized) {
        clkrstExit();
        g_initialized = false;
    }
    g_gpu.count = g_cpu.count = g_mem.count = 0;
    g_gpu.index = g_cpu.index = g_mem.index = 0;
}

u32 clkInfoGetGpuHz(void) { return getHz(PcvModule_GPU); }
u32 clkInfoGetCpuHz(void) { return getHz(PcvModule_CpuBus); }
u32 clkInfoGetMemHz(void) { return getHz(PcvModule_EMC); }

static void stepTable(ClkTable* t, PcvModule module, int delta) {
    if (!t || t->count <= 0) return;
    if (t->index < 0) t->index = 0;
    if (t->index >= t->count) t->index = t->count - 1;

    s32 idx = t->index + delta;
    if (idx < 0) idx = 0;
    if (idx >= t->count) idx = t->count - 1;
    if (idx == t->index) return;

    u32 target = t->rates[idx];
    if (setHz(module, target) == 0) return;

    t->index = idx;
}

void clkInfoGpuStep(int delta) { stepTable(&g_gpu, PcvModule_GPU, delta); }
void clkInfoCpuStep(int delta) { stepTable(&g_cpu, PcvModule_CpuBus, delta); }
void clkInfoMemStep(int delta) { stepTable(&g_mem, PcvModule_EMC, delta); }
