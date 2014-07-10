#ifndef __MAPPING_H__
#define __MAPPING_H__

/**
 * Map a file into the current processes address space. Mapping is read/write
 * Returns a pointer to the start of the mapped address space
 */
char* map_file(char const * filename);

/**
 * To be called once the mapping is finished with.
 * Unmaps the mapping, closes the mapping object (WIN32), and closes the file
 * handle/descriptor
 *
 * Must be called with the same thread that called map_file
 */
void unmap_file(char* map_address);

#endif /* __MAPPING_H__ */
