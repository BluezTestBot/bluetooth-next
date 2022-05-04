/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FLEX_ARRAY_H_
#define _LINUX_FLEX_ARRAY_H_

#include <linux/string.h>
/*
 * A "flexible array structure" is a struct which ends with a flexible
 * array _and_ contains a member that represents how many array elements
 * are present in the flexible array structure:
 *
 * struct flex_array_struct_example {
 *	...		// arbitrary members
 *	u16 part_count;	// count of elements stored in "parts" below.
 *	..		// arbitrary members
 *	u32 parts[];	// flexible array with elements of type u32.
 * };
 *
 * Without the "count of elements" member, a structure ending with a
 * flexible array has no way to check its own size, and should be
 * considered just a blob of memory that is length-checked through some
 * other means. Kernel structures with flexible arrays should strive to
 * always be true flexible array structures so that they can be operated
 * on with the flex*()-family of helpers defined below.
 *
 * An "encapsulating flexible array structure" is a structure that contains
 * a full "flexible array structure" as its final struct member. These are
 * used frequently when needing to pass around a copy of a flexible array
 * structure, and track other things about the data outside of the scope of
 * the flexible array structure itself:
 *
 * struct encapsulating_example {
 *	...		// other members
 *	struct flex_array_struct_example fas;
 * };
 *
 * For bounds checking operations on a flexible array structure, member
 * aliases must be created so the helpers can always locate the associated
 * members. Marking up the examples above would look like this:
 *
 * struct flex_array_struct_example {
 *	...		// arbitrary members
 *	// count of elements stored in "parts" below.
 *	DECLARE_FLEX_ARRAY_ELEMENTS_COUNT(u16, part_count);
 *	..		// arbitrary members
 *	// flexible array with elements of type u32.
 *	DECLARE_FLEX_ARRAY_ELEMENTS(u32, parts);
 * };
 *
 * The above creates the aliases for part_count as __flex_array_elements_count
 * and parts as __flex_array_elements.
 *
 * For encapsulated flexible array structs, there are alternative helpers
 * below where the flexible array struct member name can be explicitly
 * included as an argument. (See the @dot_fas_member arguments below.)
 *
 *
 * Examples:
 *
 * Using mem_to_flex():
 *
 *        struct single {
 *                u32 flags;
 *                u32 count;
 *                u8 data[];
 *        };
 *        struct single *ptr_single;
 *
 *        struct encap {
 *                u16 info;
 *                struct single single;
 *        };
 *        struct encap *ptr_encap;
 *
 *        struct blob {
 *                u32 flags;
 *                u8 data[];
 *        };
 *
 *        struct split {
 *                u32 count;
 *                struct blob blob;
 *        };
 *        struct split *ptr_split;
 *
 *        mem_to_flex(ptr_one, src, count);
 *        __mem_to_flex(ptr_encap, single.data, single.count, src, count);
 *        __mem_to_flex(ptr_split, count, blob.data, src, count);
 *
 */

/* These are wrappers around the UAPI macros. */
#define DECLARE_FLEX_ARRAY_ELEMENTS_COUNT(TYPE, NAME)			\
	__DECLARE_FLEX_ARRAY_ELEMENTS_COUNT(TYPE, NAME)

#define DECLARE_FLEX_ARRAY_ELEMENTS(TYPE, NAME)				\
	__DECLARE_FLEX_ARRAY_ELEMENTS(TYPE, NAME)

/* All the helpers return negative on failure, as must be checked. */
static inline int __must_check __must_check_errno(int err)
{
	return err;
}

/**
 * __fas_elements_bytes - Calculate potential size of the flexible
 *			  array elements of a given flexible array
 *			  structure.
 *
 * @p: Pointer to flexible array structure.
 * @flex_member: Member name of the flexible array elements.
 * @count_member: Member name of the flexible array elements count.
 * @elements_count: Count of proposed number of @p->__flex_array_elements
 * @bytes: Pointer to variable to write calculation of total size in bytes.
 *
 * Returns: 0 on successful calculation, -ve on error.
 *
 * This performs the same calculation as flex_array_size(), except
 * that the result is bounds checked and written to @bytes instead
 * of being returned.
 */
#define __fas_elements_bytes(p, flex_member, count_member,		\
			     elements_count, bytes)			\
__must_check_errno(({							\
	int __feb_err = -EINVAL;					\
	size_t __feb_elements_count = (elements_count);			\
	size_t __feb_elements_max =					\
		type_max(typeof((p)->count_member));			\
	if (__feb_elements_count > __feb_elements_max ||		\
	    check_mul_overflow(sizeof(*(p)->flex_member),		\
			       __feb_elements_count, bytes)) {		\
		*(bytes) = 0;						\
		__feb_err = -E2BIG;					\
	} else {							\
		__feb_err = 0;						\
	}								\
	__feb_err;							\
}))

/**
 * fas_elements_bytes - Calculate current size of the flexible array
 *			elements of a given flexible array structure.
 *
 * @p: Pointer to flexible array structure.
 * @bytes: Pointer to variable to write calculation of total size in bytes.
 *
 * Returns: 0 on successful calculation, -ve on error.
 *
 * This performs the same calculation as flex_array_size(), except
 * that the result is bounds checked and written to @bytes instead
 * of being returned.
 */
#define fas_elements_bytes(p, bytes)					\
	__fas_elements_bytes(p, __flex_array_elements,			\
			     __flex_array_elements_count,		\
			     (p)->__flex_array_elements_count, bytes)

/** __fas_bytes - Calculate potential size of flexible array structure
 *
 * @p: Pointer to flexible array structure.
 * @flex_member: Member name of the flexible array elements.
 * @count_member: Member name of the flexible array elements count.
 * @elements_count: Count of proposed number of @p->__flex_array_elements
 * @bytes: Pointer to variable to write calculation of total size in bytes.
 *
 * Returns: 0 on successful calculation, -ve on error.
 *
 * This performs the same calculation as struct_size(), except
 * that the result is bounds checked and written to @bytes instead
 * of being returned.
 */
#define __fas_bytes(p, flex_member, count_member, elements_count, bytes)\
__must_check_errno(({							\
	int __fasb_err;							\
	typeof(*bytes) __fasb_bytes;					\
									\
	if (__fas_elements_bytes(p, flex_member, count_member,		\
				 elements_count, &__fasb_bytes) ||	\
	    check_add_overflow(sizeof(*(p)), __fasb_bytes, bytes)) {	\
		*(bytes) = 0;						\
		__fasb_err = -E2BIG;					\
	} else {							\
		__fasb_err = 0;						\
	}								\
	__fasb_err;							\
}))

/** fas_bytes - Calculate current size of flexible array structure
 *
 * @p: Pointer to flexible array structure.
 * @bytes: Pointer to variable to write calculation of total size in bytes.
 *
 * This performs the same calculation as struct_size(), except
 * that the result is bounds checked and written to @bytes instead
 * of being returned, using the current size of the flexible array
 * structure (via @p->__flexible_array_elements_count).
 *
 * Returns: 0 on successful calculation, -ve on error.
 */
#define fas_bytes(p, bytes)						\
	__fas_bytes(p, __flex_array_elements,				\
		    __flex_array_elements_count,			\
		    (p)->__flex_array_elements_count, bytes)

/** flex_cpy - Copy from one flexible array struct into another with count conversion
 *
 * @dst: Destination pointer
 * @src: Source pointer
 *
 * The full structure of @src will be copied to @dst, including all trailing
 * flexible array elements. @dst->__flex_array_elements_count must be large
 * enough to hold @src->__flex_array_elements_count. Any elements left over
 * in @dst will be zero-wiped.
 *
 * Returns: 0 on successful calculation, -ve on error.
 */
#define flex_cpy(dst, src) __must_check_errno(({			\
	int __fc_err = -EINVAL;						\
	typeof(*(dst)) *__fc_dst = (dst);				\
	typeof(*(src)) *__fc_src = (src);				\
	size_t __fc_dst_bytes, __fc_src_bytes;				\
									\
	BUILD_BUG_ON(!__same_type(*(__fc_dst), *(__fc_src)));		\
									\
	do {								\
		if (fas_bytes(__fc_dst, &__fc_dst_bytes) ||		\
		    fas_bytes(__fc_src, &__fc_src_bytes) ||		\
		    __fc_dst_bytes < __fc_src_bytes) {			\
			/* do we need to wipe dst here? */		\
			__fc_err = -E2BIG;				\
			break;						\
		}							\
		__builtin_memcpy(__fc_dst, __fc_src, __fc_src_bytes);	\
		/* __flex_array_elements_count is included in memcpy */	\
		/* Wipe any now-unused trailing elements in @dst: */	\
		__builtin_memset((u8 *)__fc_dst + __fc_src_bytes, 0,	\
				 __fc_dst_bytes - __fc_src_bytes);	\
		__fc_err = 0;						\
	} while (0);							\
	__fc_err;							\
}))

/** __flex_dup - Allocate and copy an arbitrarily encapsulated flexible
 *		 array struct
 *
 * @alloc: Pointer to Pointer to hold to-be-allocated (optionally
 *	   encapsulating) flexible array struct.
 * @dot_fas_member: For encapsulating flexible arrays, the name of the
 *		    flexible array struct member preceded with a literal
 *		    dot (e.g. .foo.bar.flex_array_struct_name). For a
 *		    regular flexible array struct, this macro arument is
 *		    empty.
 * @src: Pointer to source flexible array struct.
 * @gfp: GFP allocation flags
 *
 * This copies the contents of one flexible array struct into another.
 * The (**@alloc)@dot_fas_member and @src arguments must resolve to the
 * same type. Everything prior to @dot_fas_member in *@alloc will be
 * initialized to zero.
 *
 * Failure modes:
 * - @alloc is NULL.
 * - *@alloc is not NULL (something was already allocated).
 * - Required allocation size is larger than size_t can hold.
 * - No available memory to allocate @alloc.
 *
 * Returns: 0 on success, -ve on failure.
 */
#define __flex_dup(alloc, dot_fas_member, src, gfp)			\
__must_check_errno(({							\
	int __fd_err = -EINVAL;						\
	typeof(*(src)) *__fd_src = (src);				\
	typeof(**(alloc)) *__fd_alloc;					\
	typeof((*__fd_alloc)dot_fas_member) *__fd_dst;			\
	size_t __fd_alloc_bytes, __fd_copy_bytes;			\
									\
	BUILD_BUG_ON(!__same_type(*(__fd_dst), *(__fd_src)));		\
									\
	do {								\
		if ((uintptr_t)(alloc) < 1 || *(alloc)) {		\
			__fd_err = -EINVAL;				\
			break;						\
		}							\
		if (fas_bytes(__fd_src, &__fd_copy_bytes) ||		\
		    check_add_overflow(__fd_copy_bytes,			\
				       sizeof(*__fd_alloc) -		\
					sizeof(*__fd_dst),		\
				       &__fd_alloc_bytes)) {		\
			__fd_err = -E2BIG;				\
			break;						\
		}							\
		__fd_alloc = kmalloc(__fd_alloc_bytes, gfp);		\
		if (!__fd_alloc) {					\
			__fd_err = -ENOMEM;				\
			break;						\
		}							\
		__fd_dst = &((*__fd_alloc)dot_fas_member);		\
		/* Optimize away any unneeded memset. */		\
		if (sizeof(*__fd_alloc) != sizeof(*__fd_dst))		\
			__builtin_memset(__fd_alloc, 0,			\
					 __fd_alloc_bytes -		\
						__fd_copy_bytes);	\
		__builtin_memcpy(__fd_dst, src, __fd_copy_bytes);	\
		/* __flex_array_elements_count is included in memcpy */	\
		*(alloc) = __fd_alloc;					\
		__fd_err = 0;						\
	} while (0);							\
	__fd_err;							\
}))

/** flex_dup - Allocate and copy a flexible array struct
 *
 * @alloc: Pointer to Pointer to hold to-be-allocated flexible array struct.
 * @src: Pointer to source flexible array struct.
 * @gfp: GFP allocation flags
 *
 * This copies the contents of one flexible array struct into another.
 * The *@alloc and @src arguments must resolve to the same type.
 *
 * Failure modes:
 * - @alloc is NULL.
 * - *@alloc is not NULL (something was already allocated).
 * - Required allocation size is larger than size_t can hold.
 * - No available memory to allocate @alloc.
 *
 * Returns: 0 on success, -ve on failure.
 */
#define flex_dup(alloc, src, gfp)					\
	__flex_dup(alloc, /* alloc itself */, src, gfp)

/** __mem_to_flex - Copy from memory buffer into a flexible array structure's
 *		    flexible array elements.
 *
 * @ptr: Pointer to already allocated flexible array struct.
 * @flex_member: Member name of the flexible array elements.
 * @count_member: Member name of the flexible array elements count.
 * @src: Source memory pointer.
 * @elements_count: Number of @ptr's flexible array elements to copy from
 *		    @src into @ptr.
 *
 * Copies @elements_count-many elements from memory buffer at @src into
 * @ptr->@flex_member, wipes any remaining elements, and updates
 * @ptr->@count_member.
 *
 * This is essentially a simple deserializer.
 *
 * TODO: It would be nice to automatically discover the max bounds of @src
 *	 besides @elements_count. There is currently no universal way to ask
 *	 "what is the size of a given pointer's allocation?" So for
 *	 now just use __builtin_object_size(@src, 1) to validate known
 *	 compile-time too-large conditions. Perhaps in the future if
 *	 __mtf_copy_bytes above is > PAGE_SIZE, perform a dynamic lookup
 *	 using something similar to __check_heap_object().
 *
 * Failure conditions:
 * - The value of @elements_count cannot fit in the @ptr's @count_member
 *   type (e.g. 260 in a u8).
 * - @ptr's @count_member value is smaller than @elements_count (e.g. not
 *   enough space was previously allocated).
 * - @elements_count yields a byte count greater than:
 *   - INT_MAX (as a simple "too big" sanity check)
 *   - the compile-time size of @src (when it can be determined)
 *
 * Returns: 0 on success, -ve on error.
 */
#define __mem_to_flex(ptr, flex_member, count_member, src,		\
		      elements_count)					\
__must_check_errno(({							\
	int __mtf_err = -EINVAL;					\
	typeof(*(ptr)) *__mtf_ptr = (ptr);				\
	typeof(elements_count) __mtf_src_count = (elements_count);	\
	size_t __mtf_copy_bytes, __mtf_dst_bytes;			\
	u8 *__mtf_dst = (u8 *)__mtf_ptr->flex_member;			\
									\
	do {								\
		if (is_negative(__mtf_src_count) ||			\
		    __fas_elements_bytes(__mtf_ptr, flex_member,	\
					 count_member,			\
					 __mtf_src_count,		\
					 &__mtf_copy_bytes) ||		\
		    __mtf_copy_bytes > INT_MAX ||			\
		    __mtf_copy_bytes > __builtin_object_size(src, 1) ||	\
		    __fas_elements_bytes(__mtf_ptr, flex_member,	\
					 count_member,			\
					 __mtf_ptr->count_member,	\
					 &__mtf_dst_bytes) ||		\
		    __mtf_dst_bytes < __mtf_copy_bytes) {		\
			__mtf_err = -E2BIG;				\
			break;						\
		}							\
		__builtin_memcpy(__mtf_dst, src, __mtf_copy_bytes);	\
		/* Wipe any now-unused trailing elements in @dst: */	\
		__builtin_memset(__mtf_dst + __mtf_dst_bytes, 0,	\
				 __mtf_dst_bytes - __mtf_copy_bytes);	\
		/* Make sure in-struct count of elements is updated: */	\
		__mtf_ptr->count_member = __mtf_src_count;		\
		__mtf_err = 0;						\
	} while (0);							\
	__mtf_err;							\
}))

#define mem_to_flex(ptr, src, elements_count)				\
	__mem_to_flex(ptr, __flex_array_elements,			\
		      __flex_array_elements_count, src, elements_count)

/** __mem_to_flex_dup - Allocate a flexible array structure and copy into
 *			its flexible array elements from a memory buffer.
 *
 * @alloc: Pointer to pointer to hold allocation for flexible array struct.
 * @dot_fas_member: For encapsulating flexible array structs, the name of
 *		    the flexible array struct member preceded with a
 *		    literal dot (e.g. .foo.bar.flex_array_struct_name).
 *		    For a regular flexible array struct, this macro arument
 *		    is empty.
 * @src: Source memory buffer pointer.
 * @elements_count: Number of @alloc's flexible array elements to copy from
 *		    @src into @ptr.
 * @gfp: GFP allocation flags
 *
 * This behaves like mem_to_flex(), but allocates the needed space for
 * a new flexible array struct and its trailing elements.
 *
 * This is essentially a simple allocating deserializer.
 *
 * TODO: It would be nice to automatically discover the max bounds of @src
 *	 besides @elements_count. There is currently no universal way to ask
 *	 "what is the size of a given pointer's allocation?" So for now just
 *	 use __builtin_object_size(@src, 1) to validate known compile-time
 *	 too-large conditions. Perhaps in the future if __mtfd_copy_bytes
 *	 above is > PAGE_SIZE, perform a dynamic lookup using something
 *	 similar to __check_heap_object().
 *
 * Failure conditions:
 * - @alloc is NULL.
 * - *@alloc is not NULL (something was already allocated).
 * - The value of @elements_count cannot fit in the @alloc's
 *   __flex_array_elements_count member type (e.g. 260 in u8).
 * - @elements_count yields a byte count greater than:
 *   - INT_MAX (as a simple "too big" sanity check)
 *   - the compile-time size of @src (when it can be determined)
 * - @alloc could not be allocated.
 *
 * Returns: 0 on success, -ve on error.
 */
#define __mem_to_flex_dup(alloc, dot_fas_member, src, elements_count,	\
			  gfp)						\
__must_check_errno(({							\
	int __mtfd_err = -EINVAL;					\
	typeof(elements_count) __mtfd_src_count = (elements_count);	\
	typeof(**(alloc)) *__mtfd_alloc;				\
	typeof((*__mtfd_alloc)dot_fas_member) *__mtfd_fas;		\
	u8 *__mtfd_dst;							\
	size_t __mtfd_alloc_bytes, __mtfd_copy_bytes;			\
									\
	do {								\
		if ((uintptr_t)(alloc) < 1 || *(alloc)) {		\
			__mtfd_err = -EINVAL;				\
			break;						\
		}							\
		if (is_negative(__mtfd_src_count) ||			\
		    __fas_elements_bytes(__mtfd_fas,			\
					 __flex_array_elements,		\
					 __flex_array_elements_count,	\
					 __mtfd_src_count,		\
					 &__mtfd_copy_bytes) ||		\
		    __mtfd_copy_bytes > INT_MAX ||			\
		    __mtfd_copy_bytes > __builtin_object_size(src, 1) ||\
		    check_add_overflow(sizeof(*__mtfd_alloc),		\
				       __mtfd_copy_bytes,		\
				       &__mtfd_alloc_bytes)) {		\
			__mtfd_err = -E2BIG;				\
			break;						\
		}							\
		__mtfd_alloc = kmalloc(__mtfd_alloc_bytes, gfp);	\
		if (!__mtfd_alloc) {					\
			__mtfd_err = -ENOMEM;				\
			break;						\
		}							\
		__mtfd_fas = &((*__mtfd_alloc)dot_fas_member);		\
		__mtfd_dst = (u8 *)__mtfd_fas->__flex_array_elements;	\
		__builtin_memset(__mtfd_alloc, 0, __mtfd_alloc_bytes -	\
						  __mtfd_copy_bytes);	\
		__builtin_memcpy(__mtfd_dst, src, __mtfd_copy_bytes);	\
		/* Make sure in-struct count of elements is updated: */	\
		__mtfd_fas->__flex_array_elements_count =		\
						    __mtfd_src_count;	\
		*(alloc) = __mtfd_alloc;				\
		__mtfd_err = 0;						\
	} while (0);							\
	__mtfd_err;							\
}))

/** mem_to_flex_dup - Allocate a flexible array structure and copy
 *			into it from a memory buffer.
 *
 * @alloc: Pointer to pointer to hold allocation for flexible array struct.
 * @src: Source memory pointer.
 * @elements_count: Number of @alloc's flexible array elements to copy from
 *		   @src into @alloc.
 * @gfp: GFP allocation flags
 *
 * This behaves like mem_to_flex(), but allocates the needed space for
 * a new flexible array struct and its trailing elements.
 *
 * This is essentially a simple allocating deserializer.
 *
 * TODO: It would be nice to automatically discover the max bounds of @src
 *	 besides @elements_count. There is currently no universal way to ask
 *	 "what is the size of a given pointer's allocation?" So for
 *	 now just use __builtin_object_size(@src, 1) to validate known
 *	 compile-time too-large conditions. Perhaps in the future if
 *	 __mtf_copy_bytes above is > PAGE_SIZE, perform a dynamic lookup
 *	 using something similar to __check_heap_object().
 *
 * Failure conditions:
 * - @alloc is NULL.
 * - *@alloc is not NULL (something was already allocated).
 * - The value of @elements_count cannot fit in the @alloc's
 *   __flex_array_elements_count member type (e.g. 260 in u8).
 * - @elements_count yields a byte count greater than:
 *   - INT_MAX (as a simple "too big" sanity check)
 *   - the compile-time size of @src (when it can be determined)
 * - @alloc could not be allocated.
 *
 * Returns: 0 on success, -ve on error.
 */
#define mem_to_flex_dup(alloc, src, elements_count, gfp)		\
	__mem_to_flex_dup(alloc, /* alloc itself */, src, elements_count, gfp)

/** flex_to_mem - Copy all flexible array structure elements into memory
 *		  buffer.
 *
 * @dst: Destination buffer pointer.
 * @bytes_available: How many bytes are available in @dst.
 * @ptr: Pointer to allocated flexible array struct.
 * @bytes_written: Pointer to variable to store how many bytes were written
 *		   (may be NULL).
 *
 * Copies all of @ptr's flexible array elements into @dst.
 *
 * This is essentially a simple serializer.
 *
 * Failure conditions:
 * - @bytes_available in @dst is any of:
 *   - negative.
 *   - larger than INT_MAX.
 *   - not large enough to hold the resulting copy.
 * - @bytes_written's type cannot hold the size of the copy (e.g. 260 in u8).
 *
 * Return: 0 on success, -ve on failure.
 *
 */
#define flex_to_mem(dst, bytes_available, ptr, bytes_written)		\
__must_check_errno(({							\
	int __ftm_err = -EINVAL;					\
	typeof(*(ptr)) *__ftm_ptr = (ptr);				\
	u8 *__ftm_src = (u8 *)__ftm_ptr->__flex_array_elements;		\
	typeof(*(bytes_written)) *__ftm_written = (bytes_written);	\
	size_t __ftm_written_max = type_max(typeof(*__ftm_written));	\
	typeof(bytes_available) __ftm_dst_bytes = (bytes_available);	\
	size_t __ftm_copy_bytes;					\
									\
	do {								\
		if (is_negative(__ftm_dst_bytes) ||			\
		    __ftm_dst_bytes > INT_MAX ||			\
		    fas_elements_bytes(__ftm_ptr, &__ftm_copy_bytes) ||	\
		    __ftm_dst_bytes < __ftm_copy_bytes ||		\
		    (!__same_type(typeof(bytes_written), NULL) &&	\
		     __ftm_copy_bytes > __ftm_written_max)) {		\
			__ftm_err = -E2BIG;				\
			break;						\
		}							\
		__builtin_memcpy(dst, __ftm_src, __ftm_copy_bytes);	\
		if (__ftm_written)					\
			*__ftm_written = __ftm_copy_bytes;		\
		__ftm_err = 0;						\
	} while (0);							\
	__ftm_err;							\
}))

/** flex_to_mem_dup - Copy entire flexible array structure into newly
 *		      allocated memory buffer.
 *
 * @alloc: Pointer to pointer to newly allocated memory region to hold contents
 *	   of the copy.
 * @alloc_size: Pointer to variable to hold the size of the allocated memory.
 * @ptr: Pointer to allocated flexible array struct.
 * @gfp: GFP allocation flags
 *
 * Allocates @alloc and copies all of @ptr's flexible array elements.
 *
 * This is essentially a simple allocating serializer.
 *
 * Failure conditions:
 * - @alloc is NULL.
 * - *@alloc is not NULL (something was already allocated).
 * - @alloc_size is NULL.
 * - @alloc_size's type cannot hold the size of the copy (e.g. 260 in u8).
 * - @alloc could not be allocated.
 *
 * Return: 0 on success, -ve on failure.
 */
#define flex_to_mem_dup(alloc, alloc_size, ptr, gfp)			\
__must_check_errno(({							\
	int __ftmd_err = -EINVAL;					\
	typeof(**(alloc)) *__ftmd_alloc;				\
	typeof(*(alloc_size)) *__ftmd_alloc_size = (alloc_size);	\
	typeof(*(ptr)) *__ftmd_ptr = (ptr);				\
	u8 *__ftmd_src = (u8 *)__ftmd_ptr->__flex_array_elements;	\
	size_t __ftmd_alloc_max = type_max(typeof(*__ftmd_alloc_size));	\
	size_t __ftmd_copy_bytes;					\
									\
	do {								\
		if ((uintptr_t)(alloc) < 1 || *(alloc) ||		\
		    (uintptr_t)(alloc_size) < 1) {			\
			__ftmd_err = -EINVAL;				\
			break;						\
		}							\
		if (fas_elements_bytes(__ftmd_ptr,			\
				       &__ftmd_copy_bytes) ||		\
		    __ftmd_copy_bytes > __ftmd_alloc_max) {		\
			__ftmd_err = -E2BIG;				\
			break;						\
		}							\
		__ftmd_alloc = kmemdup(__ftmd_src, __ftmd_copy_bytes,	\
				       gfp);				\
		if (!__ftmd_alloc) {					\
			__ftmd_err = -ENOMEM;				\
			break;						\
		}							\
		*__ftmd_alloc_size = __ftmd_copy_bytes;			\
		*(alloc) = __ftmd_alloc;				\
		__ftmd_err = 0;						\
	} while (0);							\
	__ftmd_err;							\
}))

#endif /* _LINUX_FLEX_ARRAY_H_ */
