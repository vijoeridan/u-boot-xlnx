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
#include <mapmem.h>

int do_ec_test(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
#if DBG
    printf("Doing Eridan POST tests\n");
#endif

    return 0;
}

U_BOOT_CMD(
        ectest,  1,      1,      do_ec_test,
        "Eridan Custom POST",
        ""
);
