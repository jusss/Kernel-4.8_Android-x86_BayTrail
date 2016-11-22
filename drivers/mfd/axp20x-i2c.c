/*
 * I2C driver for the X-Powers' Power Management ICs
 *
 * AXP20x typically comprises an adaptive USB-Compatible PWM charger, BUCK DC-DC
 * converters, LDOs, multiple 12-bit ADCs of voltage, current and temperature
 * as well as configurable GPIOs.
 *
 * This driver supports the I2C variants.
 *
 * Copyright (C) 2014 Carlo Caione
 *
 * Author: Carlo Caione <carlo@caione.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mfd/axp20x.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#define GPIO_USB_MUX_INDEX	1
#define XPOWER_DEFAULT_TEMP_MAX	45

static int fg_bat_curve[] = {
        0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x2,
        0x2, 0x3, 0x5, 0x9, 0xf, 0x18, 0x24, 0x29,
    0x2e, 0x32, 0x35, 0x3b, 0x40, 0x45, 0x49, 0x4c,
    0x50, 0x53, 0x55, 0x57, 0x5a, 0x5d, 0x61, 0x64,
};

#ifdef CONFIG_ACPI
#define FGCONFIG_ACPI_TABLE_NAME        "BCFG"
#define XPWR_FGCONFIG_NAME              "XPOWER-0"

static int axp288_get_acpi_cdata(struct axp20x_fg_pdata *pdata)
{
	struct axp288_acpi_fg_config *acpi_tbl = NULL;
	char *name = FGCONFIG_ACPI_TABLE_NAME;
	int i;
	acpi_size tbl_size;
	acpi_status status;

        /* read the fg config table from acpi */
        status = acpi_get_table_with_size(name , 0,
                        (struct acpi_table_header **)&acpi_tbl, &tbl_size);
        if (ACPI_FAILURE(status)) {
                pr_err("%s:%s table not found!!\n", __func__, name);
                return status;
        }
        pr_info("%s: %s table found, size=%d\n",
                                __func__, name, (int)tbl_size);

        /* validate the table size */
        if (tbl_size <  sizeof(struct axp288_acpi_fg_config)) {
                pr_err("%s:%s table incomplete!!\n", __func__, name);
                pr_info("%s: table_size=%d, structure_size=%lu\n",
                        __func__, (int)tbl_size,
                        sizeof(struct axp288_acpi_fg_config));
                return -ENXIO;
        }

        if (strncmp(acpi_tbl->cdata.fg_name, XPWR_FGCONFIG_NAME,
                ACPI_FG_CONF_NAME_LEN)
                || strncmp(acpi_tbl->cdata.battid, pdata->battid,
                BATTID_STR_LEN)) {
                pr_err("%s: battid and fg_name mismatch!!!\n", __func__);
                return -EINVAL;
        }

        /* copy battid */
        for (i = 0; i < BATTID_STR_LEN; i++)
		pdata->battid[i]=acpi_tbl->cdata.battid[i];

	pdata->cap1=acpi_tbl->cdata.cap1;
	pdata->cap0=acpi_tbl->cdata.cap0;
	pdata->rdc1=acpi_tbl->cdata.rdc1;
	pdata->rdc0=acpi_tbl->cdata.rdc0;

        /* copy curve data */
        for (i = 0; i < OCV_CURVE_SIZE; i++)
                pdata->ocv_curve[i] = acpi_tbl->cdata.bat_curve[i];

	return 0;
}
#endif /* CONFIG_ACPI */

static void axp288_get_fg_config_data(struct axp20x_fg_pdata *pdata)
{
        int scaled_capacity;
        int i;

#ifdef CONFIG_ACPI
        if (!axp288_get_acpi_cdata(pdata)) {
                pr_info("%s: Loading fg config from acpi table\n", __func__);
                return;
        }
#endif /* CONFIG_ACPI */

        pr_info("%s: Loading default fg config.\n", __func__);
        /*
         * Calculate cap1 and cap0.  The value of a LSB is 1.456mAh.
         * Using 1.5 as math friendly and close enough.
         */

        scaled_capacity = (pdata->design_cap >> 1) +
                                (pdata->design_cap >> 3) +
                                (pdata->design_cap >> 4);

        /*
         * bit 7 of cap1 register is set to indicate battery maximum
         * capacity is valid
         */
        pdata->cap0 = scaled_capacity & 0xFF;
        pdata->cap1 = (scaled_capacity >> 8) | 0x80;

        pdata->rdc1 = 0xc0;
        pdata->rdc0 = 0x97;

        /* copy curve data */
        for (i = 0; i < OCV_CURVE_SIZE; i++)
                pdata->ocv_curve[i] = fg_bat_curve[i];

        return;
}

static void platform_set_battery_data(struct axp20x_fg_pdata *pdata)
{
        pdata->design_cap = 4045;
        pdata->max_volt = 4350;
        pdata->min_volt = 3400;
}

static void axp288_fg_pdata(void)
{
	static struct axp20x_fg_pdata pdata;

	platform_set_battery_data(&pdata);
	pdata.max_temp = XPOWER_DEFAULT_TEMP_MAX;

	/* Load FG config data to pdata */
	axp288_get_fg_config_data(&pdata);

	axp20x_set_pdata("axp288_fuel_gauge",
		(void *)&pdata, sizeof(pdata), 0);
}

static void axp288_extcon_pdata(struct device *dev)
{
	static struct axp288_extcon_pdata pdata;

	/* Get the gpio based uab mux which will be used to switch usb D+/D-
	 * data line between SOC for data communication and PMIC for charger
	 * detection functionality.
	 */
	// FIXME: This always fails.
	pdata.gpio_mux_cntl = devm_gpiod_get_index(dev,
		"axp20x_i2c", GPIO_USB_MUX_INDEX, 0);

	if (IS_ERR(pdata.gpio_mux_cntl)) {
		pdata.gpio_mux_cntl = NULL;
		dev_err(dev,"Failed to get gpio for extcon pdata\n");
	} else {
		gpiod_put(pdata.gpio_mux_cntl);
	}

	axp20x_set_pdata("axp288_extcon",
		(void *)&pdata, sizeof(pdata), 0);
}

static void platform_init_chrg_params(struct axp20x_chrg_pdata *pdata)
{
	/* Initialize the default parameters */
	pdata->def_cc = 500;
	pdata->def_cv = 4200;
	pdata->max_cc = 2000;
	pdata->max_cv = 4350;
}

static void axp288_chrg_pdata(void)
{
	static struct axp20x_chrg_pdata pdata;
	platform_init_chrg_params(&pdata);
	axp20x_set_pdata("axp288_charger",
		(void *)&pdata, sizeof(pdata), 0);
}

static int axp288_init(struct device *dev)
{
	axp288_chrg_pdata();
	axp288_extcon_pdata(dev);
	axp288_fg_pdata();
	return 0;
}

static int axp20x_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct axp20x_dev *axp20x;
	int ret;

	axp20x = devm_kzalloc(&i2c->dev, sizeof(*axp20x), GFP_KERNEL);
	if (!axp20x)
		return -ENOMEM;

	axp20x->dev = &i2c->dev;
	axp20x->irq = i2c->irq;
	dev_set_drvdata(axp20x->dev, axp20x);

	ret = axp20x_match_device(axp20x);
	if (ret)
		return ret;

	axp20x->regmap = devm_regmap_init_i2c(i2c, axp20x->regmap_cfg);
	if (IS_ERR(axp20x->regmap)) {
		ret = PTR_ERR(axp20x->regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	if (axp20x->variant==AXP288_ID) axp288_init(axp20x->dev);

	return axp20x_device_probe(axp20x);
}

static int axp20x_i2c_remove(struct i2c_client *i2c)
{
	struct axp20x_dev *axp20x = i2c_get_clientdata(i2c);

	return axp20x_device_remove(axp20x);
}

static const struct of_device_id axp20x_i2c_of_match[] = {
	{ .compatible = "x-powers,axp152", .data = (void *)AXP152_ID },
	{ .compatible = "x-powers,axp202", .data = (void *)AXP202_ID },
	{ .compatible = "x-powers,axp209", .data = (void *)AXP209_ID },
	{ .compatible = "x-powers,axp221", .data = (void *)AXP221_ID },
	{ },
};
MODULE_DEVICE_TABLE(of, axp20x_i2c_of_match);

/*
 * This is useless for OF-enabled devices, but it is needed by I2C subsystem
 */
static const struct i2c_device_id axp20x_i2c_id[] = {
	{ },
};
MODULE_DEVICE_TABLE(i2c, axp20x_i2c_id);

static const struct acpi_device_id axp20x_i2c_acpi_match[] = {
	{
		.id = "INT33F4",
		.driver_data = AXP288_ID,
	},
	{ },
};
MODULE_DEVICE_TABLE(acpi, axp20x_i2c_acpi_match);

static struct i2c_driver axp20x_i2c_driver = {
	.driver = {
		.name	= "axp20x-i2c",
		.of_match_table	= of_match_ptr(axp20x_i2c_of_match),
		.acpi_match_table = ACPI_PTR(axp20x_i2c_acpi_match),
	},
	.probe		= axp20x_i2c_probe,
	.remove		= axp20x_i2c_remove,
	.id_table	= axp20x_i2c_id,
};

module_i2c_driver(axp20x_i2c_driver);

MODULE_DESCRIPTION("PMIC MFD I2C driver for AXP20X");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_LICENSE("GPL");
