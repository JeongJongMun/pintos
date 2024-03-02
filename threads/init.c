#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#endif
#include "tests/threads/tests.h"
#ifdef VM
#include "vm/vm.h"
#endif
#ifdef FILESYS
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* Page-map-level-4 with kernel mappings only. */
uint64_t *base_pml4;

#ifdef FILESYS
/* -f: Format the file system? */
static bool format_filesys;
#endif

/* -q: Power off after kernel tasks complete? */
bool power_off_when_done;

bool thread_tests;

static void bss_init(void);
static void paging_init(uint64_t mem_end);

static char **read_command_line(void);
static char **parse_options(char **argv);
static void run_actions(char **argv);
static void usage(void);

static void print_stats(void);

int main(void) NO_RETURN;

/* Pintos main program. */
/*
 * main : 실제 커널의 초기화와 수행을 담당하는 함수
 */
int main(void)
{
	printf("main() called\n");
	uint64_t mem_end;
	char **argv;

	/* Clear BSS and get machine's RAM size.
	 * BSS : 초기화되지 않은 전역 변수들을 저장하는 메모리 영역
	 */
	bss_init();

	// 명령줄을 인수와 구문 분석 옵션으로 나누기
	argv = read_command_line();
	argv = parse_options(argv);

	// 잠금을 사용할 수 있도록 스레드로 초기화한 다음 콘솔 잠금을 사용하도록 설정합니다.
	thread_init();
	console_init();

	// 메모리 시스템 초기화
	mem_end = palloc_init();
	malloc_init();
	paging_init(mem_end);

#ifdef USERPROG
	tss_init(); // task state segment 초기화
	gdt_init(); // global descriptor table 초기화
#endif

	// 인터럽트 핸들러 초기화
	intr_init(); // PIC(Programmable Interrupt Controller), idt(Interrupt Descriptor Table) 초기화
	// PIC는 인터럽트 장치에 연결되어 CPU에 인터럽트를 전달하는 역할을 한다.
	// idt는 인터럽트가 발생했을 때 어떤 인터럽트 핸들러를 호출할지를 결정하는 테이블이다.
	timer_init(); 
	kbd_init();
	input_init();
#ifdef USERPROG
	exception_init();
	syscall_init();
#endif
	// 스레드 스케줄러 시작 및 인터럽트 활성화
	thread_start(); // 가장 실행 우선 순위가 낮은 idle이라는 스레드를 생성하고 실행한다.
	serial_init_queue();
	timer_calibrate();

#ifdef FILESYS
	/* Initialize file system. */
	disk_init();
	filesys_init(format_filesys);
#endif

#ifdef VM
	vm_init();
#endif

	printf("Boot complete.\n");

	/* Run actions specified on kernel command line. */
	run_actions(argv);

	/* Finish up. */
	if (power_off_when_done)
		power_off();
	thread_exit();
}

/* Clear BSS */
static void bss_init(void)
{
	/* "BSS"는 0으로 초기화해야 하는 세그먼트입니다.
	   실제로 디스크에 저장되거나 커널 로더에 의해 0으로 초기화되지 않으므로 직접 0으로 초기화해야 합니다.
	   BSS 세그먼트의 시작과 끝은 링커에 의해 _start_bss 및 _end_bss로 기록됩니다.  kernel.lds를 참조하세요.
	*/
	extern char _start_bss, _end_bss;
	memset(&_start_bss, 0, &_end_bss - &_start_bss);
}

/* Populates the page table with the kernel virtual mapping,
 * and then sets up the CPU to use the new page directory.
 * Points base_pml4 to the pml4 it creates. */
static void paging_init(uint64_t mem_end)
{
	uint64_t *pml4, *pte;
	int perm;
	pml4 = base_pml4 = palloc_get_page(PAL_ASSERT | PAL_ZERO);

	extern char start, _end_kernel_text;
	// Maps physical address [0 ~ mem_end] to
	//   [LOADER_KERN_BASE ~ LOADER_KERN_BASE + mem_end].
	for (uint64_t pa = 0; pa < mem_end; pa += PGSIZE)
	{
		uint64_t va = (uint64_t)ptov(pa);

		perm = PTE_P | PTE_W;
		if ((uint64_t)&start <= va && va < (uint64_t)&_end_kernel_text)
			perm &= ~PTE_W;

		if ((pte = pml4e_walk(pml4, va, 1)) != NULL)
			*pte = pa | perm;
	}

	// reload cr3
	pml4_activate(0);
}

/* Breaks the kernel command line into words and returns them as
   an argv-like array. */
static char **read_command_line(void)
{
	static char *argv[LOADER_ARGS_LEN / 2 + 1];
	char *p, *end;
	int argc;
	int i;

	argc = *(uint32_t *)ptov(LOADER_ARG_CNT);
	p = ptov(LOADER_ARGS);
	end = p + LOADER_ARGS_LEN;
	for (i = 0; i < argc; i++)
	{
		if (p >= end)
			PANIC("command line arguments overflow");

		argv[i] = p;
		p += strnlen(p, end - p) + 1;
	}
	argv[argc] = NULL;

	/* Print kernel command line. */
	printf("Kernel command line:");
	for (i = 0; i < argc; i++)
		if (strchr(argv[i], ' ') == NULL)
			printf(" %s", argv[i]);
		else
			printf(" '%s'", argv[i]);
	printf("\n");

	return argv;
}

/* Parses options in ARGV[]
   and returns the first non-option argument. */
static char **parse_options(char **argv)
{
	for (; *argv != NULL && **argv == '-'; argv++)
	{
		char *save_ptr;
		char *name = strtok_r(*argv, "=", &save_ptr);
		char *value = strtok_r(NULL, "", &save_ptr);

		if (!strcmp(name, "-h"))
			usage();
		else if (!strcmp(name, "-q"))
			power_off_when_done = true;
#ifdef FILESYS
		else if (!strcmp(name, "-f"))
			format_filesys = true;
#endif
		else if (!strcmp(name, "-rs"))
			random_init(atoi(value));
		else if (!strcmp(name, "-mlfqs"))
			thread_mlfqs = true;
#ifdef USERPROG
		else if (!strcmp(name, "-ul"))
			user_page_limit = atoi(value);
		else if (!strcmp(name, "-threads-tests"))
			thread_tests = true;
#endif
		else
			PANIC("unknown option `%s' (use -h for help)", name);
	}

	return argv;
}

/* Runs the task specified in ARGV[1]. */
static void run_task(char **argv)
{
	printf("run_task() called\n");
	const char *task = argv[1];

	printf("Executing '%s':\n", task);
#ifdef USERPROG
	if (thread_tests)
	{
		run_test(task);
	}
	else
	{
		process_wait(process_create_initd(task));
	}
#else
	run_test(task);
#endif
	printf("Execution of '%s' complete.\n", task);
}

/* Executes all of the actions specified in ARGV[]
   up to the null pointer sentinel. */
static void run_actions(char **argv)
{
	printf("run_actions() called\n");
	/* An action. */
	struct action
	{
		char *name;					   /* Action name. */
		int argc;					   /* # of args, including action name. */
		void (*function)(char **argv); /* Function to execute action. */
	};

	/* Table of supported actions. */
	static const struct action actions[] = {
		{"run", 2, run_task},
#ifdef FILESYS
		{"ls", 1, fsutil_ls},
		{"cat", 2, fsutil_cat},
		{"rm", 2, fsutil_rm},
		{"put", 2, fsutil_put},
		{"get", 2, fsutil_get},
#endif
		{NULL, 0, NULL},
	};

	while (*argv != NULL)
	{
		const struct action *a;
		int i;

		/* Find action name. */
		for (a = actions;; a++)
			if (a->name == NULL)
				PANIC("unknown action `%s' (use -h for help)", *argv);
			else if (!strcmp(*argv, a->name))
				break;

		/* Check for required arguments. */
		for (i = 1; i < a->argc; i++)
			if (argv[i] == NULL)
				PANIC("action `%s' requires %d argument(s)", *argv, a->argc - 1);

		/* Invoke action and advance. */
		a->function(argv);
		argv += a->argc;
	}
}

/* Prints a kernel command line help message and powers off the
   machine. */
static void usage(void)
{
	printf("\nCommand line syntax: [OPTION...] [ACTION...]\n"
		   "Options must precede actions.\n"
		   "Actions are executed in the order specified.\n"
		   "\nAvailable actions:\n"
#ifdef USERPROG
		   "  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
		   "  run TEST           Run TEST.\n"
#endif
#ifdef FILESYS
		   "  ls                 List files in the root directory.\n"
		   "  cat FILE           Print FILE to the console.\n"
		   "  rm FILE            Delete FILE.\n"
		   "Use these actions indirectly via `pintos' -g and -p options:\n"
		   "  put FILE           Put FILE into file system from scratch disk.\n"
		   "  get FILE           Get FILE from file system into scratch disk.\n"
#endif
		   "\nOptions:\n"
		   "  -h                 Print this help message and power off.\n"
		   "  -q                 Power off VM after actions or on panic.\n"
		   "  -f                 Format file system disk during startup.\n"
		   "  -rs=SEED           Set random number seed to SEED.\n"
		   "  -mlfqs             Use multi-level feedback queue scheduler.\n"
#ifdef USERPROG
		   "  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif
	);
	power_off();
}

/* Powers down the machine we're running on,
   as long as we're running on Bochs or QEMU. */
void power_off(void)
{
#ifdef FILESYS
	filesys_done();
#endif

	print_stats();

	printf("Powering off...\n");
	outw(0x604, 0x2000); /* Poweroff command for qemu */
	for (;;)
		;
}

/* Print statistics about Pintos execution. */
static void print_stats(void)
{
	timer_print_stats();
	thread_print_stats();
#ifdef FILESYS
	disk_print_stats();
#endif
	console_print_stats();
	kbd_print_stats();
#ifdef USERPROG
	exception_print_stats();
#endif
}
