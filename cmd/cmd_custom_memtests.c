/* This file contains the custom DDR memory tests as a part of POST
 *
 * The Memory tests will be done for 
 * PS DDR : Data Line Integrity Test, Address Line Integrity Test and Pattern Test[Fixed Range]
 * 
 */

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

#define HPS 0 
#define DBG 0 
#define START_ADDR 0x10000000
#define END_ADDR   0x7f000000

int do_dl_test(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
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
}

int do_al_test(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
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
}

static ulong mem_test_alt(vu_long *buf, ulong start_addr, ulong end_addr,
		vu_long *dummy)
{
	vu_long *addr;
	ulong errs = 0;
	ulong val, readback;
	int j;
	vu_long offset;
	vu_long test_offset;
	vu_long pattern;
	vu_long temp;
	vu_long anti_pattern;
	vu_long num_words;
	static const ulong bitpattern[] = {
		0x00000001,	/* single bit */
		0x00000003,	/* two adjacent bits */
		0x00000007,	/* three adjacent bits */
		0x0000000F,	/* four adjacent bits */
		0x00000005,	/* two non-adjacent bits */
		0x00000015,	/* three non-adjacent bits */
		0x00000055,	/* four non-adjacent bits */
		0xaaaaaaaa,	/* alternating 1/0 */
	};

	num_words = (end_addr - start_addr) / sizeof(vu_long);

	/*
	 * Data line test: write a pattern to the first
	 * location, write the 1's complement to a 'parking'
	 * address (changes the state of the data bus so a
	 * floating bus doesn't give a false OK), and then
	 * read the value back. Note that we read it back
	 * into a variable because the next time we read it,
	 * it might be right (been there, tough to explain to
	 * the quality guys why it prints a failure when the
	 * "is" and "should be" are obviously the same in the
	 * error message).
	 *
	 * Rather than exhaustively testing, we test some
	 * patterns by shifting '1' bits through a field of
	 * '0's and '0' bits through a field of '1's (i.e.
	 * pattern and ~pattern).
	 */
	addr = buf;
	for (j = 0; j < sizeof(bitpattern) / sizeof(bitpattern[0]); j++) {
		val = bitpattern[j];
		for (; val != 0; val <<= 1) {
			*addr = val;
			*dummy  = ~val; /* clear the test data off the bus */
			readback = *addr;
			if (readback != val) {
				printf("FAILURE (data line): "
						"expected %08lx, actual %08lx\n",
						val, readback);
				errs++;
				if (ctrlc())
					return -1;
			}
			*addr  = ~val;
			*dummy  = val;
			readback = *addr;
			if (readback != ~val) {
				printf("FAILURE (data line): "
						"Is %08lx, should be %08lx\n",
						readback, ~val);
				errs++;
				if (ctrlc())
					return -1;
			}
		}
	}

	/*
	 * Based on code whose Original Author and Copyright
	 * information follows: Copyright (c) 1998 by Michael
	 * Barr. This software is placed into the public
	 * domain and may be used for any purpose. However,
	 * this notice must not be changed or removed and no
	 * warranty is either expressed or implied by its
	 * publication or distribution.
	 */

	/*
	 * Address line test

	 * Description: Test the address bus wiring in a
	 *              memory region by performing a walking
	 *              1's test on the relevant bits of the
	 *              address and checking for aliasing.
	 *              This test will find single-bit
	 *              address failures such as stuck-high,
	 *              stuck-low, and shorted pins. The base
	 *              address and size of the region are
	 *              selected by the caller.

	 * Notes:	For best results, the selected base
	 *              address should have enough LSB 0's to
	 *              guarantee single address bit changes.
	 *              For example, to test a 64-Kbyte
	 *              region, select a base address on a
	 *              64-Kbyte boundary. Also, select the
	 *              region size as a power-of-two if at
	 *              all possible.
	 *
	 * Returns:     0 if the test succeeds, 1 if the test fails.
	 */
	pattern = (vu_long) 0xaaaaaaaa;
	anti_pattern = (vu_long) 0x55555555;

	pr_debug("%s:%d: length = 0x%.8lx\n", __func__, __LINE__, num_words);
	/*
	 * Write the default pattern at each of the
	 * power-of-two offsets.
	 */
	for (offset = 1; offset < num_words; offset <<= 1)
		addr[offset] = pattern;

	/*
	 * Check for address bits stuck high.
	 */
	test_offset = 0;
	addr[test_offset] = anti_pattern;

	for (offset = 1; offset < num_words; offset <<= 1) {
		temp = addr[offset];
		if (temp != pattern) {
			printf("\nFAILURE: Address bit stuck high @ 0x%.8lx:"
					" expected 0x%.8lx, actual 0x%.8lx\n",
					start_addr + offset*sizeof(vu_long),
					pattern, temp);
			errs++;
			if (ctrlc())
				return -1;
		}
	}
	addr[test_offset] = pattern;
	WATCHDOG_RESET();

	/*
	 * Check for addr bits stuck low or shorted.
	 */
	for (test_offset = 1; test_offset < num_words; test_offset <<= 1) {
		addr[test_offset] = anti_pattern;

		for (offset = 1; offset < num_words; offset <<= 1) {
			temp = addr[offset];
			if ((temp != pattern) && (offset != test_offset)) {
				printf("\nFAILURE: Address bit stuck low or"
						" shorted @ 0x%.8lx: expected 0x%.8lx,"
						" actual 0x%.8lx\n",
						start_addr + offset*sizeof(vu_long),
						pattern, temp);
				errs++;
				if (ctrlc())
					return -1;
			}
		}
		addr[test_offset] = pattern;
	}

	/*
	 * Description: Test the integrity of a physical
	 *		memory device by performing an
	 *		increment/decrement test over the
	 *		entire region. In the process every
	 *		storage bit in the device is tested
	 *		as a zero and a one. The base address
	 *		and the size of the region are
	 *		selected by the caller.
	 *
	 * Returns:     0 if the test succeeds, 1 if the test fails.
	 */
	num_words++;

	/*
	 * Fill memory with a known pattern.
	 */
	for (pattern = 1, offset = 0; offset < num_words; pattern++, offset++) {
		WATCHDOG_RESET();
		addr[offset] = pattern;
	}

	/*
	 * Check each location and invert it for the second pass.
	 */
	for (pattern = 1, offset = 0; offset < num_words; pattern++, offset++) {
		WATCHDOG_RESET();
		temp = addr[offset];
		if (temp != pattern) {
			printf("\nFAILURE (read/write) @ 0x%.8lx:"
					" expected 0x%.8lx, actual 0x%.8lx)\n",
					start_addr + offset*sizeof(vu_long),
					pattern, temp);
			errs++;
			if (ctrlc())
				return -1;
		}

		anti_pattern = ~pattern;
		addr[offset] = anti_pattern;
	}

	/*
	 * Check each location for the inverted pattern and zero it.
	 */
	for (pattern = 1, offset = 0; offset < num_words; pattern++, offset++) {
		WATCHDOG_RESET();
		anti_pattern = ~pattern;
		temp = addr[offset];
		if (temp != anti_pattern) {
			printf("\nFAILURE (read/write): @ 0x%.8lx:"
					" expected 0x%.8lx, actual 0x%.8lx)\n",
					start_addr + offset*sizeof(vu_long),
					anti_pattern, temp);
			errs++;
			if (ctrlc())
				return -1;
		}
		addr[offset] = 0;
	}

	return 0;
}

static ulong mem_test_quick(vu_long *buf, ulong start_addr, ulong end_addr,
		vu_long pattern, int iteration)
{
	vu_long *end;
	vu_long *addr;
	ulong errs = 0;
	ulong incr, length;
	ulong val, readback;

	/* Alternate the pattern */
	incr = 1;
	if (iteration & 1) {
		incr = -incr;
		/*
		 * Flip the pattern each time to make lots of zeros and
		 * then, the next time, lots of ones.  We decrement
		 * the "negative" patterns and increment the "positive"
		 * patterns to preserve this feature.
		 */
		if (pattern & 0x80000000)
			pattern = -pattern;	/* complement & increment */
		else
			pattern = ~pattern;
	}
	length = (end_addr - start_addr) / sizeof(ulong);
	end = buf + length;
	printf("\rPattern %08lX  Writing..."
			"%12s"
			"\b\b\b\b\b\b\b\b\b\b",
			pattern, "");
	for (addr = buf, val = pattern; addr < end; addr++) {
		WATCHDOG_RESET();
		*addr = val;
		val += incr;
	}

	puts("Reading...");

	for (addr = buf, val = pattern; addr < end; addr++) {
		WATCHDOG_RESET();
		readback = *addr;
		if (readback != val) {
			ulong offset = addr - buf;

			printf("\nMem error @ 0x%08X: "
					"found %08lX, expected %08lX\n",
					(uint)(uintptr_t)(start_addr + offset*sizeof(vu_long)),
					readback, val);
			errs++;
			if (ctrlc())
				return -1;
		}
		val += incr;
	}

	return 0;
}

/*
 * Perform a memory test. A more complete alternative test can be
 * configured using CONFIG_SYS_ALT_MEMTEST. The complete test loops until
 * interrupted by ctrl-c or by a failure of one of the sub-tests.
 */
static int do_prtn_test(struct cmd_tbl *cmdtp, int flag, int argc,
		char * const argv[])
{
	ulong start, end;
	vu_long *buf, *dummy;
	int iteration_limit;
	int ret,fpga_cs;
	ulong errs = 0;	/* number of errors, or -1 if interrupted */
	ulong pattern;
	int iteration;
	int *fpga_addr_sel_pio;

#if defined(CONFIG_SYS_ALT_MEMTEST)
	const int alt_test = 1;
#else
	const int alt_test = 0;
#endif
	if (argc <= 3){
		printf("--help\r\nptrn_test start end pattern\r\nEx: ptrn_test 0x10000000 0x10000010 0xAAAABBBB\r\n");
		return 1;
	}
	else
		start = simple_strtoul(argv[1], NULL, 16);

		end = simple_strtoul(argv[2], NULL, 16);

		pattern = (ulong)simple_strtoul(argv[3], NULL, 16);
		
		iteration_limit = 1;
	if ( start >= START_ADDR && end < END_ADDR)
		printf("Pattern test for DDR memory..\n");
	else{
		errs = -1UL;
		return -1;
	}	
	
	printf("Testing %08x ... %08x:\n", (uint)start, (uint)end);
	pr_debug("%s:%d: start %#08lx end %#08lx\n", __func__, __LINE__,
			start, end);

	buf = map_sysmem(start, end - start);
	dummy = map_sysmem(SYS_MEMTEST_SCRATCH, sizeof(vu_long));
	for (iteration = 0;
			!iteration_limit || iteration < iteration_limit;
			iteration++) {
		if (ctrlc()) {
			errs = -1UL;
			break;
		}
#if DBG

		printf("Iteration: %6d\r", iteration + 1);
		pr_debug("\n");

#endif
		if (alt_test) {
			errs = mem_test_alt(buf, start, end, dummy);
		} else {
			errs = mem_test_quick(buf, start, end, pattern,
					iteration);
		}
		if (errs == -1UL)
			break;
	}

	/*
	 * Work-around for eldk-4.2 which gives this warning if we try to
	 * case in the unmap_sysmem() call:
	 * warning: initialization discards qualifiers from pointer target type
	 */
	{
		void *vbuf = (void *)buf;
		void *vdummy = (void *)dummy;

		unmap_sysmem(vbuf);
		unmap_sysmem(vdummy);
	}

	if (errs == -1UL) {
		/* Memory test was aborted - write a newline to finish off */
		putc('\n');
		ret = 1;
	} else {
		if(errs > 0)
		printf("Tested %d iteration(s) with %lu errors.\n",
				iteration, errs);
		else
		printf("Test is done with %lu errors.\n", errs);
		ret = errs != 0;
	}

	return ret;	/* not reached */
}

U_BOOT_CMD(
        dl_test,  1,      1,      do_dl_test,
        "data line integrity test for PS DDR",
        ""
);

U_BOOT_CMD(al_test, 1, 1, do_al_test, "Address line integrity test for PS DDR", "");

U_BOOT_CMD(ptrn_test, 4, 1, do_prtn_test, "simple RAM pattern test", "start end pattern");
