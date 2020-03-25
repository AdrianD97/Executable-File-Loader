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

/* TODO: Sa nu uit sa verific fisierele .h(loader.h, exec_parser.h) cu cele din repo-ul temei*/

#define INVALID_SEGMENT	-1
#define PAGE_SIZE		0x10000

static so_exec_t *exec;

/* handle-ul care identifica instanta de fisier deschisa */
static HANDLE file_handle;

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

/* citeste un numar de bytes din fiserul executabil
 * si ii salveaza la dresa addr
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

	if (addr + size > addr_helper)
		size = addr_helper - addr;

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

/* descrie implementarea handler-ului pentru exceptia cauzata de un acces invalid la memorie */
// static LONG WINAPI sigsegv_sig_handler(struct _EXCEPTION_POINTERS *except_info)
static LONG CALLBACK sigsegv_sig_handler(PEXCEPTION_POINTERS except_info)
{
	int seg_index, nr_pages;
	int page_index;
	uintptr_t page_addr, addr;
	LPVOID ret;
	DWORD flags;
	BOOL res;
	DWORD old_prot;

	if (except_info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION
		&& except_info->ExceptionRecord->ExceptionCode != EXCEPTION_DATATYPE_MISALIGNMENT)
		return EXCEPTION_CONTINUE_SEARCH;

	addr = (uintptr_t)except_info->ExceptionRecord->ExceptionInformation[1];

	seg_index = get_segment_index(addr);

	if (seg_index == INVALID_SEGMENT) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if (!exec->segments[seg_index].data) {
		nr_pages = ceil_(exec->segments[seg_index].mem_size * 1.0f / PAGE_SIZE * 1.0f);

		exec->segments[seg_index].data = calloc(nr_pages, sizeof(uint8_t));
		DIE(!exec->segments[seg_index].data, "calloc failed.");
	}

	/* calculam indexul paginii din cadrul segmentului identificat
	 * prin seg_index .
	 */
	page_index = (addr - exec->segments[seg_index].vaddr) / PAGE_SIZE;
	if (*((uint8_t *)exec->segments[seg_index].data + page_index)) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	/* calculam adresa de inceput a paginii de memorie */
	page_addr = exec->segments[seg_index].vaddr + page_index * PAGE_SIZE;

	flags = MEM_COMMIT | MEM_RESERVE;
	ret = VirtualAlloc((LPVOID)page_addr, PAGE_SIZE, flags, PAGE_READWRITE);
	DIE(ret == NULL, "VirtualAlloc failed.");

	read_data(&exec->segments[seg_index], page_addr, PAGE_SIZE);
	res = VirtualProtect((LPVOID)page_addr, PAGE_SIZE, get_permissions(exec->segments[seg_index].perm), &old_prot);
	DIE(res == FALSE, "VirtualProtect failed");

	*((uint8_t *)exec->segments[seg_index].data + page_index) = 1;

	return EXCEPTION_CONTINUE_EXECUTION;
}

static void record_sigsegv_sig_handler()
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