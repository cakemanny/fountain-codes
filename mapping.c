#define _GNU_SOURCE
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#else
#   include <sys/mman.h>    // mmap
#   include <sys/stat.h>    // stat
#   include <fcntl.h>      // open
#   include <unistd.h>     // close
#endif
#include <stdlib.h>     // perror
#include <stdio.h>      // printf

#include "mapping.h"

struct mapping_data {
    char* mapped_address;
    char const * filename;
#ifdef _WIN32
    DWORD size;
    HANDLE file_handle;
    HANDLE mapping_object;
#else
    size_t size;
    int filedes;
#endif
};

#ifdef _MSC_VER
#define thread_local __declspec(thread)
#else
#define thread_local __thread /* Use __thread rather than _Thread_local for
                                 compatibility with gcc-4.8 and lower */
#endif

static thread_local int num_mappings = 0;
static thread_local struct mapping_data mappings[32] = { };

static char* _map_file(const char * filename, int allow_write);

char* map_file(char const * filename)
{
    return _map_file(filename, 1);
}
char * map_file_read(char const * filename)
{
    return _map_file(filename, 0);
}

char* _map_file(char const * filename, int allow_write) {
    if (num_mappings >= 32)
        return NULL;
#ifdef _WIN32
    // Convert filename to wide string
    // Need at most two bytes per character
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    WCHAR wfilename[wlen];
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, wfilename, wlen);

	HANDLE file = CreateFileW(
		wfilename,		/* filename */
        GENERIC_READ | (-allow_write & GENERIC_WRITE), /* desired access */
		0,				/* Dont' share */
		NULL,			/* don't care about security */
		OPEN_EXISTING,		/* only work with existing files */
		FILE_ATTRIBUTE_NORMAL, /* Just normal thanks */
		NULL);			/* template file - nope */

	if (file == INVALID_HANDLE_VALUE)
		return NULL;

	DWORD filesize = GetFileSize(file, NULL);
	// Cannot map zero-length files
	if (filesize == INVALID_FILE_SIZE || filesize == 0) {
		CloseHandle(file);
		return NULL;
	}

    DWORD prot = allow_write ? PAGE_READWRITE : PAGE_READONLY;
	HANDLE mapping_object = CreateFileMappingA(file, NULL, prot, 0, 0, NULL);
	if (mapping_object == NULL) {
		CloseHandle(file);
		return NULL;
	}

    DWORD access = allow_write ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
	char* mapped = (char*)MapViewOfFile(mapping_object, access, 0, 0, 0);
	if (mapped == NULL) {
		CloseHandle(mapping_object);
		CloseHandle(file);
		return NULL;
	}

	struct mapping_data new_mapping = {
		.mapped_address = mapped,
		.filename = filename,
		.size = filesize,
		.file_handle = file,
        .mapping_object = mapping_object
	};
	mappings[num_mappings++] = new_mapping;

	return mapped;
#else
    int fd = open(filename, allow_write ? O_RDWR : O_RDONLY);
    if (fd < 0)
        return NULL;

    struct stat st;
    stat(filename, &st);
    size_t filesize = st.st_size;

    int prot = PROT_READ | (-allow_write & PROT_WRITE);
    char* mapped = (char*)mmap(NULL, filesize, prot, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    // Hurray success
    struct mapping_data new_mapping = {
        .mapped_address = mapped,
        .filename = filename,
        .size = filesize,
        .filedes = fd
    };
    mappings[num_mappings++] = new_mapping;

    return mapped;

#endif
}

void unmap_file(char* map_address) {
    for (int i = 0; i < num_mappings; i++) {
        if (map_address == mappings[i].mapped_address) {
#ifdef _WIN32
            UnmapViewOfFile(map_address);
            CloseHandle(mappings[i].mapping_object);
            CloseHandle(mappings[i].file_handle);
#else
            munmap(map_address, mappings[i].size);
            close(mappings[i].filedes);
#endif
            while (++i < num_mappings) {
                mappings[i - 1] = mappings[i];
            }
        }
    }
}

#ifdef RUN_TESTS
int main(int argc, char** argv) {

    FILE* f = fopen("testfile", "w");
    if (f != NULL) {
        fprintf(f, "Hello there my little friend\n");
        fclose(f);
    }

    char copy[50] = { };

    char* mapped = map_file("testfile");

    for (size_t i = 0; i < sizeof "Hello there my little friends\n" -1; i++)
        copy[i] = mapped[i];

    printf("Contents of mapped region:\n\t");
    printf("%s", copy);

    unmap_file(mapped);

#ifdef _WIN32
#   define unlink(x) DeleteFileA(x)
#endif
    unlink("testfile");

}
#endif

