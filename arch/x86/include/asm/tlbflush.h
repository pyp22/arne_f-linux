#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/special_insns.h>

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define __flush_tlb() __native_flush_tlb()
#define __flush_tlb_global() __native_flush_tlb_global()
#define __flush_tlb_single(addr) __native_flush_tlb_single(addr)
#endif

static inline void __native_flush_tlb(void)
{
	if (static_cpu_has(X86_FEATURE_INVPCID)) {
		u64 descriptor[2];

		descriptor[0] = PCID_KERNEL;
		asm volatile(__ASM_INVPCID : : "d"(&descriptor), "a"(INVPCID_ALL_MONGLOBAL) : "memory");
		return;
	}

#if defined(CONFIG_X86_64) && defined(CONFIG_PAX_MEMORY_UDEREF)
	if (static_cpu_has(X86_FEATURE_PCID)) {
		unsigned int cpu = raw_get_cpu();

		native_write_cr3(__pa(get_cpu_pgd(cpu, user)) | PCID_USER);
		native_write_cr3(__pa(get_cpu_pgd(cpu, kernel)) | PCID_KERNEL);
		raw_put_cpu_no_resched();
		return;
	}
#endif

	native_write_cr3(native_read_cr3());
}

static inline void __native_flush_tlb_global_irq_disabled(void)
{
	if (static_cpu_has(X86_FEATURE_INVPCID)) {
		u64 descriptor[2];

		descriptor[0] = PCID_KERNEL;
		asm volatile(__ASM_INVPCID : : "d"(&descriptor), "a"(INVPCID_ALL_GLOBAL) : "memory");
	} else {
		unsigned long cr4;

		cr4 = native_read_cr4();
		/* clear PGE */
		native_write_cr4(cr4 & ~X86_CR4_PGE);
		/* write old PGE again and flush TLBs */
		native_write_cr4(cr4);
	}
}

static inline void __native_flush_tlb_global(void)
{
	unsigned long flags;

	/*
	 * Read-modify-write to CR4 - protect it from preemption and
	 * from interrupts. (Use the raw variant because this code can
	 * be called from deep inside debugging code.)
	 */
	raw_local_irq_save(flags);

	__native_flush_tlb_global_irq_disabled();

	raw_local_irq_restore(flags);
}

static inline void __native_flush_tlb_single(unsigned long addr)
{

	if (static_cpu_has(X86_FEATURE_INVPCID)) {
		u64 descriptor[2];

		descriptor[0] = PCID_KERNEL;
		descriptor[1] = addr;

#if defined(CONFIG_X86_64) && defined(CONFIG_PAX_MEMORY_UDEREF)
		if (!static_cpu_has(X86_FEATURE_STRONGUDEREF) || addr >= TASK_SIZE_MAX) {
			if (addr < TASK_SIZE_MAX)
				descriptor[1] += pax_user_shadow_base;
			asm volatile(__ASM_INVPCID : : "d"(&descriptor), "a"(INVPCID_SINGLE_ADDRESS) : "memory");
		}

		descriptor[0] = PCID_USER;
		descriptor[1] = addr;
#endif

		asm volatile(__ASM_INVPCID : : "d"(&descriptor), "a"(INVPCID_SINGLE_ADDRESS) : "memory");
		return;
	}

#if defined(CONFIG_X86_64) && defined(CONFIG_PAX_MEMORY_UDEREF)
	if (static_cpu_has(X86_FEATURE_PCID)) {
		unsigned int cpu = raw_get_cpu();

		native_write_cr3(__pa(get_cpu_pgd(cpu, user)) | PCID_USER | PCID_NOFLUSH);
		asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
		native_write_cr3(__pa(get_cpu_pgd(cpu, kernel)) | PCID_KERNEL | PCID_NOFLUSH);
		raw_put_cpu_no_resched();

		if (!static_cpu_has(X86_FEATURE_STRONGUDEREF) && addr < TASK_SIZE_MAX)
			addr += pax_user_shadow_base;
	}
#endif

	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

static inline void __flush_tlb_all(void)
{
	if (cpu_has_pge)
		__flush_tlb_global();
	else
		__flush_tlb();
}

static inline void __flush_tlb_one(unsigned long addr)
{
		__flush_tlb_single(addr);
}

#define TLB_FLUSH_ALL	-1UL

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_others(cpumask, mm, start, end) flushes TLBs on other cpus
 *
 * ..but the i386 has somewhat limited tlb flushing capabilities,
 * and page-granular flushes are available only on i486 and up.
 */

#ifndef CONFIG_SMP

#define flush_tlb() __flush_tlb()
#define flush_tlb_all() __flush_tlb_all()
#define local_flush_tlb() __flush_tlb()

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->active_mm)
		__flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long addr)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb_one(addr);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb();
}

static inline void flush_tlb_mm_range(struct mm_struct *mm,
	   unsigned long start, unsigned long end, unsigned long vmflag)
{
	if (mm == current->active_mm)
		__flush_tlb();
}

static inline void native_flush_tlb_others(const struct cpumask *cpumask,
					   struct mm_struct *mm,
					   unsigned long start,
					   unsigned long end)
{
}

static inline void reset_lazy_tlbstate(void)
{
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	flush_tlb_all();
}

#else  /* SMP */

#include <asm/smp.h>

#define local_flush_tlb() __flush_tlb()

#define flush_tlb_mm(mm)	flush_tlb_mm_range(mm, 0UL, TLB_FLUSH_ALL, 0UL)

#define flush_tlb_range(vma, start, end)	\
		flush_tlb_mm_range(vma->vm_mm, start, end, vma->vm_flags)

extern void flush_tlb_all(void);
extern void flush_tlb_current_task(void);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);
extern void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned long vmflag);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);

#define flush_tlb()	flush_tlb_current_task()

void native_flush_tlb_others(const struct cpumask *cpumask,
				struct mm_struct *mm,
				unsigned long start, unsigned long end);

#define TLBSTATE_OK	1
#define TLBSTATE_LAZY	2

struct tlb_state {
	struct mm_struct *active_mm;
	int state;
};
DECLARE_PER_CPU_SHARED_ALIGNED(struct tlb_state, cpu_tlbstate);

static inline void reset_lazy_tlbstate(void)
{
	this_cpu_write(cpu_tlbstate.state, 0);
	this_cpu_write(cpu_tlbstate.active_mm, &init_mm);
}

#endif	/* SMP */

#ifndef CONFIG_PARAVIRT
#define flush_tlb_others(mask, mm, start, end)	\
	native_flush_tlb_others(mask, mm, start, end)
#endif

#endif /* _ASM_X86_TLBFLUSH_H */
