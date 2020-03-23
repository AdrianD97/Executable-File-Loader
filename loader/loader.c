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
#include <math.h> /* va trebui sa leg la compilare biblioteca -lm */

/* TODO: Sa nu uit sa verific fisierele .h(loader.h, exec_parser.h) cu cele din repo-ul temei*/

#include "exec_parser.h"
#include "utils.h"

#define INVALID_SEGMENT	-1

typedef struct _page_info {
	uintptr_t addr;
	uint8_t mapped;
} page_info_t;

typedef struct _seg_info {
	int nr_pages;
	page_info_t *pages;
} seg_info_t;

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
	uintptr_t addr_start = addr;
	uintptr_t addr_helper = segment->vaddr + segment->file_size;
	int length = size;

	if (addr < addr_helper)
		addr_start += addr_helper - addr;

	addr_helper = segment->vaddr + segment->mem_size;
	if (addr + size > addr_helper)
		length = addr_helper - addr_start;

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
	void *start_addr = (void *)addr;

	if (addr + size > addr_helper)
		size = addr_helper - addr;

	offset += addr - segment->vaddr;
	pos = lseek(file_descriptor, offset, SEEK_SET);
	DIE(pos < 0, "lseek failed");
	
	while (size > 0) {
		bytes_read = read(file_descriptor, start_addr, size);
		DIE(bytes_read < 0, "read failed");
		size -= bytes_read;
		index += bytes_read;
		printf("index after read = %d", index);
		DIE(0 == 9, "Am iesit eu");
	}
}

/* descrie implementarea handler-ului pentru semnalul SIGSEGV cand are loc un page fault */
static void sigsegv_sig_handler(int signum, siginfo_t *info, void *ucont)
{
	int seg_index, nr_pages;
	int page_size, page_index;
	uintptr_t page_addr;
	void *ret;
	int flags;

	if (signum != SIGSEGV)
		return;

	seg_index = get_segment_index((uintptr_t)info->si_addr);

	if (seg_index == INVALID_SEGMENT) {
		sigsegv_sig_default_handler(SIGSEGV);
		return; /* cred ca nu este necesara aceasta instructiune pentru ca default inseamna opreste programul */
	}

	// /* obtinem dimensiunea unei pagini de memorie */
	page_size = getpagesize();
	// /* eu va trebui sa calculez si adresa paginii(prima adresa din pagina)
	//  * ma gandeam daca as putea sa retin pentru fiecare pagina prima adresa si starea paginii.
	//  */
	// /* adresa inceput = base + index_page * size_page*/
	// /* aici de unde stiu eu ca data este NULL,
	// va trebui sa initializez cu NULL in fisierul de parsare care creeza exec */
	if (!exec->segments[seg_index].data) {
		exec->segments[seg_index].data = calloc(1, sizeof(seg_info_t));
		DIE(!exec->segments[seg_index].data, "calloc failed.");
		nr_pages = (int)ceil(exec->segments[seg_index].mem_size * 1.0f / page_size);

		(*(seg_info_t *)exec->segments[seg_index].data).nr_pages = nr_pages;
		(*(seg_info_t *)exec->segments[seg_index].data).pages = (page_info_t *)calloc(nr_pages, sizeof(page_info_t));
		DIE(!(*(seg_info_t *)exec->segments[seg_index].data).pages, "calloc failed.");
	}

	// /* calculam indexul paginii din cadrul segmentului identificat
	//  * prin seg_index .
	//  */
	page_index = ((uintptr_t)info->si_addr - exec->segments[seg_index].vaddr) / page_size;
	if ((*(seg_info_t *)exec->segments[seg_index].data).pages[page_index].mapped) {
		sigsegv_sig_default_handler(SIGSEGV);
		return;  // cred ca nu este necesara aceasta instructiune pentru ca default inseamna opreste programul 
	}

	// /* calculam adresa de inceput a paginii de memorie */
	page_addr = exec->segments[seg_index].vaddr + page_index * page_size;
	// /* salvam adresa paginii pentru a putea demapa ulterior pagina */
	(*(seg_info_t *)exec->segments[seg_index].data).pages[page_index].addr = page_addr;
	// /*
	//  * 1. mapez la adresa calcultata memorie virtuala
	//  * 2. zeroiesc memoria (daca page_add + size > file_size
	//  	  de la file_size pana la page_adr+page_size)
	//  * 3. citesc din fisier continutul paginii(deci identific in cadrul
	//    segmentului din fisier pagina si citesc cate date am nevoie
	//    pentru ultima pagina s-ar putea sa nu fie nevoie sa citesc o pagina intreaga
	//    deci dimensiune citita = if page_add + pagesie < file_size citesc o pagina
	//    							else citesc file_size - page_addr)

	//  */
	// /* TODO: use mmap to map a page into memory */
	flags = MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS;
	/* TODO: La protectie avem o problema pentru ca eu daca aloc cu protectiile din segment
	 * de exemplu este doar pentru citire si executie,
	 * atunci eu nu o sa pot sa scriu in aceasta zona -> deci ce facem ?*/
	ret = mmap((void *)page_addr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0); // exec->segments[seg_index].perm, flags, -1, 0);
	DIE(ret == MAP_FAILED, "mmap failed.");
	// /* TODO: call zero_memory() */
	zero_memory(&exec->segments[seg_index], page_addr, page_size);
	// /* TODO: call read_data() */
	read_data(&exec->segments[seg_index], page_addr, page_size);

	(*(seg_info_t *)exec->segments[seg_index].data).pages[page_index].mapped = 1;
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

static void free_segments_memory()
{
	/* TODO: Va trebui sa demapez paginile mapate  si dupa sa eliberez 
	 * memoria pentru vectorul data
	 */
	unsigned int i, j;
	int ret;
	int size = getpagesize();

	for (i = 0; i < exec->segments_no; ++i) {
		if (exec->segments[i].data) {
			for (j = 0; j < (*(seg_info_t *)exec->segments[i].data).nr_pages; ++j) {
				if ((*(seg_info_t *)exec->segments[i].data).pages[i].mapped) {
					ret = munmap((void *)(*(seg_info_t *)exec->segments[i].data).pages[i].addr, size);
					DIE(ret < 0, "munmap failed");
				}
			}

			free(exec->segments[i].data);
		}
	}
}

static void free_memory()
{
	int ret = close(file_descriptor);
	DIE(ret < 0, "close failed.");

	free_segments_memory();
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

	/* TODO: va trebui sa deschid fisierul pentru citire din el */
	file_descriptor = open(path, O_RDONLY);
	DIE(file_descriptor < 0, "open failed.");

	so_start_exec(exec, argv);

	free_memory();

	return -1;
}
