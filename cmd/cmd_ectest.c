/* Eridan custom commands for POST */
#include <common.h>
#include <bootretry.h>
#include <cli.h>
#include <command.h>
#ifdef CONFIG_HAS_DATAFLASH
#include <dataflash.h>
#endif
#include <hash.h>
#include <watchdog.h>
#include <asm/io.h>
#include <linux/compiler.h>
#include <asm/arch/hardware.h>
#include <mapmem.h>
#include <mmc.h>

#define HPS 0
#define DBG 0
#define START_ADDR 0x10000000
#define END_ADDR   0x7f000000

int do_eridan_mem_data_test(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
    vu_long *addr,*dummy;
	ulong dl_ps_err = 0;
	ulong val, readback;
	static const ulong bitpattern = 0x00000001;

	addr = map_sysmem(0x10000000,0);
	val = bitpattern;

	dummy = map_sysmem(SYS_MEMTEST_SCRATCH,0);

	for (; val != 0; val <<= 1) {
#if DBG
		printf("\n Writing value %08lx to address 0x%.8lx\n",val,(ulong)addr);
#endif

		*addr = val;
		*dummy  = ~val; /* writing this clears the data line bus */
		readback = *addr;
#if DBG
		printf("\n Reading value %08lx to address 0x%.8lx \n",readback,(ulong)addr);
#endif
		if(readback != val) {
			printf("FAILURE (data line): expected %08lx, actual %08lx\n", val, readback);
			dl_ps_err++;
		}

#if DBG
		printf("\n Writing value %08lx to address 0x%.8lx \n",~val,(ulong)addr);
#endif

		*addr  = ~val;
		*dummy  = val;
		readback = *addr;
#if DBG
		printf("\n Reading value %08lx to address 0x%.8lx \n",readback,(ulong)addr);
#endif

		if (readback != ~val) {
			printf("FAILURE (data line): Is %08lx, should be %08lx\n",readback, ~val);
			dl_ps_err++;
		}
	}

	printf("PS DDR Data line test is done with ");
	printf("%d errors\r\n",dl_ps_err);

    return 0;
}

int do_eridan_mem_address_test(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
	vu_long pattern ,anti_pattern;
	vu_long *addr;
	vu_long test_offset;
	vu_long offset;
	vu_long temp;
	ulong al_ps_err = 0;

	addr = map_sysmem(0x00000000,0);
	pattern = (vu_long) 0xaaaaaaaa;
	anti_pattern = (vu_long) 0x55555555;

	/* Write the pattern to the power-of-two offset locations*/
	for(offset=1; offset < 4; offset <<=1)
		addr[offset] = pattern;

	test_offset = 0;

	/* Write anti-pattern to the below location */
	addr[test_offset] = anti_pattern;

	/* All the address lines are set to 0 as we written pattern to offset
	 * Read the data ie.pattern from the power-of-two offset locations which has one address bit set to high
	 * If the pattern is re-written to anti-pattern to any one of the power-of-two offset locations
	 stuck-at high fault is detected at that address bit*/

	for(offset =1 ;offset < 4; offset <<=1){
		temp = addr[offset];
		if(temp !=pattern){
			printf("\nFAILURE: Address bit stuck high"
					"@ 0x%.8lx: expected 0x%.8lx,"
					" actual 0x%.8lx\n",
					(uint)addr + offset*sizeof(vu_long),
					pattern, temp);
                                al_ps_err++;
		}
	}
	addr[test_offset] = pattern;

	/*
	 * Check for addr bits stuck low or shorted.
	 */
	for (test_offset = 1; test_offset < 4; test_offset <<= 1) {
		addr[test_offset] = anti_pattern;

		for (offset = 1; offset < 4; offset <<= 1) {
			temp = addr[offset];
			if ((temp != pattern) && (offset != test_offset)) {
				printf("\nFAILURE: Address bit stuck low or"
						" shorted @ 0x%.8lx: expected 0x%.8lx,"
						" actual 0x%.8lx\n",
						(uint)addr + offset*sizeof(vu_long),
						pattern, temp);
                                al_ps_err++;
			}
		}
		addr[test_offset] = pattern;
	}
    printf("PS DDR Address line test is done with ");
    printf("%d errors\r\n",al_ps_err);

    return 0;
}

static struct mmc *init_mmc_device(int dev, bool force_init)
{
	struct mmc *mmc;
	mmc = find_mmc_device(dev);
	if (!mmc) {
		printf("no mmc device at slot %x\n", dev);
		return NULL;
	}

	if (!mmc_getcd(mmc))
		force_init = true;

	if (force_init)
		mmc->has_init = 0;
	if (mmc_init(mmc))
		return NULL;

	return mmc;
}

char *zynqmp_get_silicon_idcode_name(void); /* Borrowed from zynqmp.c */

static void print_zynqmp_cpu(void)
{
	char *zname;

#if CONFIG_IS_ENABLED(FPGA) && defined(CONFIG_FPGA_ZYNQMPPL)
	zname = zynqmp_get_silicon_idcode_name();
	printf("\tChip ID:\t%s\n", zname);
#else
	printf("Please enable CONFIG_IS_ENABLED(FPGA) & CONFIG_FPGA_ZYNQMPPL\n");
#endif
	return;
}

#define NUM_EMMC_DEVS 2

static void print_eridan_mmc_info(void)
{
	struct mmc *mmc;

	for (int dev = 0; dev < NUM_EMMC_DEVS; dev++) {
		mmc = init_mmc_device(dev, false);
		if (!mmc)
			continue;
			//return CMD_RET_FAILURE;

		//print_mmcinfo(mmc);
		printf("\n");
		printf("\tDevice: %s\n", mmc->cfg->name);
		printf("\tManufacturer ID: %x\n", mmc->cid[0] >> 24);
		printf("\tOEM: %x\n", (mmc->cid[0] >> 8) & 0xffff);
		printf("\tName: %c%c%c%c%c \n", mmc->cid[0] & 0xff,
				(mmc->cid[1] >> 24), (mmc->cid[1] >> 16) & 0xff,
				(mmc->cid[1] >> 8) & 0xff, mmc->cid[1] & 0xff);

		printf("\tBus Speed: %d\n", mmc->clock);
#if CONFIG_IS_ENABLED(MMC_VERBOSE)
			printf("\tMode: %s\n", mmc_mode_name(mmc->selected_mode));
			mmc_dump_capabilities("card capabilities", mmc->card_caps);
			mmc_dump_capabilities("host capabilities", mmc->host_caps);
#else
			printf("Please enable MMC_Verbose in config\n");
#endif
	}
}

u8 zynqmp_get_bootmode(void); /* Borrowed from zynqmp.c */

static void print_uboot_check(void)
{
	u8 bootmode;
	bootmode = zynqmp_get_bootmode();

	puts("Bootmode: ");
	switch (bootmode) {
		case USB_MODE:
			puts("USB Boot\n");
			break;
		case JTAG_MODE:
			puts("JTAG Boot\n");
			break;
		case QSPI_MODE_24BIT:
		case QSPI_MODE_32BIT:
			puts("QSPI Boot\n");
			break;
		case EMMC_MODE:
			puts("EMMC Boot\n");
			break;
		case SD_MODE:
			puts("SD Boot\n");
			break;
		case SD1_LSHFT_MODE:
			puts("LVL_SHFT ");
			/* fall through */
		case SD_MODE1:
			puts("SD_MODE1\n");
			break;
		case NAND_MODE:
			puts("NAND Boot\n");
			break;
		default:
			puts("Unknown Boot\n");
	}

	return;
}

void print_eridan_board_info(void)
{
	print_uboot_check();
#ifdef CONFIG_ZYNQMP_IWG30M_H
	printf("IWG Board config found\n");
	print_zynqmp_cpu();
#else
	printf("IWG board config NOT Found\n");
#endif
	print_eridan_mmc_info();
}

int do_ec_test(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
    char op[2];

#if DBG
    printf("Doing Eridan POST tests\n");
#else
    printf("Starting Eridan POST debug test\n");
#endif

	print_eridan_board_info();
    op[0] = argv[1][0];
    if (op[0] == 'm') {
        do_eridan_mem_data_test(cmdtp, flag, argc, argv);
        do_eridan_mem_address_test(cmdtp, flag, argc, argv);
    } else if (op[0] == 's') {
        printf("SD Card test\n");
    } else if (op[0] == 'e') {
        printf("eMMC test\n");
    } else if (op[0] == 'c') {
        printf("Caladan Tests\n");
    }

    return 0;
}

U_BOOT_CMD(
        ectest,  2,      1,      do_ec_test,
        "Eridan Custom POST",
        "memory   - Memory Tests\n"
        "ectest sdcard   - Test for SD card\n"
        "ectest emmc     - eMMC flash\n"
        "ectest 10g      - 10Gig Ethernet\n"
        "ectest 1g       - 1Gig Ethernet\n"
        "ectest caladan  - Caladan Interface\n"
        "ectest fpga     - Eridan FPGA checks\n"
        ""
);
