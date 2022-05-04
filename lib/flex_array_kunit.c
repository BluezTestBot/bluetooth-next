// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for flex_*() array manipulation helpers.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/flex_array.h>

#define COMPARE_STRUCTS(STRUCT_A, STRUCT_B)	do {			\
	STRUCT_A *ptr_A;						\
	STRUCT_B *ptr_B;						\
	int rc;								\
	size_t size_A, size_B;						\
									\
	/* matching types for flex array elements and count */		\
	KUNIT_EXPECT_EQ(test, sizeof(*ptr_A), sizeof(*ptr_B));		\
	KUNIT_EXPECT_TRUE(test, __same_type(*ptr_A->data,		\
		*ptr_B->__flex_array_elements));			\
	KUNIT_EXPECT_TRUE(test, __same_type(ptr_A->datalen,		\
		ptr_B->__flex_array_elements_count));			\
	KUNIT_EXPECT_EQ(test, sizeof(*ptr_A->data),			\
			      sizeof(*ptr_B->__flex_array_elements));	\
	KUNIT_EXPECT_EQ(test, offsetof(typeof(*ptr_A), data),		\
			      offsetof(typeof(*ptr_B),			\
				       __flex_array_elements));		\
	KUNIT_EXPECT_EQ(test, offsetof(typeof(*ptr_A), datalen),	\
			      offsetof(typeof(*ptr_B),			\
				       __flex_array_elements_count));	\
									\
	/* struct_size() vs __fas_bytes() */				\
	size_A = struct_size(ptr_A, data, 13);				\
	rc = __fas_bytes(ptr_B, __flex_array_elements,			\
			 __flex_array_elements_count, 13, &size_B);	\
	KUNIT_EXPECT_EQ(test, rc, 0);					\
	KUNIT_EXPECT_EQ(test, size_A, size_B);				\
									\
	/* flex_array_size() vs __fas_elements_bytes() */		\
	size_A = flex_array_size(ptr_A, data, 13);			\
	rc = __fas_elements_bytes(ptr_B, __flex_array_elements,		\
			 __flex_array_elements_count, 13, &size_B);	\
	KUNIT_EXPECT_EQ(test, rc, 0);					\
	KUNIT_EXPECT_EQ(test, size_A, size_B);				\
									\
	KUNIT_EXPECT_EQ(test, sizeof(*ptr_A) + size_A,			\
			      offsetof(typeof(*ptr_A), data) +		\
			      (sizeof(*ptr_A->data) * 13));		\
	KUNIT_EXPECT_EQ(test, sizeof(*ptr_B) + size_B,			\
			      offsetof(typeof(*ptr_B),			\
				       __flex_array_elements) +		\
			      (sizeof(*ptr_B->__flex_array_elements) *	\
			       13));					\
} while (0)

struct normal {
	size_t	datalen;
	u32	data[];
};

struct decl_normal {
	DECLARE_FLEX_ARRAY_ELEMENTS_COUNT(size_t, datalen);
	DECLARE_FLEX_ARRAY_ELEMENTS(u32, data);
};

struct aligned {
	unsigned short	datalen;
	char		data[] __aligned(__alignof__(u64));
};

struct decl_aligned {
	DECLARE_FLEX_ARRAY_ELEMENTS_COUNT(unsigned short, datalen);
	DECLARE_FLEX_ARRAY_ELEMENTS(char, data) __aligned(__alignof__(u64));
};

static void struct_test(struct kunit *test)
{
	COMPARE_STRUCTS(struct normal, struct decl_normal);
	COMPARE_STRUCTS(struct aligned, struct decl_aligned);
}

/* Flexible array structure with internal padding. */
struct flex_cpy_obj {
	DECLARE_FLEX_ARRAY_ELEMENTS_COUNT(u8, count);
	unsigned long empty;
	char induce_padding;
	/* padding ends up here */
	unsigned long after_padding;
	DECLARE_FLEX_ARRAY_ELEMENTS(u32, flex);
};

/* Encapsulating flexible array structure. */
struct flex_dup_obj {
	unsigned long flags;
	int junk;
	struct flex_cpy_obj fas;
};

/* Flexible array struct of only bytes. */
struct tiny_flex {
	DECLARE_FLEX_ARRAY_ELEMENTS_COUNT(u8, count);
	DECLARE_FLEX_ARRAY_ELEMENTS(u8, byte_array);
};

#define CHECK_COPY(ptr)		do {						\
	typeof(*(ptr)) *_cc_dst = (ptr);					\
	KUNIT_EXPECT_EQ(test, _cc_dst->induce_padding, 0);			\
	memcpy(&padding, &_cc_dst->induce_padding + sizeof(_cc_dst->induce_padding), \
	       sizeof(padding));						\
	/* Padding should be zero too. */					\
	KUNIT_EXPECT_EQ(test, padding, 0);					\
	KUNIT_EXPECT_EQ(test, src->count, _cc_dst->count);			\
	KUNIT_EXPECT_EQ(test, _cc_dst->count, TEST_TARGET);			\
	for (i = 0; i < _cc_dst->count - 1; i++) {				\
		/* 'A' is 0x41, and here repeated in a u32. */			\
		KUNIT_EXPECT_EQ(test, _cc_dst->flex[i], 0x41414141);		\
	}									\
	/* Last item should be different. */					\
	KUNIT_EXPECT_EQ(test, _cc_dst->flex[_cc_dst->count - 1], 0x14141414);	\
} while (0)

/* Test copying from one flexible array struct into another. */
static void flex_cpy_test(struct kunit *test)
{
#define TEST_BOUNDS	13
#define TEST_TARGET	12
#define TEST_SMALL	10
	struct flex_cpy_obj *src, *dst;
	unsigned long padding;
	int i, rc;

	/* Prepare open-coded source. */
	src = kzalloc(struct_size(src, flex, TEST_BOUNDS), GFP_KERNEL);
	src->count = TEST_BOUNDS;
	memset(src->flex, 'A', flex_array_size(src, flex, TEST_BOUNDS));
	src->flex[src->count - 2] = 0x14141414;
	src->flex[src->count - 1] = 0x24242424;

	/* Prepare open-coded destination, alloc only. */
	dst = kzalloc(struct_size(src, flex, TEST_BOUNDS), GFP_KERNEL);
	/* Pre-fill with 0xFE marker. */
	memset(dst, 0xFE, struct_size(src, flex, TEST_BOUNDS));
	/* Pretend we're 1 element smaller. */
	dst->count = TEST_TARGET;

	/* Pretend to match the target destination size. */
	src->count = TEST_TARGET;

	rc = flex_cpy(dst, src);
	KUNIT_EXPECT_EQ(test, rc, 0);
	CHECK_COPY(dst);
	/* Item past last copied item is unchanged from initial memset. */
	KUNIT_EXPECT_EQ(test, dst->flex[dst->count], 0xFEFEFEFE);

	/* Now trip overflow, and verify we didn't clobber beyond end. */
	src->count = TEST_BOUNDS;
	rc = flex_cpy(dst, src);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	/* Item past last copied item is unchanged from initial memset. */
	KUNIT_EXPECT_EQ(test, dst->flex[dst->count], 0xFEFEFEFE);

	/* Reset destination contents. */
	memset(dst, 0xFD, struct_size(src, flex, TEST_BOUNDS));
	dst->count = TEST_TARGET;

	/* Copy less than max. */
	src->count = TEST_SMALL;
	rc = flex_cpy(dst, src);
	KUNIT_EXPECT_EQ(test, rc, 0);
	/* Verify count was adjusted. */
	KUNIT_EXPECT_EQ(test, dst->count, TEST_SMALL);
	/* Verify element beyond src size was wiped. */
	KUNIT_EXPECT_EQ(test, dst->flex[TEST_SMALL], 0);
	/* Verify element beyond original dst size was untouched. */
	KUNIT_EXPECT_EQ(test, dst->flex[TEST_TARGET], 0xFDFDFDFD);

	kfree(dst);
	kfree(src);
#undef TEST_BOUNDS
#undef TEST_TARGET
#undef TEST_SMALL
}

static void flex_dup_test(struct kunit *test)
{
#define TEST_TARGET	12
	struct flex_cpy_obj *src, *dst = NULL, **null = NULL;
	struct flex_dup_obj *encap = NULL;
	unsigned long padding;
	int i, rc;

	/* Prepare open-coded source. */
	src = kzalloc(struct_size(src, flex, TEST_TARGET), GFP_KERNEL);
	src->count = TEST_TARGET;
	memset(src->flex, 'A', flex_array_size(src, flex, TEST_TARGET));
	src->flex[src->count - 1] = 0x14141414;

	/* Reject NULL @alloc. */
	rc = flex_dup(null, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -EINVAL);

	/* Check good copy. */
	rc = flex_dup(&dst, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_ASSERT_TRUE(test, dst != NULL);
	CHECK_COPY(dst);

	/* Reject non-NULL *@alloc. */
	rc = flex_dup(&dst, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -EINVAL);

	kfree(dst);

	/* Check good encap copy. */
	rc = __flex_dup(&encap, .fas, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_ASSERT_TRUE(test, dst != NULL);
	CHECK_COPY(&encap->fas);
	/* Check that items external to "fas" are zero. */
	KUNIT_EXPECT_EQ(test, encap->flags, 0);
	KUNIT_EXPECT_EQ(test, encap->junk, 0);
	kfree(encap);
#undef MAGIC_WORD
#undef TEST_TARGET
}

static void mem_to_flex_test(struct kunit *test)
{
#define TEST_TARGET	9
#define TEST_MAX	U8_MAX
#define MAGIC_WORD	0x03030303
	u8 magic_byte = MAGIC_WORD & 0xff;
	struct flex_cpy_obj *dst;
	size_t big = (size_t)INT_MAX + 1;
	char small[] = "Hello";
	char *src;
	u32 src_len;
	int rc;

	/* Open coded allocations, 1 larger than actually used. */
	src_len = flex_array_size(dst, flex, TEST_MAX + 1);
	src = kzalloc(src_len, GFP_KERNEL);
	dst = kzalloc(struct_size(dst, flex, TEST_MAX + 1), GFP_KERNEL);
	dst->count = TEST_TARGET;

	/* Fill source. */
	memset(src, magic_byte, src_len);

	/* Short copy is fine. */
	KUNIT_EXPECT_EQ(test, dst->flex[0], 0);
	KUNIT_EXPECT_EQ(test, dst->flex[1], 0);
	rc = mem_to_flex(dst, src, 1);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_EXPECT_EQ(test, dst->count, 1);
	KUNIT_EXPECT_EQ(test, dst->after_padding, 0);
	KUNIT_EXPECT_EQ(test, dst->flex[0], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, dst->flex[1], 0);
	dst->count = TEST_TARGET;

	/* Reject negative elements count. */
	rc = mem_to_flex(dst, small, -1);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	/* Make sure dst is unchanged. */
	KUNIT_EXPECT_EQ(test, dst->flex[0], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, dst->flex[1], 0);

	/* Reject compile-time read overflow. */
	rc = mem_to_flex(dst, small, 20);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	/* Make sure dst is unchanged. */
	KUNIT_EXPECT_EQ(test, dst->flex[0], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, dst->flex[1], 0);

	/* Reject giant buffer source. */
	rc = mem_to_flex(dst, small, big);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	/* Make sure dst is unchanged. */
	KUNIT_EXPECT_EQ(test, dst->flex[0], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, dst->flex[1], 0);

	/* Copy beyond storage size is rejected. */
	dst->count = TEST_MAX;
	KUNIT_EXPECT_EQ(test, dst->flex[TEST_MAX - 1], 0);
	KUNIT_EXPECT_EQ(test, dst->flex[TEST_MAX], 0);
	rc = mem_to_flex(dst, src, TEST_MAX + 1);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	/* Make sure dst is unchanged. */
	KUNIT_EXPECT_EQ(test, dst->flex[0], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, dst->flex[1], 0);

	kfree(dst);
	kfree(src);
#undef MAGIC_WORD
#undef TEST_MAX
#undef TEST_TARGET
}

static void mem_to_flex_dup_test(struct kunit *test)
{
#define ELEMENTS_COUNT	259
#define MAGIC_WORD	0xABABABAB
	u8 magic_byte = MAGIC_WORD & 0xff;
	struct flex_dup_obj *obj = NULL;
	struct tiny_flex *tiny = NULL, **null = NULL;
	size_t src_len, count, big = (size_t)INT_MAX + 1;
	char small[] = "Hello";
	u8 *src;
	int rc;

	src_len = struct_size(tiny, byte_array, ELEMENTS_COUNT);
	src = kzalloc(src_len, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, src != NULL);
	/* Fill with bytes. */
	memset(src, magic_byte, src_len);
	KUNIT_EXPECT_EQ(test, src[0], magic_byte);
	KUNIT_EXPECT_EQ(test, src[src_len / 2], magic_byte);
	KUNIT_EXPECT_EQ(test, src[src_len - 1], magic_byte);

	/* Reject storage exceeding elements_count type. */
	count = ELEMENTS_COUNT;
	rc = mem_to_flex_dup(&tiny, src, count, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	KUNIT_EXPECT_TRUE(test, tiny == NULL);

	/* Reject negative elements count. */
	rc = mem_to_flex_dup(&tiny, src, -1, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	KUNIT_EXPECT_TRUE(test, tiny == NULL);

	/* Reject compile-time read overflow. */
	rc = mem_to_flex_dup(&tiny, small, 20, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	KUNIT_EXPECT_TRUE(test, tiny == NULL);

	/* Reject giant buffer source. */
	rc = mem_to_flex_dup(&tiny, small, big, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	KUNIT_EXPECT_TRUE(test, tiny == NULL);

	/* Reject NULL @alloc. */
	rc = mem_to_flex_dup(null, src, count, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -EINVAL);

	/* Allow reasonable count.*/
	count = ELEMENTS_COUNT / 2;
	rc = mem_to_flex_dup(&tiny, src, count, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_ASSERT_TRUE(test, tiny != NULL);
	/* Spot check the copy happened. */
	KUNIT_EXPECT_EQ(test, tiny->count, count);
	KUNIT_EXPECT_EQ(test, tiny->byte_array[0], magic_byte);
	KUNIT_EXPECT_EQ(test, tiny->byte_array[count / 2], magic_byte);
	KUNIT_EXPECT_EQ(test, tiny->byte_array[count - 1], magic_byte);

	/* Reject non-NULL *@alloc. */
	rc = mem_to_flex_dup(&tiny, src, count, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -EINVAL);
	kfree(tiny);

	/* Works with encapsulation too. */
	count = ELEMENTS_COUNT / 10;
	rc = __mem_to_flex_dup(&obj, .fas, src, count, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_ASSERT_TRUE(test, obj != NULL);
	/* Spot check the copy happened. */
	KUNIT_EXPECT_EQ(test, obj->fas.count, count);
	KUNIT_EXPECT_EQ(test, obj->fas.after_padding, 0);
	KUNIT_EXPECT_EQ(test, obj->fas.flex[0], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, obj->fas.flex[count / 2], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, obj->fas.flex[count - 1], MAGIC_WORD);
	/* Check members before flexible array struct are zero. */
	KUNIT_EXPECT_EQ(test, obj->flags, 0);
	KUNIT_EXPECT_EQ(test, obj->junk, 0);
	kfree(obj);
#undef MAGIC_WORD
#undef ELEMENTS_COUNT
}

static void flex_to_mem_test(struct kunit *test)
{
#define ELEMENTS_COUNT	200
#define MAGIC_WORD	0xF1F2F3F4
	struct flex_cpy_obj *src;
	typeof(*src->flex) *cast;
	size_t src_len = struct_size(src, flex, ELEMENTS_COUNT);
	size_t copy_len = flex_array_size(src, flex, ELEMENTS_COUNT);
	int i, rc;
	size_t bytes = 0;
	u8 too_small;
	u8 *dst;

	/* Create a filled flexible array struct. */
	src = kzalloc(src_len, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, src != NULL);
	src->count = ELEMENTS_COUNT;
	src->after_padding = 13;
	for (i = 0; i < ELEMENTS_COUNT; i++)
		src->flex[i] = MAGIC_WORD;

	/* Over-allocate space to do past-src_len checking. */
	dst = kzalloc(src_len * 2, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, dst != NULL);
	cast = (void *)dst;

	/* Fail if dst is too small. */
	rc = flex_to_mem(dst, copy_len - 1, src, &bytes);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	/* Make sure nothing was copied. */
	KUNIT_EXPECT_EQ(test, bytes, 0);
	KUNIT_EXPECT_EQ(test, cast[0], 0);

	/* Fail if type too small to hold size of copy. */
	KUNIT_EXPECT_GT(test, copy_len, type_max(typeof(too_small)));
	rc = flex_to_mem(dst, copy_len, src, &too_small);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	/* Make sure nothing was copied. */
	KUNIT_EXPECT_EQ(test, bytes, 0);
	KUNIT_EXPECT_EQ(test, cast[0], 0);

	/* Check good copy. */
	rc = flex_to_mem(dst, copy_len, src, &bytes);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_EXPECT_EQ(test, bytes, copy_len);
	/* Spot check the copy */
	KUNIT_EXPECT_EQ(test, cast[0], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, cast[ELEMENTS_COUNT / 2], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, cast[ELEMENTS_COUNT - 1], MAGIC_WORD);
	/* Make sure nothing was written after last element. */
	KUNIT_EXPECT_EQ(test, cast[ELEMENTS_COUNT], 0);

	kfree(dst);
	kfree(src);
#undef MAGIC_WORD
#undef ELEMENTS_COUNT
}

static void flex_to_mem_dup_test(struct kunit *test)
{
#define ELEMENTS_COUNT	210
#define MAGIC_WORD	0xF0F1F2F3
	struct flex_dup_obj *obj, **null = NULL;
	struct flex_cpy_obj *src;
	typeof(*src->flex) *cast;
	size_t obj_len = struct_size(obj, fas.flex, ELEMENTS_COUNT);
	size_t src_len = struct_size(src, flex, ELEMENTS_COUNT);
	size_t copy_len = flex_array_size(src, flex, ELEMENTS_COUNT);
	int i, rc;
	size_t bytes = 0;
	u8 too_small = 0;
	u8 *dst = NULL;

	/* Create a filled flexible array struct. */
	obj = kzalloc(obj_len, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, obj != NULL);
	obj->fas.count = ELEMENTS_COUNT;
	obj->fas.after_padding = 13;
	for (i = 0; i < ELEMENTS_COUNT; i++)
		obj->fas.flex[i] = MAGIC_WORD;
	src = &obj->fas;

	/* Fail if type too small to hold size of copy. */
	KUNIT_EXPECT_GT(test, src_len, type_max(typeof(too_small)));
	rc = flex_to_mem_dup(&dst, &too_small, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -E2BIG);
	KUNIT_EXPECT_TRUE(test, dst == NULL);
	KUNIT_EXPECT_EQ(test, too_small, 0);

	/* Fail if @alloc_size is NULL. */
	KUNIT_EXPECT_TRUE(test, dst == NULL);
	rc = flex_to_mem_dup(&dst, dst, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -EINVAL);
	KUNIT_EXPECT_TRUE(test, dst == NULL);

	/* Fail if @alloc is NULL. */
	rc = flex_to_mem_dup(null, &bytes, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -EINVAL);
	KUNIT_EXPECT_TRUE(test, dst == NULL);
	KUNIT_EXPECT_EQ(test, bytes, 0);

	/* Check good copy. */
	rc = flex_to_mem_dup(&dst, &bytes, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_EXPECT_TRUE(test, dst != NULL);
	KUNIT_EXPECT_EQ(test, bytes, copy_len);
	cast = (void *)dst;
	/* Spot check the copy */
	KUNIT_EXPECT_EQ(test, cast[0], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, cast[ELEMENTS_COUNT / 2], MAGIC_WORD);
	KUNIT_EXPECT_EQ(test, cast[ELEMENTS_COUNT - 1], MAGIC_WORD);

	/* Fail if *@alloc is non-NULL. */
	bytes = 0;
	rc = flex_to_mem_dup(&dst, &bytes, src, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, rc, -EINVAL);
	KUNIT_EXPECT_EQ(test, bytes, 0);

	kfree(dst);
	kfree(obj);
#undef MAGIC_WORD
#undef ELEMENTS_COUNT
}

static struct kunit_case flex_array_test_cases[] = {
	KUNIT_CASE(struct_test),
	KUNIT_CASE(flex_cpy_test),
	KUNIT_CASE(flex_dup_test),
	KUNIT_CASE(mem_to_flex_test),
	KUNIT_CASE(mem_to_flex_dup_test),
	KUNIT_CASE(flex_to_mem_test),
	KUNIT_CASE(flex_to_mem_dup_test),
	{}
};

static struct kunit_suite flex_array_test_suite = {
	.name = "flex_array",
	.test_cases = flex_array_test_cases,
};

kunit_test_suite(flex_array_test_suite);

MODULE_LICENSE("GPL");
