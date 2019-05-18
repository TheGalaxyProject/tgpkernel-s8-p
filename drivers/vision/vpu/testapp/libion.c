#include <asm/fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "ion.h"

#ifdef FPGA
#define DEFAULT_NODE_ATTRIBUTE      O_RDONLY
#else
#define DEFAULT_NODE_ATTRIBUTE      O_RDWR
#endif

typedef unsigned long ion_handle;

struct ion_allocation_data {
	size_t len;
	size_t align;
    unsigned int heap_mask;
	unsigned int flags;
	ion_handle handle;
};

struct ion_fd_data {
	ion_handle handle;
	int fd;
};

struct ion_handle_data {
	ion_handle handle;
};

struct ion_custom_data {
	unsigned int cmd;
	unsigned long arg;
};

#define ION_IOC_MAGIC	'I'
#define ION_IOC_ALLOC	_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE	_IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_MAP     _IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)
#define ION_IOC_SHARE   _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_IMPORT  _IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data)
#define ION_IOC_CUSTOM  _IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)
#define ION_IOC_SYNC	_IOWR(ION_IOC_MAGIC, 7, struct ion_fd_data)

struct ion_msync_data {
	long flags;
	ion_buffer buf;
	size_t size;
	off_t offset;
};

enum ION_EXYNOS_CUSTOM_CMD {
	ION_EXYNOS_CUSTOM_MSYNC
};

ion_client ion_client_create(void)
{
	return open("/dev/ion", DEFAULT_NODE_ATTRIBUTE);
}

void ion_client_destroy(ion_client client)
{
	close(client);
}

ion_buffer ion_alloc(ion_client client, size_t len, size_t align,
                     unsigned int heap_mask, unsigned int flags)
{
    int ret;
    struct ion_handle_data arg_free;
    struct ion_fd_data arg_share;
    struct ion_allocation_data arg_alloc;

    arg_alloc.len = len;
    arg_alloc.align = align;
    arg_alloc.heap_mask = heap_mask;
    arg_alloc.flags = flags;

	ret = ioctl(client, ION_IOC_ALLOC, &arg_alloc);
	if (ret < 0)
		return ret;

	arg_share.handle = arg_alloc.handle;
	ret = ioctl(client, ION_IOC_SHARE, &arg_share);

	arg_free.handle = arg_alloc.handle;
	ioctl(client, ION_IOC_FREE, &arg_free);

	if (ret < 0)
		return ret;

	return arg_share.fd;
}

void ion_free(ion_buffer buffer)
{
	close(buffer);
}

void *ion_map(ion_buffer buffer, size_t len, off_t offset)
{
    return mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED,
                buffer, offset);
}

int ion_unmap(void *addr, size_t len)
{
    return munmap(addr, len);
}

int ion_sync(ion_client client, ion_buffer buffer)
{
    struct ion_fd_data data;

    data.fd = buffer;

    return ioctl(client, ION_IOC_SYNC, &data);
}
