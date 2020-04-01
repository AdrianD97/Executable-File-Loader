/*
 * Loader Implementation
 *
 * 2018, Operating Systems
 */

#include <stdio.h>
#include <Windows.h>
#include <signal.h>

#define DLL_EXPORTS
#include "loader.h"
#include "exec_parser.h"
#include "utils.h"

#define INVALID_SEGMENT	-1
#define PAGE_SIZE		0x10000

static so_exec_t *exec;

/* handle-ul care identifica instanta de fisier deschisa */
static HANDLE file_handle;

/*
 * intoarce index-ul segmentului din care face parte addr sau
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

/*
 * citeste un numar de bytes din fisierul executabil
 * si ii salveaza la adresa addr
 */
void read_data(so_seg_t *segment, uintptr_t addr, int size)
{
	DWORD bytes_read;
	uintptr_t addr_helper = segment->vaddr + segment->file_size;
	int index = 0;
	unsigned int offset = segment->offset;
	DWORD pos;
	BOOL ret;
	char *start_addr = (char *)addr;

	if (addr + size > addr_helper) {
		size = addr_helper - addr;
		if (size < 0)
			return;
	}

	offset += addr - segment->vaddr;
	pos = SetFilePointer(file_handle, offset, NULL, SEEK_SET);
	DIE(pos == INVALID_SET_FILE_POINTER, "SetFilePointer failed");

	while (size > 0) {
		ret = ReadFile(
			file_handle,
			start_addr + index,
			size,
			&bytes_read,
			NULL
		);

		DIE(ret == FALSE, "read failed");
		size -= bytes_read;
		index += bytes_read;
	}
}

/*
 * Calculeaza permisiunile unui segment echivalente sistemului
 * de operare Windows
 */
DWORD get_permissions(unsigned int seg_perm)
{
	DWORD perm = 0;

	if (seg_perm == PERM_R)
		return PAGE_READONLY;

	if (seg_perm == PERM_W)
		return PAGE_READWRITE;

	if (seg_perm == PERM_X)
		return PAGE_EXECUTE;

	if (seg_perm == (PERM_R | PERM_W))
		return PAGE_READWRITE;

	if (seg_perm == (PERM_R | PERM_X))
		return PAGE_EXECUTE_READ;

	return PAGE_NOACCESS;

}

/* descrie implementarea handler-ului pentru exceptia cauzata
 * de un acces invalid la memorie
 */
static LONG CALLBACK sigsegv_sig_handler(PEXCEPTION_POINTERS except_info)
{
	int seg_index, nr_pages;
	int page_index;
	uintptr_t page_addr, addr;
	LPVOID ret;
	DWORD flags;
	BOOL res;
	DWORD old_prot;
	EXCEPTION_RECORD *exc_rec;

	if (except_info->ExceptionRecord->ExceptionCode
		!= EXCEPTION_ACCESS_VIOLATION
		&& except_info->ExceptionRecord->ExceptionCode
		!= EXCEPTION_DATATYPE_MISALIGNMENT)
		return EXCEPTION_CONTINUE_SEARCH;

	exc_rec = except_info->ExceptionRecord;
	addr = (uintptr_t)exc_rec->ExceptionInformation[1];

	/*
	 * obtinem indexul segmentului din care face parte pagina care contine
	 * adresa care a cauzat exceptia
	 */
	seg_index = get_segment_index(addr);

	if (seg_index == INVALID_SEGMENT)
		return EXCEPTION_CONTINUE_SEARCH;

	if (!exec->segments[seg_index].data) {
		/* calculam numarul de pagini din segment */
		nr_pages = ceil_(exec->segments[seg_index].mem_size * 1.0f
						/ PAGE_SIZE * 1.0f);

		/* marcam initial toate paginile ca fiind nemapate */
		exec->segments[seg_index].data = calloc(nr_pages,
							sizeof(uint8_t));
		DIE(!exec->segments[seg_index].data, "calloc failed.");
	}

	/* calculam indexul paginii din cadrul segmentului identificat
	 * prin seg_index .
	 */
	page_index = (addr - exec->segments[seg_index].vaddr) / PAGE_SIZE;
	if (*((uint8_t *)exec->segments[seg_index].data + page_index))
		return EXCEPTION_CONTINUE_SEARCH;

	/* calculam adresa de inceput a paginii de memorie */
	page_addr = exec->segments[seg_index].vaddr + page_index * PAGE_SIZE;

	flags = MEM_COMMIT | MEM_RESERVE;
	/* alocam memorie */
	ret = VirtualAlloc((LPVOID)page_addr, PAGE_SIZE,
						flags, PAGE_READWRITE);
	DIE(ret == NULL, "VirtualAlloc failed.");

	/* citim datele paginii din fisierul executabil */
	read_data(&exec->segments[seg_index], page_addr, PAGE_SIZE);

	/*
	 * schimbam permisiunile paginii(pagina trebuie sa aiba aceleasi
	 * permisiunii ca segmentul din care face parte)
	 */
	res = VirtualProtect((LPVOID)page_addr, PAGE_SIZE,
			get_permissions(exec->segments[seg_index].perm),
			&old_prot);
	DIE(res == FALSE, "VirtualProtect failed");

	/* marcam in vectorul data ca pagina a fost mapata */
	*((uint8_t *)exec->segments[seg_index].data + page_index) = 1;

	/*
	 * semnalizam ca exceptia a fost tratata si se
	 * poate continua executia
	 */
	return EXCEPTION_CONTINUE_EXECUTION;
}

/* inregistreaza handler-ul */
static void record_sigsegv_sig_handler(void)
{
	PVOID sigsegv_handle;

	sigsegv_handle = AddVectoredExceptionHandler(1, sigsegv_sig_handler);
	DIE(sigsegv_handle == NULL, "AddVectoredExceptionHandler failed.");
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

	/*
	 * deschidem fisierul executabil pentru a putea citi ulterior
	 * datele paginilor din el
	 */
	file_handle = CreateFile(
		 (LPCSTR)path,
		 GENERIC_READ,
		 FILE_SHARE_READ | FILE_SHARE_WRITE,
		 NULL,
		 OPEN_EXISTING,
		 FILE_ATTRIBUTE_NORMAL,
		 NULL
	);
	DIE(file_handle == INVALID_HANDLE_VALUE, "CreateFile failed.");

	so_start_exec(exec, argv);

	return -1;
}
