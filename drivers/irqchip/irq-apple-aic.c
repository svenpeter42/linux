// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2021 Hector Martin <marcan@marcan.st>
 *
 * Based on irq-lpc32xx:
 *   Copyright 2015-2016 Vladimir Zapolskiy <vz@mleia.com>
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <asm/exception.h>

#define AIC_EVENT		0x2004

#define AIC_EVENT_TYPE_HW	1

#define AIC_TARGET_CPU		0x3000
#define AIC_SW_GEN_SET		0x4000
#define AIC_SW_GEN_CLR		0x4080
#define AIC_MASK_SET		0x4100
#define AIC_MASK_CLR		0x4180

#define MASK_REG(x)		(4 * ((x)>>5))
#define MASK_BIT(x)		BIT((x)&0x1f)

#define NR_AIC_IRQS		896

struct aic_irq_chip {
	void __iomem *base;
	struct irq_domain *domain;
};

static DEFINE_SPINLOCK(aic_lock);
static struct aic_irq_chip *aic_irqc;

static inline u32 aic_ic_read(struct aic_irq_chip *ic, u32 reg)
{
	return readl(ic->base + reg);
}

static inline void aic_ic_write(struct aic_irq_chip *ic,
				    u32 reg, u32 val)
{
	writel(val, ic->base + reg);
}

static void aic_irq_mask(struct irq_data *d)
{
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);

	aic_ic_write(ic, AIC_MASK_SET + MASK_REG(d->hwirq), MASK_BIT(d->hwirq));
}

static void aic_irq_unmask(struct irq_data *d)
{
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);

	aic_ic_write(ic, AIC_MASK_CLR + MASK_REG(d->hwirq), MASK_BIT(d->hwirq));
}

static void aic_irq_eoi(struct irq_data *d)
{
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);

	/*
	 * Reading the interrupt reason automatically acknowledges and masks the IRQ,
	 * so we just unmask it here if needed.
	 */
	if (!irqd_irq_disabled(d) && !irqd_irq_masked(d))
		aic_irq_unmask(d);
}

static int aic_irq_set_affinity(struct irq_data *d,
				const struct cpumask *mask_val, bool force)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct aic_irq_chip *ic = irq_data_get_irq_chip_data(d);
	unsigned long flags;
	unsigned int cpu;

	if (force)
		cpu = cpumask_first(mask_val);
	else
		cpu = cpumask_any_and(mask_val, cpu_online_mask);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	spin_lock_irqsave(&aic_lock, flags);
	aic_ic_write(ic, AIC_TARGET_CPU + hwirq * 4, BIT(cpu));
	irq_data_update_effective_affinity(d, cpumask_of(cpu));
	spin_unlock_irqrestore(&aic_lock, flags);

	return IRQ_SET_MASK_OK;
}

static void __exception_irq_entry aic_handle_irq(struct pt_regs *regs)
{
	struct aic_irq_chip *ic = aic_irqc;
	u32 reason = aic_ic_read(ic, AIC_EVENT);

	while (reason) {
		u32 type = reason >> 16, irq = reason & 0xffff;
		if (type == AIC_EVENT_TYPE_HW) {
			handle_domain_irq(aic_irqc->domain, irq, regs);
		} else {
			pr_err("spurious IRQ event %d, %d\n", type, reason);
		}
		reason = aic_ic_read(ic, AIC_EVENT);
	}
}

static struct irq_chip aic_chip = {
	.name			= "AIC",
	.irq_mask		= aic_irq_mask,
	.irq_unmask		= aic_irq_unmask,
	.irq_eoi		= aic_irq_eoi,
#ifdef CONFIG_SMP
	.irq_set_affinity	= aic_irq_set_affinity,
#endif
};

static int aic_irq_domain_map(struct irq_domain *id, unsigned int irq,
				  irq_hw_number_t hw)
{
	struct aic_irq_chip *ic = id->host_data;

	irq_set_chip_data(irq, ic);
	irq_set_chip_and_handler(irq, &aic_chip, handle_fasteoi_irq);
	irq_set_status_flags(irq, IRQ_LEVEL);
	irq_set_noprobe(irq);

	return 0;
}

static void aic_irq_domain_unmap(struct irq_domain *id, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
}

static const struct irq_domain_ops aic_irq_domain_ops = {
	.map    = aic_irq_domain_map,
	.unmap	= aic_irq_domain_unmap,
	.xlate  = irq_domain_xlate_twocell,
};

static int __init aic_of_ic_init(struct device_node *node,
				     struct device_node *parent)
{
	int i;
	void __iomem *regs;
	struct aic_irq_chip *irqc;

	regs = of_iomap(node, 0);
	if (WARN_ON(!regs))
		return -EIO;

	irqc = kzalloc(sizeof(*irqc), GFP_KERNEL);
	if (!irqc)
		return -ENOMEM;

	irqc->base = regs;
	irqc->domain = irq_domain_add_linear(node, NR_AIC_IRQS,
					     &aic_irq_domain_ops, irqc);
	if (!irqc->domain) {
		pr_err("unable to add irq domain\n");
		iounmap(irqc->base);
		kfree(irqc);
		return -ENODEV;
	}

	aic_irqc = irqc;
	set_handle_irq(aic_handle_irq);

	for (i = 0; i < BITS_TO_LONGS(NR_AIC_IRQS); i++)
		aic_ic_write(irqc, AIC_MASK_SET + i * 4, ~0);
	for (i = 0; i < BITS_TO_LONGS(NR_AIC_IRQS); i++)
		aic_ic_write(irqc, AIC_SW_GEN_CLR + i * 4, ~0);
	for (i = 0; i < NR_AIC_IRQS; i++)
		aic_ic_write(irqc, AIC_TARGET_CPU + i * 4, 1);

	pr_info("AIC: initialized\n");

	return 0;
}

IRQCHIP_DECLARE(apple_t8103_aic, "apple,t8103-aic", aic_of_ic_init);
