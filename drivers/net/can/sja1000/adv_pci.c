/*
 * Copyright (C) 2011 Pavel Samarkin <pavel.samarkin@gmail.com>
 *
 * Derived from the ems_pci.c driver:
 *	Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 *	Copyright (C) 2008 Markus Plessing <plessing@ems-wuensche.com>
 *	Copyright (C) 2008 Sebastian Haas <haas@ems-wuensche.com>
 *
 * This software is available to you under a choice of one of two
 * licenses. You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/io.h>
#include <linux/pci.h>

#include "sja1000.h"

MODULE_AUTHOR("Pavel Samarkin (samarkinpa@gmail.com)");
MODULE_DESCRIPTION("Socket-CAN driver for Advantech PCI cards");
MODULE_SUPPORTED_DEVICE("Advantech PCI cards");
MODULE_LICENSE("Dual BSD/GPL");

#define MAX_NO_OF_CHANNELS 4 /* max no of channels on a single card */

struct adv_pci_card {
	int channels;
	struct pci_dev *pci_dev;
	struct net_device *net_dev[MAX_NO_OF_CHANNELS];
    unsigned int RegShift;
};

#define PCI_VENDOR_ID_ADV 0x13fe
#define DRV_NAME "adv_pci"

/*
 * Depends on the board configuration
 */
#define ADV_PCI_OCR (OCR_TX0_PUSHPULL | OCR_TX1_PUSHPULL | OCR_TX1_INVERT)

/*
 * In the CDR register, you should set CBP to 1.
 */
#define ADV_PCI_CDR (CDR_CBP)

/*
 * According to the datasheet,
 * internal clock is 1/2 of the external oscillator frequency
 * which is 16 MHz
 */
#define ADV_PCI_CAN_CLOCK (16000000 / 2)

static const struct pci_device_id adv_pci_tbl[] = {
	{PCI_VENDOR_ID_ADV, 0x1680, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0x3680, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0x2052, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0x1681, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0xc001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0xc002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0xc004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0xc101, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0xc102, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{PCI_VENDOR_ID_ADV, 0xc104, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
    {PCI_VENDOR_ID_ADV, 0xc201, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
    {PCI_VENDOR_ID_ADV, 0xc202, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
    {PCI_VENDOR_ID_ADV, 0xc204, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
    {PCI_VENDOR_ID_ADV, 0xc301, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
    {PCI_VENDOR_ID_ADV, 0xc302, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
    {PCI_VENDOR_ID_ADV, 0xc304, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ANY_ID},
	{0,}
};

/*
 * Read one of the SJA1000 registers
 */
static u8 adv_pci_read_reg(const struct sja1000_priv *priv, int port)
{
    struct adv_pci_card *card = priv->priv;

	return ioread8(priv->reg_base + (port << card->RegShift));
}

/*
 * Write one of the SJA1000 registers
 */
static void adv_pci_write_reg(const struct sja1000_priv *priv,
						 int port, u8 val)
{
    struct adv_pci_card *card = priv->priv;

	iowrite8(val, priv->reg_base + (port << card->RegShift));
}

static void adv_pci_remove_one(struct pci_dev *pdev)
{
	struct adv_pci_card *card = pci_get_drvdata(pdev);
	struct net_device *dev;
	int i;

	dev_info(&pdev->dev, "Removing card");
	for (i = 0; i < card->channels; i++) {
		dev = card->net_dev[i];

		if (!dev)
			continue;

		dev_info(&pdev->dev, "Removing %s.\n", dev->name);
		unregister_sja1000dev(dev);
		free_sja1000dev(dev);
	}

	kfree(card);

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int adv_pci_reset(const struct sja1000_priv *priv)
{
	unsigned char res;

	/* Make sure SJA1000 is in reset mode */
	priv->write_reg(priv, SJA1000_MOD, 1);

	/* Set PeliCAN mode */
	priv->write_reg(priv, SJA1000_CDR, CDR_PELICAN);

	/* check if mode is set */
	res = priv->read_reg(priv, SJA1000_CDR);

	if (res != CDR_PELICAN)
		return -EIO;

	return 0;
}

/*
 * Probe PCI device for Advantech CAN signature and register each available
 * CAN channel to SJA1000 Socket-CAN subsystem.
 */
static int /*__devinit*/ adv_pci_add_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct sja1000_priv *priv;
	struct net_device *dev;
	struct adv_pci_card *card;

	unsigned int portNum;
	unsigned int bar, barFlag, offset;
	int i, err;
    unsigned int RegShift;

	err = 0;
	portNum = 0;
	bar = 0;
	barFlag = 0;
	offset = 0x100;
    RegShift = 0;

	dev_info(&pdev->dev, "Registering card");

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable card");
		return -ENODEV;
	}

	/* Identifying card */
	switch (pdev->device) {
	case 0xc001:
	case 0xc002:
	case 0xc004:
	case 0xc101:
	case 0xc102:
	case 0xc104:
		portNum = pdev->device & 0x7;
		break;
    case 0xc201:
    case 0xc202:
    case 0xc204:
    case 0xc301:
    case 0xc302:
    case 0xc304:
        portNum = pdev->device & 0x7;
        offset = 0x400;
        RegShift = 2;
        break;
	case 0x1680:
	case 0x2052:
		portNum = 2;
		bar = 2;
		barFlag = 1;
		offset = 0x0;
		break;
	case 0x1681:
		portNum = 1;
		bar = 2;
		barFlag = 1;
		offset = 0x0;
		break;
	}

	dev_info(&pdev->dev, "Detected Advantech PCI card at slot #%i\n",
			PCI_SLOT(pdev->devfn));

	dev_info(&pdev->dev, "Device ID #%x\n", pdev->device);

	/* Allocating card structures to hold addresses, ... */
	card = kzalloc(sizeof(struct adv_pci_card), GFP_KERNEL);
	if (card == NULL) {
		dev_err(&pdev->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, card);

	card->channels = portNum;
	card->pci_dev = pdev;
    card->RegShift = RegShift;

	for (i = 0; i < portNum; i++) {
		dev = alloc_sja1000dev(0);
		if (dev == NULL) {
			err = -ENOMEM;
			goto failure_cleanup;
		}

		card->net_dev[i] = dev;
		priv = netdev_priv(dev);
		priv->priv = card;
		priv->irq_flags = IRQF_SHARED;

		dev->irq = pdev->irq;
		priv->reg_base = pci_iomap(pdev, bar, 128) + offset * i;

	    dev_info(&pdev->dev, "Port %i - Base %x\n",
            i, priv->reg_base);

		priv->read_reg = adv_pci_read_reg;
		priv->write_reg = adv_pci_write_reg;

		adv_pci_reset(priv);

		priv->can.clock.freq = ADV_PCI_CAN_CLOCK;
		priv->ocr = ADV_PCI_OCR;
		priv->cdr = ADV_PCI_CDR;

		SET_NETDEV_DEV(dev, &pdev->dev);
		dev->dev_id = i;

		/* Register SJA1000 device */
		err = register_sja1000dev(dev);
		if (err) {
			dev_err(&pdev->dev, "Registering device failed "
							"(err=%d)\n", err);
			free_sja1000dev(dev);
			goto failure_cleanup;
		}

		if (barFlag)
			bar++;
	}
	return 0;

failure_cleanup:
	adv_pci_remove_one(pdev);
	return err;
}

static struct pci_driver adv_pci_driver = {
	.name = DRV_NAME,
	.id_table = adv_pci_tbl,
	.probe = adv_pci_add_one,
	.remove = adv_pci_remove_one
};

static int __init adv_pci_init(void)
{
	return pci_register_driver(&adv_pci_driver);
}

static void __exit adv_pci_exit(void)
{
	pci_unregister_driver(&adv_pci_driver);
}

module_init(adv_pci_init);
module_exit(adv_pci_exit);
