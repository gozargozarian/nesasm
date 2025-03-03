/*
 *  MagicKit assembler
 *  ----
 *  This program was originaly a 6502 assembler written by J. H. Van Ornum,
 *  it has been modified and enhanced to support the PC Engine and NES consoles.
 *
 *  This program is freeware. You are free to distribute, use and modifiy it
 *  as you wish.
 *
 *  Enjoy!
 *  ----
 *  Original 6502 version by:
 *    J. H. Van Ornum
 *
 *  PC-Engine version by:
 *    David Michel
 *    Dave Shadoff
 *
 *  NES version by:
 *    Charles Doty
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "defs.h"
#include "externs.h"
#include "protos.h"
#include "vars.h"
#include "inst.h"

/* defines */
#define STANDARD_CD	1
#define SUPER_CD	2

/* variables */
unsigned char ipl_buffer[4096];
char   in_fname[128];	/* file names, input */
char  out_fname[128];	/* output */
char  bin_fname[128];	/* binary */
char  lst_fname[128];	/* listing */
char  fns_fname[128];	/* functions */
char *prg_name;	/* program name */
FILE *in_fp;	/* file pointers, input */
FILE *lst_fp;	/* listing */
char  section_name[4][8] = { "  ZP", " BSS", "CODE", "DATA" };
int   dump_seg;
int   header_opt;
int   run_opt;
int   mlist_opt;	/* macro listing main flag */
int   xlist;		/* listing file main flag */
int   list_level;	/* output level */
int   asm_opt[8];	/* assembler options */


/* ----
 * main()
 * ----
 */

int
main(int argc, char **argv)
{
	FILE *fp, *ipl;
	char *p;
	char  cmd[80];
	int i, j;
	int file;
	int ram_bank;

	/* get program name */
	if ((prg_name = strrchr(argv[0], '/')) != NULL)
		 prg_name++;
	else {
		if ((prg_name = strrchr(argv[0], '\\')) == NULL)
			 prg_name = argv[0];
		else
			 prg_name++;
	}	

	/* remove extension */
	if ((p = strrchr(prg_name, '.')) != NULL)
		*p = '\0';

    machine = &nes;

	/* init assembler options */
	list_level = 2;
	header_opt = 1;
	mlist_opt = 0;
	run_opt = 0;
	file = 0;

	/* display assembler version message */
    printf("%s\n\n", machine->asm_title);

	/* parse command line */
	if (argc > 1) {
		for (i = 1; i < argc; i++) {
			if (argv[i][0] == '-') {
				/* segment usage */
				if (!strcmp(argv[i], "-s"))
					dump_seg = 1;
				else if (!strcmp(argv[i], "-S"))
					dump_seg = 2;

				/* forces macros expansion */
				else if (!strcmp(argv[i], "-m"))
					mlist_opt = 1;

				/* no header */
				else if (!strcmp(argv[i], "-raw"))
					header_opt = 0;

				/* output level */
				else if (!strncmp(argv[i], "-l", 2)) {
					/* get level */
					if (strlen(argv[i]) == 2)
						list_level = atol(argv[++i]);
					else
						list_level = atol(&argv[i][2]);

					/* check range */
					if (list_level < 0 || list_level > 3)
						list_level = 2;
				}

				/* help */
				else if (!strcmp(argv[i], "-?")) {
					help();
					return (0);
				}
			}
			else {
				strcpy(in_fname, argv[i]);
				file++;
			}
		}
	}
	if (!file) {
		help();
		return (0);
	}

	/* search file extension */
	if ((p = strrchr(in_fname, '.')) != NULL) {
		if (!strchr(p, PATH_SEPARATOR))
		   *p = '\0';
		else
			p = NULL;
	}

	/* auto-add file extensions */
	strcpy(out_fname, in_fname);
	strcpy(bin_fname, in_fname);
	strcpy(lst_fname, in_fname);
	strcpy(fns_fname, in_fname);
	strcat(bin_fname, machine->rom_ext);
	strcat(lst_fname, ".lst");
	strcat(fns_fname, ".fns");
        

	if (p)
	   *p = '.';
	else
		strcat(in_fname, ".asm");

	/* init include path */
	init_path();

	/* init crc functions */
	crc_init();

	/* open the input file */
	if (open_input(in_fname)) {
		printf("Can not open input file '%s'!\n", in_fname);
		exit(1);
	}

	/* clear the ROM array */
	memset(rom, 0xFF, 8192 * 128);
	memset(map, 0xFF, 8192 * 128);

	/* clear symbol hash tables */
	for (i = 0; i < 256; i++) {
		hash_tbl[i]  = NULL;
		macro_tbl[i] = NULL;
		func_tbl[i]  = NULL;
		inst_tbl[i]  = NULL;
	}

	/* fill the instruction hash table */
	addinst(base_inst);
	addinst(base_pseudo);

	/* add machine specific instructions and pseudos */
	addinst(machine->inst);
	addinst(machine->pseudo_inst);

	/* predefined symbols */
	lablset("MAGICKIT", 1);
	lablset("_bss_end", 0);
	lablset("_bank_base", 0);
	lablset("_nb_bank", 1);
	lablset("_call_bank", 0);

	/* init global variables */
	max_zp = 0x01;
	max_bss = 0x0201;
	max_bank = 0;
	rom_limit = 0x100000;		/* 1MB */
	bank_limit = 0x7F;
	bank_base = 0;
	errcnt = 0;

	/* assemble */
	for (pass = FIRST_PASS; pass <= LAST_PASS; pass++) {
		infile_error = -1;
		page = 7;
		bank = 0;
		loccnt = 0;
		slnum = 0;
		mcounter = 0;
		mcntmax = 0;
		xlist = 0;
		glablptr = NULL;
		skip_lines = 0;
		rsbase = 0;
		proc_nb = 0;

		/* reset assembler options */
		asm_opt[OPT_LIST] = 0;
		asm_opt[OPT_MACRO] = mlist_opt;
		asm_opt[OPT_WARNING] = 0;
		asm_opt[OPT_OPTIMIZE] = 0;

		/* reset bank arrays */
		for (i = 0; i < 4; i++) {
			for (j = 0; j < 256; j++) {
				bank_loccnt[i][j] = 0;
				bank_glabl[i][j]  = NULL;
				bank_page[i][j]   = 0;
			}
		}

		/* reset sections */
		ram_bank = machine->ram_bank;
		section  = S_CODE;

		/* .zp */
		section_bank[S_ZP]           = ram_bank;
		bank_page[S_ZP][ram_bank]    = machine->ram_page;
		bank_loccnt[S_ZP][ram_bank]  = 0x0000;

		/* .bss */
		section_bank[S_BSS]          = ram_bank;
		bank_page[S_BSS][ram_bank]   = machine->ram_page;
		bank_loccnt[S_BSS][ram_bank] = 0x0200;

		/* .code */
		section_bank[S_CODE]         = 0x00;
		bank_page[S_CODE][0x00]      = 0x07;
		bank_loccnt[S_CODE][0x00]    = 0x0000;

		/* .data */
		section_bank[S_DATA]         = 0x00;
		bank_page[S_DATA][0x00]      = 0x07;
		bank_loccnt[S_DATA][0x00]    = 0x0000;

		/* pass message */
		printf("pass %i\n", pass + 1);

		/* assemble */
		while (readline() != -1) {
			assemble();
			if (loccnt > 0x2000) {
				if (proc_ptr == NULL)
					fatal_error("Bank overflow, offset > $1FFF!");
				else {
					char tmp[128];

					sprintf(tmp, "Proc : '%s' is too large (code > 8KB)!", proc_ptr->name);
					fatal_error(tmp);
				}
				break;
			}
			if (stop_pass)
				break;
		}

		/* relocate procs */
		if (pass == FIRST_PASS)
			proc_reloc();

		/* abord pass on errors */
		if (errcnt) {
			printf("# %d error(s)\n", errcnt);
			break;
		}

		/* adjust bank base */
		if (pass == FIRST_PASS)
			bank_base = calc_bank_base();

		/* update predefined symbols */
		if (pass == FIRST_PASS) {
			lablset("_bss_end", machine->ram_base + max_bss);
			lablset("_bank_base", bank_base);
			lablset("_nb_bank", max_bank + 1);
		}

		/* rewind input file */
		rewind(in_fp);

		/* open the listing file */
		if (pass == FIRST_PASS) {
			if (xlist && list_level) {
				if ((lst_fp = fopen(lst_fname, "w")) == NULL) {
					printf("Can not open listing file '%s'!\n", lst_fname);
					exit(1);
				}
				fprintf(lst_fp, "#[1]   %s\n", input_file[1].name);
			}
		}
	}

	/* rom */
	if (errcnt == 0) {
		/* save binary file */

        /* open file */
        if ((fp = fopen(bin_fname, "wb")) == NULL) {
            printf("Can not open binary file '%s'!\n", bin_fname);
            exit(1);
        }

        /* write header */
        if (header_opt)
            machine->write_header(fp, max_bank + 1);

        /* write rom */
        fwrite(rom, 8192, (max_bank + 1), fp);
        fclose(fp);
	}

	/* close listing file */
	if (xlist && list_level)
		fclose(lst_fp);

	/* close input file */
	fclose(in_fp);

	/* dump the bank table */
	if (dump_seg)
		show_seg_usage();

	/* GrP dump function addresses */
	funcdump(fns_fname, in_fname);

	/* ok */
	return(0);
}


/* ----
 * calc_bank_base()
 * ----
 * calculate rom bank base
 */

int
calc_bank_base(void)
{
	int base;

	/* default */
    base = 0;

	return (base);
}


/* ----
 * help()
 * ----
 * show assembler usage
 */

void
help(void)
{
	/* check program name */
	if (strlen(prg_name) == 0)
		prg_name = machine->asm_name;

	/* display help */
	printf("%s [-options] [-? (for help)] infile\n\n", prg_name);
	printf("-s/S   : show segment usage\n");
	printf("-l #   : listing file output level (0-3)\n");
	printf("-m     : force macro expansion in listing\n");
	printf("-raw   : prevent adding a ROM header\n");
	printf("infile : file to be assembled\n");
}


/* ----
 * show_seg_usage()
 * ----
 */

void
show_seg_usage(void)
{
	int i, j;
	int addr, start, stop, nb;
	int rom_used;
	int rom_free;
	int ram_base = machine->ram_base;

	printf("segment usage:\n");
	printf("\n");

	/* zp usage */
	if (max_zp <= 1)
		printf("      ZP    -\n");
	else {
		start = ram_base;
		stop  = ram_base + (max_zp - 1);
		printf("      ZP    $%04X-$%04X  [%4i]\n", start, stop, stop - start + 1);
	}

	/* bss usage */
	if (max_bss <= 0x201)
		printf("     BSS    -\n");
	else {
		start = ram_base + 0x200;
		stop  = ram_base + (max_bss - 1);
		printf("     BSS    $%04X-$%04X  [%4i]\n", start, stop, stop - start + 1);
	}

	/* bank usage */
	rom_used = 0;
	rom_free = 0;

	if (max_bank)
		printf("\t\t\t\t    USED/FREE\n");

	/* scan banks */
	for (i = 0; i <= max_bank; i++) {
		start = 0;
		addr = 0;
		nb = 0;

		/* count used and free bytes */
		for (j = 0; j < 8192; j++)
			if (map[i][j] != 0xFF)
				nb++;

		/* display bank infos */
		if (nb)			
			printf("BANK% 4i    %20s    %4i/%4i\n",
					i, bank_name[i], nb, 8192 - nb);
		else {
			printf("BANK% 4i    %20s       0/8192\n", i, bank_name[i]);
			continue;
		}

		/* update used/free counters */
		rom_used += nb;
		rom_free += 8192 - nb;

		/* scan */
		if (dump_seg == 1)
			continue;

		for (;;) {
			/* search section start */
			for (; addr < 8192; addr++)
				if (map[i][addr] != 0xFF)
					break;

			/* check for end of bank */
			if (addr > 8191)
				break;

			/* get section type */
			section = map[i][addr] & 0x0F;
			page = (map[i][addr] & 0xE0) << 8;
			start = addr;

			/* search section end */
			for (; addr < 8192; addr++)
				if ((map[i][addr] & 0x0F) != section)
					break;

			/* display section infos */
			printf("    %s    $%04X-$%04X  [%4i]\n",
					section_name[section],	/* section name */
				    start + page,			/* starting address */
					addr  + page - 1,		/* end address */
					addr  - start);			/* size */
		}
	}

	/* total */
	rom_used = (rom_used + 511) >> 10;
	rom_free = (rom_free + 511) >> 10;
	printf("\t\t\t\t    ---- ----\n");
	printf("\t\t\t\t    %4iK%4iK\n", rom_used, rom_free);
}

