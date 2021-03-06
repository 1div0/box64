/*****************************************************************
 * File automatically generated by rebuild_wrappers.py (v1.2.0.09)
 *****************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "wrapper.h"
#include "emu/x64emu_private.h"
#include "emu/x87emu_private.h"
#include "regs.h"
#include "x64emu.h"

typedef union ui64_s {
    int64_t     i;
    uint64_t    u;
    uint32_t    d[2];
} ui64_t;

typedef struct _2uint_struct_s {
	uint32_t	a;
	uint32_t	b;
} _2uint_struct_t;

extern void* my__IO_2_1_stderr_;
extern void* my__IO_2_1_stdin_ ;
extern void* my__IO_2_1_stdout_;

static void* io_convert(void* v)
{
	if(!v)
		return v;
	if(v==my__IO_2_1_stderr_)
		return stderr;
	if(v==my__IO_2_1_stdin_)
		return stdin;
	if(v==my__IO_2_1_stdout_)
		return stdout;
	return v;
}

typedef struct my_GValue_s
{
  int         g_type;
  union {
    int        v_int;
    int64_t    v_int64;
    uint64_t   v_uint64;
    float      v_float;
    double     v_double;
    void*      v_pointer;
  } data[2];
} my_GValue_t;

static void alignGValue(my_GValue_t* v, void* value)
{
    v->g_type = *(int*)value;
    memcpy(v->data, value+4, 2*sizeof(double));
}
static void unalignGValue(void* value, my_GValue_t* v)
{
    *(int*)value = v->g_type;
    memcpy(value+4, v->data, 2*sizeof(double));
}

void* VulkanFromx86(void* src, void** save);
void VulkanTox86(void* src, void* save);

#define ST0val ST0.d

int of_convert(int);
typedef void (*vFE_t)(x64emu_t*);
typedef void (*vFv_t)(void);
typedef void (*vFp_t)(void*);
typedef int32_t (*iFE_t)(x64emu_t*);
typedef int32_t (*iFv_t)(void);
typedef int32_t (*iFi_t)(int32_t);
typedef int32_t (*iFu_t)(uint32_t);
typedef int32_t (*iFf_t)(float);
typedef int32_t (*iFd_t)(double);
typedef int32_t (*iFp_t)(void*);
typedef int64_t (*IFf_t)(float);
typedef int64_t (*IFd_t)(double);
typedef int64_t (*IFp_t)(void*);
typedef uint64_t (*UFV_t)(void*);
typedef float (*fFf_t)(float);
typedef double (*dFd_t)(double);
typedef double (*dFp_t)(void*);
typedef intptr_t (*lFp_t)(void*);
typedef uintptr_t (*LFv_t)(void);
typedef void* (*pFE_t)(x64emu_t*);
typedef void* (*pFp_t)(void*);
typedef void (*vFEp_t)(x64emu_t*, void*);
typedef void (*vFpp_t)(void*, void*);
typedef int32_t (*iFEp_t)(x64emu_t*, void*);
typedef int32_t (*iFip_t)(int32_t, void*);
typedef int32_t (*iFup_t)(uint32_t, void*);
typedef int32_t (*iFLL_t)(uintptr_t, uintptr_t);
typedef int32_t (*iFLp_t)(uintptr_t, void*);
typedef int32_t (*iFpi_t)(void*, int32_t);
typedef float (*fFif_t)(int32_t, float);
typedef float (*fFfi_t)(float, int32_t);
typedef float (*fFff_t)(float, float);
typedef float (*fFfp_t)(float, void*);
typedef double (*dFid_t)(int32_t, double);
typedef double (*dFdi_t)(double, int32_t);
typedef double (*dFdd_t)(double, double);
typedef double (*dFdp_t)(double, void*);
typedef void* (*pFEp_t)(x64emu_t*, void*);
typedef void* (*pFLL_t)(uintptr_t, uintptr_t);
typedef void* (*pFpV_t)(void*, void*);
typedef void (*vFEpu_t)(x64emu_t*, void*, uint32_t);
typedef void (*vFfpp_t)(float, void*, void*);
typedef void (*vFdpp_t)(double, void*, void*);
typedef int32_t (*iFEpp_t)(x64emu_t*, void*, void*);
typedef int32_t (*iFEpV_t)(x64emu_t*, void*, void*);
typedef int32_t (*iFpiu_t)(void*, int32_t, uint32_t);
typedef float (*fFfff_t)(float, float, float);
typedef float (*fFffp_t)(float, float, void*);
typedef double (*dFddd_t)(double, double, double);
typedef double (*dFddp_t)(double, double, void*);
typedef void* (*pFEpi_t)(x64emu_t*, void*, int32_t);
typedef void* (*pFEpp_t)(x64emu_t*, void*, void*);
typedef void (*vFEpup_t)(x64emu_t*, void*, uint32_t, void*);
typedef int32_t (*iFEupp_t)(x64emu_t*, uint32_t, void*, void*);
typedef int32_t (*iFEpip_t)(x64emu_t*, void*, int32_t, void*);
typedef int32_t (*iFEppp_t)(x64emu_t*, void*, void*, void*);
typedef int32_t (*iFuipp_t)(uint32_t, int32_t, void*, void*);
typedef void* (*pFEppi_t)(x64emu_t*, void*, void*, int32_t);
typedef void* (*pFEppp_t)(x64emu_t*, void*, void*, void*);
typedef int32_t (*iFEpppp_t)(x64emu_t*, void*, void*, void*, void*);
typedef int32_t (*iFipppi_t)(int32_t, void*, void*, void*, int32_t);
typedef int32_t (*iFEpippppp_t)(x64emu_t*, void*, int32_t, void*, void*, void*, void*, void*);

#if defined(HAVE_LD80BITS)
typedef long double (*DFD_t)(long double);
typedef long double (*DFDD_t)(long double, long double);
typedef long double (*DFDp_t)(long double, void*);
#endif

#if !defined(HAVE_LD80BITS)
typedef double (*KFK_t)(double);
typedef double (*KFKK_t)(double, double);
typedef double (*KFKp_t)(double, void*);
#endif

void vFE(x64emu_t *emu, uintptr_t fcn) { vFE_t fn = (vFE_t)fcn; fn(emu); }
void vFv(x64emu_t *emu, uintptr_t fcn) { vFv_t fn = (vFv_t)fcn; fn(); }
void vFp(x64emu_t *emu, uintptr_t fcn) { vFp_t fn = (vFp_t)fcn; fn((void*)R_RDI); }
void iFE(x64emu_t *emu, uintptr_t fcn) { iFE_t fn = (iFE_t)fcn; R_RAX=fn(emu); }
void iFv(x64emu_t *emu, uintptr_t fcn) { iFv_t fn = (iFv_t)fcn; R_RAX=fn(); }
void iFi(x64emu_t *emu, uintptr_t fcn) { iFi_t fn = (iFi_t)fcn; R_RAX=fn((int32_t)R_RDI); }
void iFu(x64emu_t *emu, uintptr_t fcn) { iFu_t fn = (iFu_t)fcn; R_RAX=fn((uint32_t)R_RDI); }
void iFf(x64emu_t *emu, uintptr_t fcn) { iFf_t fn = (iFf_t)fcn; R_RAX=fn(emu->xmm[0].f[0]); }
void iFd(x64emu_t *emu, uintptr_t fcn) { iFd_t fn = (iFd_t)fcn; R_RAX=fn(emu->xmm[0].d[0]); }
void iFp(x64emu_t *emu, uintptr_t fcn) { iFp_t fn = (iFp_t)fcn; R_RAX=fn((void*)R_RDI); }
void IFf(x64emu_t *emu, uintptr_t fcn) { IFf_t fn = (IFf_t)fcn; R_RAX=(uint64_t)fn(emu->xmm[0].f[0]); }
void IFd(x64emu_t *emu, uintptr_t fcn) { IFd_t fn = (IFd_t)fcn; R_RAX=(uint64_t)fn(emu->xmm[0].d[0]); }
void IFp(x64emu_t *emu, uintptr_t fcn) { IFp_t fn = (IFp_t)fcn; R_RAX=(uint64_t)fn((void*)R_RDI); }
void UFV(x64emu_t *emu, uintptr_t fcn) { UFV_t fn = (UFV_t)fcn; R_RAX=fn((void*)(R_RSP + 8)); }
void fFf(x64emu_t *emu, uintptr_t fcn) { fFf_t fn = (fFf_t)fcn; emu->xmm[0].f[0]=fn(emu->xmm[0].f[0]); }
void dFd(x64emu_t *emu, uintptr_t fcn) { dFd_t fn = (dFd_t)fcn; emu->xmm[0].d[0]=fn(emu->xmm[0].d[0]); }
void dFp(x64emu_t *emu, uintptr_t fcn) { dFp_t fn = (dFp_t)fcn; emu->xmm[0].d[0]=fn((void*)R_RDI); }
void lFp(x64emu_t *emu, uintptr_t fcn) { lFp_t fn = (lFp_t)fcn; R_RAX=(intptr_t)fn((void*)R_RDI); }
void LFv(x64emu_t *emu, uintptr_t fcn) { LFv_t fn = (LFv_t)fcn; R_RAX=(uintptr_t)fn(); }
void pFE(x64emu_t *emu, uintptr_t fcn) { pFE_t fn = (pFE_t)fcn; R_RAX=(uintptr_t)fn(emu); }
void pFp(x64emu_t *emu, uintptr_t fcn) { pFp_t fn = (pFp_t)fcn; R_RAX=(uintptr_t)fn((void*)R_RDI); }
void vFEp(x64emu_t *emu, uintptr_t fcn) { vFEp_t fn = (vFEp_t)fcn; fn(emu, (void*)R_RDI); }
void vFpp(x64emu_t *emu, uintptr_t fcn) { vFpp_t fn = (vFpp_t)fcn; fn((void*)R_RDI, (void*)R_RSI); }
void iFEp(x64emu_t *emu, uintptr_t fcn) { iFEp_t fn = (iFEp_t)fcn; R_RAX=fn(emu, (void*)R_RDI); }
void iFip(x64emu_t *emu, uintptr_t fcn) { iFip_t fn = (iFip_t)fcn; R_RAX=fn((int32_t)R_RDI, (void*)R_RSI); }
void iFup(x64emu_t *emu, uintptr_t fcn) { iFup_t fn = (iFup_t)fcn; R_RAX=fn((uint32_t)R_RDI, (void*)R_RSI); }
void iFLL(x64emu_t *emu, uintptr_t fcn) { iFLL_t fn = (iFLL_t)fcn; R_RAX=fn((uintptr_t)R_RDI, (uintptr_t)R_RSI); }
void iFLp(x64emu_t *emu, uintptr_t fcn) { iFLp_t fn = (iFLp_t)fcn; R_RAX=fn((uintptr_t)R_RDI, (void*)R_RSI); }
void iFpi(x64emu_t *emu, uintptr_t fcn) { iFpi_t fn = (iFpi_t)fcn; R_RAX=fn((void*)R_RDI, (int32_t)R_RSI); }
void fFif(x64emu_t *emu, uintptr_t fcn) { fFif_t fn = (fFif_t)fcn; emu->xmm[0].f[0]=fn((int32_t)R_RDI, emu->xmm[0].f[0]); }
void fFfi(x64emu_t *emu, uintptr_t fcn) { fFfi_t fn = (fFfi_t)fcn; emu->xmm[0].f[0]=fn(emu->xmm[0].f[0], (int32_t)R_RDI); }
void fFff(x64emu_t *emu, uintptr_t fcn) { fFff_t fn = (fFff_t)fcn; emu->xmm[0].f[0]=fn(emu->xmm[0].f[0], emu->xmm[1].f[0]); }
void fFfp(x64emu_t *emu, uintptr_t fcn) { fFfp_t fn = (fFfp_t)fcn; emu->xmm[0].f[0]=fn(emu->xmm[0].f[0], (void*)R_RDI); }
void dFid(x64emu_t *emu, uintptr_t fcn) { dFid_t fn = (dFid_t)fcn; emu->xmm[0].d[0]=fn((int32_t)R_RDI, emu->xmm[0].d[0]); }
void dFdi(x64emu_t *emu, uintptr_t fcn) { dFdi_t fn = (dFdi_t)fcn; emu->xmm[0].d[0]=fn(emu->xmm[0].d[0], (int32_t)R_RDI); }
void dFdd(x64emu_t *emu, uintptr_t fcn) { dFdd_t fn = (dFdd_t)fcn; emu->xmm[0].d[0]=fn(emu->xmm[0].d[0], emu->xmm[1].d[0]); }
void dFdp(x64emu_t *emu, uintptr_t fcn) { dFdp_t fn = (dFdp_t)fcn; emu->xmm[0].d[0]=fn(emu->xmm[0].d[0], (void*)R_RDI); }
void pFEp(x64emu_t *emu, uintptr_t fcn) { pFEp_t fn = (pFEp_t)fcn; R_RAX=(uintptr_t)fn(emu, (void*)R_RDI); }
void pFLL(x64emu_t *emu, uintptr_t fcn) { pFLL_t fn = (pFLL_t)fcn; R_RAX=(uintptr_t)fn((uintptr_t)R_RDI, (uintptr_t)R_RSI); }
void pFpV(x64emu_t *emu, uintptr_t fcn) { pFpV_t fn = (pFpV_t)fcn; R_RAX=(uintptr_t)fn((void*)R_RDI, (void*)(R_RSP + 8)); }
void vFEpu(x64emu_t *emu, uintptr_t fcn) { vFEpu_t fn = (vFEpu_t)fcn; fn(emu, (void*)R_RDI, (uint32_t)R_RSI); }
void vFfpp(x64emu_t *emu, uintptr_t fcn) { vFfpp_t fn = (vFfpp_t)fcn; fn(emu->xmm[0].f[0], (void*)R_RDI, (void*)R_RSI); }
void vFdpp(x64emu_t *emu, uintptr_t fcn) { vFdpp_t fn = (vFdpp_t)fcn; fn(emu->xmm[0].d[0], (void*)R_RDI, (void*)R_RSI); }
void iFEpp(x64emu_t *emu, uintptr_t fcn) { iFEpp_t fn = (iFEpp_t)fcn; R_RAX=fn(emu, (void*)R_RDI, (void*)R_RSI); }
void iFEpV(x64emu_t *emu, uintptr_t fcn) { iFEpV_t fn = (iFEpV_t)fcn; R_RAX=fn(emu, (void*)R_RDI, (void*)(R_RSP + 8)); }
void iFpiu(x64emu_t *emu, uintptr_t fcn) { iFpiu_t fn = (iFpiu_t)fcn; R_RAX=fn((void*)R_RDI, (int32_t)R_RSI, (uint32_t)R_RDX); }
void fFfff(x64emu_t *emu, uintptr_t fcn) { fFfff_t fn = (fFfff_t)fcn; emu->xmm[0].f[0]=fn(emu->xmm[0].f[0], emu->xmm[1].f[0], emu->xmm[2].f[0]); }
void fFffp(x64emu_t *emu, uintptr_t fcn) { fFffp_t fn = (fFffp_t)fcn; emu->xmm[0].f[0]=fn(emu->xmm[0].f[0], emu->xmm[1].f[0], (void*)R_RDI); }
void dFddd(x64emu_t *emu, uintptr_t fcn) { dFddd_t fn = (dFddd_t)fcn; emu->xmm[0].d[0]=fn(emu->xmm[0].d[0], emu->xmm[1].d[0], emu->xmm[2].d[0]); }
void dFddp(x64emu_t *emu, uintptr_t fcn) { dFddp_t fn = (dFddp_t)fcn; emu->xmm[0].d[0]=fn(emu->xmm[0].d[0], emu->xmm[1].d[0], (void*)R_RDI); }
void pFEpi(x64emu_t *emu, uintptr_t fcn) { pFEpi_t fn = (pFEpi_t)fcn; R_RAX=(uintptr_t)fn(emu, (void*)R_RDI, (int32_t)R_RSI); }
void pFEpp(x64emu_t *emu, uintptr_t fcn) { pFEpp_t fn = (pFEpp_t)fcn; R_RAX=(uintptr_t)fn(emu, (void*)R_RDI, (void*)R_RSI); }
void vFEpup(x64emu_t *emu, uintptr_t fcn) { vFEpup_t fn = (vFEpup_t)fcn; fn(emu, (void*)R_RDI, (uint32_t)R_RSI, (void*)R_RDX); }
void iFEupp(x64emu_t *emu, uintptr_t fcn) { iFEupp_t fn = (iFEupp_t)fcn; R_RAX=fn(emu, (uint32_t)R_RDI, (void*)R_RSI, (void*)R_RDX); }
void iFEpip(x64emu_t *emu, uintptr_t fcn) { iFEpip_t fn = (iFEpip_t)fcn; R_RAX=fn(emu, (void*)R_RDI, (int32_t)R_RSI, (void*)R_RDX); }
void iFEppp(x64emu_t *emu, uintptr_t fcn) { iFEppp_t fn = (iFEppp_t)fcn; R_RAX=fn(emu, (void*)R_RDI, (void*)R_RSI, (void*)R_RDX); }
void iFuipp(x64emu_t *emu, uintptr_t fcn) { iFuipp_t fn = (iFuipp_t)fcn; R_RAX=fn((uint32_t)R_RDI, (int32_t)R_RSI, (void*)R_RDX, (void*)R_RCX); }
void pFEppi(x64emu_t *emu, uintptr_t fcn) { pFEppi_t fn = (pFEppi_t)fcn; R_RAX=(uintptr_t)fn(emu, (void*)R_RDI, (void*)R_RSI, (int32_t)R_RDX); }
void pFEppp(x64emu_t *emu, uintptr_t fcn) { pFEppp_t fn = (pFEppp_t)fcn; R_RAX=(uintptr_t)fn(emu, (void*)R_RDI, (void*)R_RSI, (void*)R_RDX); }
void iFEpppp(x64emu_t *emu, uintptr_t fcn) { iFEpppp_t fn = (iFEpppp_t)fcn; R_RAX=fn(emu, (void*)R_RDI, (void*)R_RSI, (void*)R_RDX, (void*)R_RCX); }
void iFipppi(x64emu_t *emu, uintptr_t fcn) { iFipppi_t fn = (iFipppi_t)fcn; R_RAX=fn((int32_t)R_RDI, (void*)R_RSI, (void*)R_RDX, (void*)R_RCX, (int32_t)R_R8); }
void iFEpippppp(x64emu_t *emu, uintptr_t fcn) { iFEpippppp_t fn = (iFEpippppp_t)fcn; R_RAX=fn(emu, (void*)R_RDI, (int32_t)R_RSI, (void*)R_RDX, (void*)R_RCX, (void*)R_R8, (void*)R_R9, *(void**)(R_RSP + 8)); }

#if defined(HAVE_LD80BITS)
void DFD(x64emu_t *emu, uintptr_t fcn) { DFD_t fn = (DFD_t)fcn; long double ld=fn(*(long double*)(R_RSP + 8)); fpu_do_push(emu); ST0val = ld; }
void DFDD(x64emu_t *emu, uintptr_t fcn) { DFDD_t fn = (DFDD_t)fcn; long double ld=fn(*(long double*)(R_RSP + 8), *(long double*)(R_RSP + 24)); fpu_do_push(emu); ST0val = ld; }
void DFDp(x64emu_t *emu, uintptr_t fcn) { DFDp_t fn = (DFDp_t)fcn; long double ld=fn(*(long double*)(R_RSP + 8), (void*)R_RDI); fpu_do_push(emu); ST0val = ld; }
#endif

#if !defined(HAVE_LD80BITS)
void KFK(x64emu_t *emu, uintptr_t fcn) { KFK_t fn = (KFK_t)fcn; double db=fn(FromLD((void*)(R_RSP + 8))); fpu_do_push(emu); ST0val = db; }
void KFKK(x64emu_t *emu, uintptr_t fcn) { KFKK_t fn = (KFKK_t)fcn; double db=fn(FromLD((void*)(R_RSP + 8)), FromLD((void*)(R_RSP + 24))); fpu_do_push(emu); ST0val = db; }
void KFKp(x64emu_t *emu, uintptr_t fcn) { KFKp_t fn = (KFKp_t)fcn; double db=fn(FromLD((void*)(R_RSP + 8)), (void*)R_RDI); fpu_do_push(emu); ST0val = db; }
#endif

void iFEv(x64emu_t *emu, uintptr_t fcn) { iFE_t fn = (iFE_t)fcn; R_RAX=fn(emu); }
void pFEv(x64emu_t *emu, uintptr_t fcn) { pFE_t fn = (pFE_t)fcn; R_RAX=(uintptr_t)fn(emu); }
