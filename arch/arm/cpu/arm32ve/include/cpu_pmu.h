#include <vmm_stdio.h>
#include <vmm_macros.h>
#include <vmm_manager.h>
#include <vmm_scheduler.h>

#define MAX_PMU_COUNTER_VALUE       0xFFFFFFFF
#define MASK_OVERFLOW_CNT_0         0x00000001
#define MASK_OVERFLOW_CNT_1         0x00000002
#define MASK_OVERFLOW_CNT_2         0x00000004
#define MASK_OVERFLOW_CNT_3         0x00000008
#define MASK_OVERFLOW_CNT_CYCLE     0x80000000

//#define PMU_DEBUG
#ifdef PMU_DEBUG
#define VMM_PRINTF_PMUDEBUG(msg...)  vmm_printf(msg)
#else
#define VMM_PRINTF_PMUDEBUG(msg...)
#endif

typedef struct {
  u32 reg[5];       // 4 configurables and the cycle count
} vmm_cpu_perf;

//vmm_cpu_perf global_cpu_perf[4];

/** Inizialization pmu*/
int arch_pmu_init(void);

/** Start pmu for vcpu in parameter using the actual budget*/
bool start_pmu_for_vcpu(struct vmm_vcpu *vcpu);

/** Stop pmu for vcpu in parameter and store the new actual budget*/
bool stop_pmu_for_vcpu(struct vmm_vcpu *vcpu);
