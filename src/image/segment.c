/*
 * Copyright (C) 2007 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * You can also choose to distribute this program under the terms of
 * the Unmodified Binary Distribution Licence (as given in the file
 * COPYING.UBDL), provided that you have satisfied its requirements.
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

/**
 * @file
 *
 * Executable image segments
 *
 */

#include <assert.h>
#include <errno.h>
#include <ipxe/uaccess.h>
#include <ipxe/io.h>
#include <ipxe/errortab.h>
#include <ipxe/segment.h>

/**
 * Segment-specific error messages
 *
 * This error happens sufficiently often to merit a user-friendly
 * description.
 */
#define ERANGE_SEGMENT __einfo_error ( EINFO_ERANGE_SEGMENT )
#define EINFO_ERANGE_SEGMENT \
	__einfo_uniqify ( EINFO_ERANGE, 0x01, "Requested memory not available" )
struct errortab segment_errors[] __errortab = {
	__einfo_errortab ( EINFO_ERANGE_SEGMENT ),
};

#ifdef EFIAPI

EFI_MEMORY_DESCRIPTOR efi_mmap[100];

void dump_map(void)
{
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	EFI_STATUS efirc;
	UINTN size = sizeof (efi_mmap);
	UINTN key;
	UINTN desc_size;
	size_t i;
	size_t nr;

	efirc = bs->GetMemoryMap(&size, efi_mmap, &key, &desc_size, NULL);

	if (efirc != 0) {
		DBG ( "GetMemoryMap failed with %d\n", (int) efirc);
		return;
	}

	nr = size / desc_size;

	for (i = 0; i < nr; i++) {
		EFI_MEMORY_DESCRIPTOR *p = &efi_mmap[i];
		DBG ( "[%lx] type %x attr %lx phys %lx virt %lx size %lx\n",
		    i, p->Type, (long)p->Attribute, (long)p->PhysicalStart, (long)p->VirtualStart,
		    (long) p->NumberOfPages * 4096 );
	}
}

/**
 * Prepare segment for loading
 *
 * @v segment		Segment start
 * @v filesz		Size of the "allocated bytes" portion of the segment
 * @v memsz		Size of the segment
 * @ret rc		Return status code
 */
int prep_segment ( userptr_t segment, size_t filesz, size_t memsz ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	unsigned int pages;
    EFI_PHYSICAL_ADDRESS phys_addr;
	physaddr_t start = user_to_phys ( segment, 0 );
	physaddr_t mid = user_to_phys ( segment, filesz );
	physaddr_t end = user_to_phys ( segment, memsz );
	EFI_STATUS efirc;

	DBG ( "Preparing segment [%lx,%lx,%lx)\n", start, mid, end );

	/* Sanity check */
	if ( filesz > memsz ) {
		DBG ( "Insane segment [%lx,%lx,%lx)\n", start, mid, end );
		return -EINVAL;
	}

	/* Start address of the segment so that we know where to allocate from */
	phys_addr = start;
	/* Size of the segment in pages */
	pages = EFI_SIZE_TO_PAGES ( memsz );
	/* Allocate the memory via EFI to ensure its reserved */
	if ( ( efirc = bs->AllocatePages ( AllocateAddress,
				EfiLoaderData,
				pages,
				&phys_addr ))  != 0 ) {
		/* No suitable memory region found */
		DBG ( "Segment [%lx,%lx,%lx) does not fit into available memory: %d\n",
				start, mid, end, (int)efirc );
                dump_map();
		return -ERANGE_SEGMENT;
	}

	assert ( phys_to_user ( phys_addr ) == segment );

	/* Found valid region: zero bss and return */
	memset_user ( segment, filesz, 0, ( memsz - filesz ) );
	return 0;
}
#else
/**
 * Prepare segment for loading
 *
 * @v segment		Segment start
 * @v filesz		Size of the "allocated bytes" portion of the segment
 * @v memsz		Size of the segment
 * @ret rc		Return status code
 */
int prep_segment ( userptr_t segment, size_t filesz, size_t memsz ) {
	struct memory_map memmap;
	physaddr_t start = user_to_phys ( segment, 0 );
	physaddr_t mid = user_to_phys ( segment, filesz );
	physaddr_t end = user_to_phys ( segment, memsz );
	unsigned int i;

	DBG ( "Preparing segment [%lx,%lx,%lx)\n", start, mid, end );

	/* Sanity check */
	if ( filesz > memsz ) {
		DBG ( "Insane segment [%lx,%lx,%lx)\n", start, mid, end );
		return -EINVAL;
	}

	/* Get a fresh memory map.  This allows us to automatically
	 * avoid treading on any regions that Etherboot is currently
	 * editing out of the memory map.
	 */
	get_memmap ( &memmap );

	/* Look for a suitable memory region */
	for ( i = 0 ; i < memmap.count ; i++ ) {
		if ( ( start >= memmap.regions[i].start ) &&
		     ( end <= memmap.regions[i].end ) ) {
			/* Found valid region: zero bss and return */
			memset_user ( segment, filesz, 0, ( memsz - filesz ) );
			return 0;
		}
	}

	/* No suitable memory region found */
	DBG ( "Segment [%lx,%lx,%lx) does not fit into available memory\n",
	      start, mid, end );
	return -ERANGE_SEGMENT;
}
#endif /* EFIAPI */
