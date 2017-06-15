/**
 * Copyright (c) 2017 Paolo Modica.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file cmd_cp15.c
 * @author Paolo Modica
 * @brief Implementation of cp15 commands
 */

#include <vmm_error.h>
#include <vmm_stdio.h>
#include <vmm_devtree.h>
#include <vmm_manager.h>
#include <vmm_modules.h>
#include <vmm_cmdmgr.h>
#include <vmm_devemu.h>
#include <vmm_heap.h>
#include <vmm_smp.h>
#include <libs/stringlib.h>
#include <vmm_workqueue.h>
#include <cpu_pmu.h>
#include <vmm_timer.h>

#define MODULE_DESC			"Command cp15"
#define MODULE_AUTHOR       "Paolo Modica"
#define MODULE_LICENSE		"GPL"
#define MODULE_IPRIORITY	0
#define	MODULE_INIT			cmd_cp15_init
#define	MODULE_EXIT			cmd_cp15_exit

static struct vmm_work pmu_init;
static struct vmm_work pmu_read;
static struct vmm_work pmu_init_int;
static struct vmm_work pmu_reset;
static struct vmm_work timer_init;
struct vmm_timer_event ev[4];


// This will contain a nice human-readable name for the configured counters.
const char* cpu_name[5] = { "", "", "", "", "CCNT" };

typedef struct {
  u32 reg[5];       // 4 configurables and the cycle count
} cpu_perf;

cpu_perf global_cpu_perf[4];

inline u32 _read_cpu_counter(/*struct vmm_chardev *cdev,*/ int r) {
  // Read PMXEVCNTR #r
  // This is done by first writing the counter number to PMSELR and then reading PMXEVCNTR
  u32 ret;
  asm volatile ("MCR p15, 0, %0, c9, c12, 5\t\n" :: "r"(r));      // Select event counter in PMSELR
  asm volatile ("MRC p15, 0, %0, c9, c13, 2\t\n" : "=r"(ret));    // Read from PMXEVCNTR
  vmm_printf("Counter %d, evt=%s value = %d\n",r,cpu_name[r],ret);
  return ret;
}

inline void _setup_cpu_counter(u32 r, u32 event, const char* name) {
  cpu_name[r] = name;

  // Write PMXEVTYPER #r
  // This is done by first writing the counter number to PMSELR and then writing PMXEVTYPER
  asm volatile ("MCR p15, 0, %0, c9, c12, 5\t\n" :: "r"(r));        // Select event counter in PMSELR
  asm volatile ("MCR p15, 0, %0, c9, c13, 1\t\n" :: "r"(event));    // Set the event number in PMXEVTYPER
}

void init_cpu_perf() {

  // Disable all counters for configuration (PCMCNTENCLR)
  asm volatile ("MCR p15, 0, %0, c9, c12, 2\t\n" :: "r"(0x8000003f));

  // disable counter overflow interrupts
  asm volatile ("MCR p15, 0, %0, c9, c14, 2\n\t" :: "r"(0x8000003f));


  // Select which events to count in the 6 configurable counters
  // Note that both of these examples come from the list of required events.
  _setup_cpu_counter(0, 0x13, "DMACC");
  _setup_cpu_counter(1, 0x16, "L2DA");
  _setup_cpu_counter(2, 0x17, "L2CRF");

}

inline void reset_cpu_perf() {

  // Disable all counters (PMCNTENCLR):
  asm volatile ("MCR p15, 0, %0, c9, c12, 2\t\n" :: "r"(0x8000003f));
  asm volatile ("MCR p15, 0, %0, c9, c12, 2\t\n" :: "r"(0x00000000));
  u32 pmcr  = 0x1    // enable counters
            | 0x2    // reset all other counters
            | 0x4    // reset cycle counter
            //| 0x8    // enable "by 64" divider for CCNT.
            //| 0x10;  // Export events to external monitoring
            ;
  // program the performance-counter control-register (PMCR):
  asm volatile ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(pmcr));

  // clear overflows (PMOVSR):
  asm volatile ("MCR p15, 0, %0, c9, c12, 3\t\n" :: "r"(0x8000003f));

  // Enable intterrup overflow for all counters
  asm volatile ("MCR p15, 0, %0, c9, c14, 1\t\n" :: "r"(0x8000003f));

  // Enable all counters (PMCNTENSET):
  asm volatile ("MCR p15, 0, %0, c9, c12, 1\t\n" :: "r"(0x8000003f));

}

inline cpu_perf get_cpu_perf(/*struct vmm_chardev *cdev*/) {
  cpu_perf ret;
  int r;
  u32 pmovsr;

  // Read the configurable counters
  for (r=0; r<4; ++r) {
    ret.reg[r] = _read_cpu_counter(/*cdev,*/r);
  }

  // Read CPU cycle count from the CCNT Register
  asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(ret.reg[4]));

  for (r=0; r<5; r++)
  {
      vmm_printf("Performace counters #%d, evt = %s, value=%d:\n",r,cpu_name[r],ret.reg[r]);
  }

  // read overflows (PMOVSR):
  asm volatile ("MRC p15, 0, %0, c9, c12, 3\t\n" :"=r"(pmovsr));
  vmm_printf("PMOVSR register = 0x%X\n",pmovsr);
  global_cpu_perf[vmm_smp_processor_id()]=ret;
  return ret;
}

static void cmd_cp15_compute(struct vmm_chardev *cdev) {

    int *g_mem_ptr = 0;
    int mem_size = 4090;
    int i;
    g_mem_ptr = (int *)vmm_malloc(mem_size);
    memset((char *)g_mem_ptr, 1, mem_size);

    for (i = 0; i < mem_size / sizeof(int); i++)
    {
        g_mem_ptr[i] = i;
        vmm_cprintf(cdev, "g_mem_ptr[%d]=%d:\n",i,g_mem_ptr[i]);
    }

    vmm_free(g_mem_ptr);

}


static void cmd_cp15_usage(struct vmm_chardev *cdev)
{
    vmm_cprintf(cdev, "Usage:\n");
    vmm_cprintf(cdev, "   cp15 help\n");
    vmm_cprintf(cdev, "   cp15 read : (ID0 and ID1)\n");
    vmm_cprintf(cdev, "   cp15 reset #cpu: (reset e set all counters of #cpu)\n");
    vmm_cprintf(cdev, "   cp15 read_counters #cpu: (read all counters of #cpu)\n");
    vmm_cprintf(cdev, "   cp15 compute : (simulate computation)\n");
    vmm_cprintf(cdev, "   cp15 init NULL\n");
    vmm_cprintf(cdev, "   cp15 reset_wfi #cpu\n");
    vmm_cprintf(cdev, "   cp15 init_timer #cpu\n");
}

static void cmd_cp15_events(struct vmm_chardev *cdev)
{
    u32 ID0=-1;
    u32 ID1=-1;
    asm volatile ("MRC p15, 0, %0, c9, c12, 6\t\n":"=r"(ID0));
    asm volatile ("MRC p15, 0, %0, c9, c12, 7\t\n":"=r"(ID1));
    vmm_cprintf(cdev, "ID0 REGISTER = %d\n",ID0);
    vmm_cprintf(cdev, "ID1 REGISTER = %d\n",ID1);

}

static void pmu_init_work(struct vmm_work *work)
{
    //struct vmm_vcpu *vcpu;
    init_cpu_perf();
    reset_cpu_perf();
    /* Lock VCPU WFI */
   // vmm_spin_lock_irqsave_lite(&vcpu->irqs.wfi.lock, flags);

    /* Set wait for irq state */
    //vcpu->irqs.wfi.state = FALSE;

    /* Unlock VCPU WFI */
    //vmm_spin_unlock_irqrestore_lite(&vcpu->irqs.wfi.lock, flags);
    vmm_printf("Counters resetted and enabled for cpu #%d\n",vmm_smp_processor_id());
}

static void pmu_read_work(struct vmm_work *work)
{
    get_cpu_perf();
}

static void pmu_init_int_work(struct vmm_work *work)
{
    arch_pmu_init();
    vmm_printf("Init pmu on cpu %d\n", vmm_smp_processor_id());
}

static void pmu_reset_work(struct vmm_work *work)
{
    irq_flags_t flags;
    struct vmm_vcpu *vcpu;
    u32 id;

    if (vmm_smp_processor_id() == 2)
        id=22;
    else id=23;

    vcpu = vmm_manager_vcpu(id);
    /* Lock VCPU WFI */
    vmm_spin_lock_irqsave_lite(&vcpu->irqs.wfi.lock, flags);

    /* Set wait for irq state */
    vcpu->irqs.wfi.state = TRUE;

    /* Unlock VCPU WFI */
    vmm_spin_unlock_irqrestore_lite(&vcpu->irqs.wfi.lock, flags);
    vmm_printf("%s: finish/n",__func__);
}

static void my_timer_event(struct vmm_timer_event *ev)
{
    vmm_printf("Timer event arrived on cpu %d\n",vmm_smp_processor_id());
}

static void timer_init_work(struct vmm_work *work)
{
    u64 dur = 15000000000;
    INIT_TIMER_EVENT(&ev[vmm_smp_processor_id()], &my_timer_event, NULL);
    vmm_timer_event_start(&ev[vmm_smp_processor_id()], dur);
    vmm_printf("timer inizialized and started on cpu = %d",vmm_smp_processor_id());
}



static int cmd_cp15_exec(struct vmm_chardev *cdev, int argc, char **argv)
{
    //int ret = VMM_OK;
    //u32 size;
    //physical_addr_t src_addr;
    if (argc == 2) {
        if (strcmp(argv[1], "help") == 0) {
            cmd_cp15_usage(cdev);
            return VMM_OK;
        } else if (strcmp(argv[1], "read") == 0) {
            cmd_cp15_events(cdev);
            return VMM_OK;
        } else if (strcmp(argv[1], "init") == 0) {

            vmm_printf("NULL\n");
            return VMM_OK;
        } else if (strcmp(argv[1], "compute") == 0) {
            cmd_cp15_compute(cdev);
        return VMM_OK;
    }
    }
    if (argc < 3) {
        cmd_cp15_usage(cdev);
        return VMM_EFAIL;
    }
    if (strcmp(argv[1], "reset") == 0) {
        INIT_WORK(&pmu_init, &pmu_init_work);
        vmm_workqueue_schedule_work(vmm_workqueue_index2workqueue(atoi(argv[2])), &pmu_init);
        return VMM_OK;
    } else if (strcmp(argv[1], "read_counters") == 0) {
        INIT_WORK(&pmu_read, &pmu_read_work);
        vmm_workqueue_schedule_work(vmm_workqueue_index2workqueue(atoi(argv[2])), &pmu_read);
        return VMM_OK;
    } else if (strcmp(argv[1], "init") == 0) {
        INIT_WORK(&pmu_init_int, &pmu_init_int_work);
        vmm_workqueue_schedule_work(vmm_workqueue_index2workqueue(atoi(argv[2])), &pmu_init_int);
        return VMM_OK;
    } else if (strcmp(argv[1], "reset_wfi") == 0) {
        INIT_WORK(&pmu_reset, &pmu_reset_work);
        vmm_workqueue_schedule_work(vmm_workqueue_index2workqueue(atoi(argv[2])), &pmu_reset);
        return VMM_OK;
    } else if (strcmp(argv[1], "init_timer") == 0) {
        INIT_WORK(&timer_init, &timer_init_work);
        vmm_workqueue_schedule_work(vmm_workqueue_index2workqueue(atoi(argv[2])), &timer_init);
        return VMM_OK;
    }
    return VMM_OK;
}


static struct vmm_cmd cmd_cp15 = {
    .name = "cp15",
    .desc = "control commands for cp15",
    .usage = cmd_cp15_usage,
    .exec = cmd_cp15_exec,
};

static int __init cmd_cp15_init(void)
{
    return vmm_cmdmgr_register_cmd(&cmd_cp15);
}

static void __exit cmd_cp15_exit(void)
{
    vmm_cmdmgr_unregister_cmd(&cmd_cp15);
}

VMM_DECLARE_MODULE(MODULE_DESC,
            MODULE_AUTHOR,
            MODULE_LICENSE,
            MODULE_IPRIORITY,
            MODULE_INIT,
            MODULE_EXIT);
