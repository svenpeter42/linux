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

#define NR_FIQ_IRQS 1

struct fiq_irq_chip {
	struct irq_domain *domain;
};

static struct fiq_irq_chip *fiq_irqc;

static void __exception_irq_entry fiq_handle_irq(struct pt_regs *regs)
{
	handle_domain_irq(fiq_irqc->domain, 0, regs);
}

static void fiq_irq_enable(struct irq_data *d)
{
}

static void fiq_irq_disable(struct irq_data *d)
{
}

static struct irq_chip fiq_chip = {
	.name			= "FIQ",
	.irq_enable		= fiq_irq_enable,
	.irq_disable		= fiq_irq_disable,
};

static int fiq_irq_domain_map(struct irq_domain *id, unsigned int irq,
				  irq_hw_number_t hw)
{
	struct fiq_irq_chip *ic = id->host_data;

	irq_set_chip_data(irq, ic);
	irq_set_chip_and_handler(irq, &fiq_chip, handle_percpu_devid_irq);
	irq_set_status_flags(irq, IRQ_LEVEL);
	irq_set_percpu_devid(irq);
	irq_set_noprobe(irq);

	return 0;
}

static void fiq_irq_domain_unmap(struct irq_domain *id, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
}

static const struct irq_domain_ops fiq_irq_domain_ops = {
	.map    = fiq_irq_domain_map,
	.unmap	= fiq_irq_domain_unmap,
	.xlate  = irq_domain_xlate_twocell,
};

static int __init fiq_of_ic_init(struct device_node *node,
				     struct device_node *parent)
{
	struct fiq_irq_chip *irqc;

	irqc = kzalloc(sizeof(*irqc), GFP_KERNEL);
	if (!irqc)
		return -ENOMEM;

	irqc->domain = irq_domain_add_linear(node, NR_FIQ_IRQS,
					     &fiq_irq_domain_ops, irqc);
	if (!irqc->domain) {
		pr_err("unable to add irq domain\n");
		kfree(irqc);
		return -ENODEV;
	}

	fiq_irqc = irqc;
	set_handle_fiq(fiq_handle_irq);

	pr_info("FIQ: initialized\n");

	return 0;
}

IRQCHIP_DECLARE(apple_t8103_fiq, "apple,t8103-fiq", fiq_of_ic_init);
