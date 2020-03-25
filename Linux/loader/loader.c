/*
 * Loader Implementation
 *
 * 2018, Operating Systems
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/* TODO: Sa nu uit sa verific fisierele .h(loader.h, exec_parser.h) cu cele din repo-ul temei*/

#include "loader.h"
#include "exec_parser.h"
#include "utils.h"

#define INVALID_SEGMENT	-1

static so_exec_t *exec;

/* va retine default handler-ul semnalului SIGSEGV */
static void (*sigsegv_sig_default_handler)(int);

/* file descriptor-ul care identifica instanta de fisier deschisa */
static int file_descriptor;

/* intoarce index-ul segmentului din care face parte addr sau
 * INVALID_SEGMENT daca adresa nu se gaseste in nici-un segment
 */
static int get_segment_index(uintptr_t addr)
{
	unsigned int start, end;

	start = 0;
	end = exec->segments_no - 1;

	while (start <= end) {
		if (addr >= exec->segments[start].vaddr && 
			addr < (exec->segments[start].vaddr
			+ exec->segments[start].mem_size))
			return start;

		if (addr >= exec->segments[end].vaddr && 
			addr < (exec->segments[end].vaddr
			+ exec->segments[end].mem_size))
			return end;

		++start;
		--end;
	}

	return INVALID_SEGMENT;
}

/* zeroieste o zona de memorie de o anumita lungime */
void zero_memory(so_seg_t *segment, uintptr_t addr, int size)
{
	char *addr_start = (char *)addr;
	char * addr_helper = (char *)segment->vaddr + segment->file_size;
	int length = size;
	
	if ((char *)addr + size < addr_helper)
		return;

	if (addr < (uintptr_t)addr_helper)
		addr_start += addr_helper - (char *)addr;

	addr_helper = (char *)segment->vaddr + segment->mem_size;
	if ((char *)addr + size > addr_helper)
		length = addr_helper - addr_start;
	else
		length = ((char *)addr + size) - addr_start;

	memset((void *)addr_start, 0, length);
}

/* citeste un numar de bytes din fiserul executabil
 * si ii salveaza la dresa addr
 */
void read_data(so_seg_t *segment, uintptr_t addr, int size)
{
	int bytes_read;
	uintptr_t addr_helper = segment->vaddr + segment->file_size;
	int index = 0;
	unsigned int offset = segment->offset;
	off_t pos;
	char *start_addr = (char *)addr;

	if (addr + size > addr_helper)
		size = addr_helper - addr;

	offset += addr - segment->vaddr;
	pos = lseek(file_descriptor, offset, SEEK_SET);
	DIE(pos < 0, "lseek failed");
	
	while (size > 0) {
		bytes_read = read(file_descriptor, start_addr + index, size);
		DIE(bytes_read < 0, "read failed");
		size -= bytes_read;
		index += bytes_read;
	}
}

/* descrie implementarea handler-ului pentru semnalul SIGSEGV cand are loc un page fault */
static void sigsegv_sig_handler(int signum, siginfo_t *info, void *ucont)
{
	int seg_index, nr_pages;
	int page_size, page_index;
	uintptr_t page_addr;
	void *ret;
	int flags, res;

	if (signum != SIGSEGV)
		return;

	seg_index = get_segment_index((uintptr_t)info->si_addr);

	if (seg_index == INVALID_SEGMENT) {
		sigsegv_sig_default_handler(SIGSEGV);
		return;
	}

	/* obtinem dimensiunea unei pagini de memorie */
	page_size = getpagesize();

	if (!exec->segments[seg_index].data) {
		nr_pages = ceil_(exec->segments[seg_index].mem_size * 1.0f / page_size * 1.0f);

		exec->segments[seg_index].data = calloc(nr_pages, sizeof(uint8_t));
		DIE(!exec->segments[seg_index].data, "calloc failed.");
	}

	/* calculam indexul paginii din cadrul segmentului identificat
	 * prin seg_index .
	 */
	page_index = ((uintptr_t)info->si_addr - exec->segments[seg_index].vaddr) / page_size;
	if (*((uint8_t *)exec->segments[seg_index].data + page_index)) {
		sigsegv_sig_default_handler(SIGSEGV);
		return;
	}

	/* calculam adresa de inceput a paginii de memorie */
	page_addr = exec->segments[seg_index].vaddr + page_index * page_size;

	flags = MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS;
	ret = mmap((void *)page_addr, page_size, PROT_WRITE, flags, -1, 0);
	DIE(ret == MAP_FAILED, "mmap failed.");
	
	zero_memory(&exec->segments[seg_index], page_addr, page_size);

	read_data(&exec->segments[seg_index], page_addr, page_size);

	res = mprotect((void *)page_addr, page_size, exec->segments[seg_index].perm);
	DIE(res < 0, "mprotect failed");

	*((uint8_t *)exec->segments[seg_index].data + page_index) = 1;
}

static void record_sigsegv_sig_handler()
{
	struct sigaction action, old_action;
	int ret;

	memset(&action, 0, sizeof(struct sigaction));
	action.sa_flags = SA_SIGINFO;

	action.sa_sigaction = sigsegv_sig_handler;
	ret = sigaction(SIGSEGV, &action, &old_action);
	DIE(ret == -1, "sigaction failed.");
	sigsegv_sig_default_handler = old_action.sa_handler;
}

int so_init_loader(void)
{	
	record_sigsegv_sig_handler();

	return -1;
}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	file_descriptor = open(path, O_RDONLY);
	DIE(file_descriptor < 0, "open failed.");
	
	so_start_exec(exec, argv);

	return -1;
}
