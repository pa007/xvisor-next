#include <vmm_error.h>
#include <vmm_types.h>
#include <vmm_host_irq.h>
#include <vmm_smp.h>
#include <cpu_pmu.h>


static u32 pmu_irq=0;

inline u32 _read_cpu_counter(int r) {
  // Read PMXEVCNTR #r
  // This is done by first writing the counter number to PMSELR and then reading PMXEVCNTR
  u32 ret;
  asm volatile ("MCR p15, 0, %0, c9, c12, 5\t\n" :: "r"(r));      // Select event counter in PMSELR
  asm volatile ("MRC p15, 0, %0, c9, c13, 2\t\n" : "=r"(ret));    // Read from PMXEVCNTR
  return ret;
}

inline void _setup_cpu_counter(u32 r, u32 event, u32 cnt) {
  // Write PMXEVTYPER #r
  // This is done by first writing the counter number to PMSELR and then writing PMXEVTYPER
  asm volatile ("MCR p15, 0, %0, c9, c12, 5\t\n" :: "r"(r));        // Select event counter in PMSELR
  asm volatile ("MCR p15, 0, %0, c9, c13, 2\t\n" :: "r"(cnt));      // Set PMXEVCNTR with the value cnt
  asm volatile ("MCR p15, 0, %0, c9, c13, 1\t\n" :: "r"(event));    // Set the event number in PMXEVTYPER
}


static vmm_irq_return_t pmu_handler(int irq_no, void *dev)
{
    u32 pmovsr, pmcr;
    struct vmm_vcpu *vcpu;
    //u32 id, hcpu, state;
    irq_flags_t flags;

    // read overflows (PMOVSR):
    asm volatile ("MRC p15, 0, %0, c9, c12, 3\t\n" :"=r"(pmovsr));

    if (pmovsr && MASK_OVERFLOW_CNT_0)
    {
        // clear overflows (PMOVSR):
        asm volatile ("MCR p15, 0, %0, c9, c12, 3\t\n" :: "r"(0x8000000f));

        pmcr = (0x2 | 0x4); //stop pmu and reset all counters and cycle counters

        // program the performance-counter control-register (PMCR):
        asm volatile ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(pmcr));

        vcpu = vmm_scheduler_current_vcpu();

        /* Lock VCPU WFI */
        vmm_spin_lock_irqsave_lite(&vcpu->irqs.wfi.lock, flags);
        /* Set wait for irq state */
        vcpu->irqs.wfi.state = FALSE;
        /* Unlock VCPU WFI */
        vmm_spin_unlock_irqrestore_lite(&vcpu->irqs.wfi.lock, flags);

        vmm_write_lock_lite(&vcpu->sched_lock);
        vcpu->mrecharge = TRUE;
        vcpu->mactual_budget = 0;
        vmm_write_unlock_lite(&vcpu->sched_lock);

        vmm_manager_vcpu_pause(vcpu);

        // read overflows (PMOVSR):
        asm volatile ("MRC p15, 0, %0, c9, c12, 3\t\n" :"=r"(pmovsr));

        VMM_PRINTF_PMUDEBUG("%s: handler pmu for vcpu %d in core %d pmovs=0x%X\n",__func__,vcpu->id,vmm_smp_processor_id(),pmovsr);

    }


    return VMM_IRQ_HANDLED;
}

bool start_pmu_for_vcpu(struct vmm_vcpu *vcpu)
{
    // Disable all counters for configuration (PCMCNTENCLR)
    asm volatile ("MCR p15, 0, %0, c9, c12, 2\t\n" :: "r"(0x8000000f));

    // disable counter overflow interrupts
    asm volatile ("MCR p15, 0, %0, c9, c14, 2\n\t" :: "r"(0x8000000f));

    // Reset PCMCNTENCLR register
    asm volatile ("MCR p15, 0, %0, c9, c12, 2\t\n" :: "r"(0x00000000));

    u32 pmcr =  (0x2 | 0x4);    // reset all other counters and reset cycle counter

    // program the performance-counter control-register (PMCR):
    asm volatile ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(pmcr));

    _setup_cpu_counter(0, 0x13, (MAX_PMU_COUNTER_VALUE - vcpu->mactual_budget)); //0x13 DMACC DATA MEMORY ACCESS
    //VMM_PRINTF_PMUDEBUG("%s: start pmu for vcpu %d in core %d actual_budget=%u\n",__func__,vcpu->id,vmm_smp_processor_id(),(vcpu->actual_budget));
    pmcr  = (0x1 | 0x8);// enable counters and enable "by 64" divider for CCNT.

    // program the performance-counter control-register (PMCR):
    asm volatile ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(pmcr));

    // clear overflows (PMOVSR):
    asm volatile ("MCR p15, 0, %0, c9, c12, 3\t\n" :: "r"(0x8000000f));

    // Enable intterrup overflow COUNTER 0
    asm volatile ("MCR p15, 0, %0, c9, c14, 1\t\n" :: "r"(0x00000001));

    // Enable all counters (PMCNTENSET):
    asm volatile ("MCR p15, 0, %0, c9, c12, 1\t\n" :: "r"(0x8000000f));

    //VMM_PRINTF_PMUDEBUG("%s: starting pmu for vcpu %d in core %d\n",__func__,vcpu->id,vmm_smp_processor_id());

    return VMM_OK;
}

bool stop_pmu_for_vcpu(struct vmm_vcpu *vcpu)
{
    u32 mem_acc_cnt=0, new_actual_budget=0, pmcr;
    // Disable all counters for configuration (PCMCNTENCLR)
    asm volatile ("MCR p15, 0, %0, c9, c12, 2\t\n" :: "r"(0x8000000f));
    mem_acc_cnt = _read_cpu_counter(0);
    new_actual_budget = (mem_acc_cnt != 0) ? (MAX_PMU_COUNTER_VALUE - mem_acc_cnt) : 0;
    vcpu->mactual_budget = new_actual_budget;

    pmcr = (0x2 | 0x4); //stop pmu and reset all counters and cycle counters

    // program the performance-counter control-register (PMCR):
    asm volatile ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(pmcr));

    //VMM_PRINTF_PMUDEBUG("%s: stopping pmu for vcpu %d in core %d CNT=%u, new_act_budget=%u\n",__func__,vcpu->id,vmm_smp_processor_id(),mem_acc_cnt,new_actual_budget);

    return VMM_OK;
}

int __cpuinit arch_pmu_init(void)
{
    /*Do initialization*/
    //usare find per trovare 105 = 96 + 9
    vmm_host_irq_find(0, VMM_IRQ_STATE_PER_CPU,
                       &pmu_irq);
    pmu_irq += 9;
    vmm_printf("pmu_irq = %d\n",pmu_irq);

    vmm_host_irq_register(pmu_irq, "PMU",
                   &pmu_handler, NULL);
    return VMM_OK;
}
