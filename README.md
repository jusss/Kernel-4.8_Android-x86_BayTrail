# Kernel-4.8_Android-x86_BayTrail





  Kernel Android-x86 4.8 + Sound codec RT5640 on SSP2 for
	
Tablet Mediacom WinPad W101 3G
	
#Replace Firmware 
fw_sst_0f28_ssp0.bin -> /lib/firmware/intel/fw_sst_0f28.bin


Extract Information with the command:	
#dmidecode | grep -A3 '^System Information'

In my case:

#System Information

      Manufacturer: Inside

      Product Name: BayTrail

      Version: Type1 - TBD by OEM
 


#add element in  byt_rt5640_quirk_table[] = {


example:


        {
                .callback = byt_rt5640_quirk_cb,
                .matches = {
                        DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Insyde"),
                        DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "BayTrail"),
                },
                .driver_data = (unsigned long *)(BYT_RT5640_IN1_MAP |
                                                 BYT_RT5640_MCLK_EN),
        },


#NOTES:
