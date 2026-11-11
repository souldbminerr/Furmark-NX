#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

void clkInfoInit(void);
void clkInfoExit(void);

u32 clkInfoGetGpuHz(void);
u32 clkInfoGetCpuHz(void);
u32 clkInfoGetMemHz(void);

void clkInfoGpuStep(int delta);
void clkInfoCpuStep(int delta);
void clkInfoMemStep(int delta);
u32 getGpuVoltMili();
u32 getCpuVoltMili();

#ifdef __cplusplus
}
#endif
