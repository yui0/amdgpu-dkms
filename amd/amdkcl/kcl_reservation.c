/*
 * Copyright (C) 2012-2013 Canonical Ltd
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <kcl/kcl_fence.h>
#include <kcl/kcl_reservation.h>

/*
 * Modifications [2016-10-26] (c) [2016]
 * Modifications [2017-07-06] (c) [2017]
 * Advanced Micro Devices, Inc.
 */
#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0) && \
		!defined(OS_NAME_RHEL_7_4)
long _kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout)
{
	struct fence *fence;
	unsigned seq, shared_count, i = 0;
	long ret = timeout ? timeout : 1;

retry:
	shared_count = 0;
	seq = read_seqcount_begin(&obj->seq);
	rcu_read_lock();

	/*
	 * Modifications [2017-08-16] (c) [2017]
	 * Advanced Micro Devices, Inc.
	 */
	fence = rcu_dereference(obj->fence_excl);
	if (fence && !test_bit(FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		if (!dma_fence_get_rcu(fence))
			goto unlock_retry;

		if (dma_fence_is_signaled(fence)) {
			dma_fence_put(fence);
			fence = NULL;
		}

	} else {
		fence = NULL;
	}

	if (!fence && wait_all) {
		struct reservation_object_list *fobj =
						rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		if (read_seqcount_retry(&obj->seq, seq))
			goto unlock_retry;

		for (i = 0; i < shared_count; ++i) {
			struct fence *lfence = rcu_dereference(fobj->shared[i]);

			if (test_bit(FENCE_FLAG_SIGNALED_BIT, &lfence->flags))
				continue;

			if (!fence_get_rcu(lfence))
				goto unlock_retry;

			if (fence_is_signaled(lfence)) {
				fence_put(lfence);
				continue;
			}

			fence = lfence;
			break;
		}
	}

	rcu_read_unlock();
	if (fence) {
		ret = kcl_fence_wait_timeout(fence, intr, ret);
		fence_put(fence);
		if (ret > 0 && wait_all && (i + 1 < shared_count))
			goto retry;
	}
	return ret;

unlock_retry:
	rcu_read_unlock();
	goto retry;
}
EXPORT_SYMBOL(_kcl_reservation_object_wait_timeout_rcu);
#endif

/*
 * Modifications [2017-09-14] (c) [2017]
 * Advanced Micro Devices, Inc.
 */
#if defined(BUILD_AS_DKMS)
int _kcl_reservation_object_copy_fences(struct reservation_object *dst,
					struct reservation_object *src)
{
	struct reservation_object_list *src_list, *dst_list;
	struct dma_fence *old, *new;
	size_t size;
	unsigned i;

	rcu_read_lock();
	src_list = rcu_dereference(src->fence);

retry:
	if (src_list) {
		unsigned shared_count = src_list->shared_count;

		size = offsetof(typeof(*src_list), shared[shared_count]);
		rcu_read_unlock();

		dst_list = kmalloc(size, GFP_KERNEL);
		if (!dst_list)
			return -ENOMEM;

		rcu_read_lock();
		src_list = rcu_dereference(src->fence);
		if (!src_list || src_list->shared_count > shared_count) {
			kfree(dst_list);
			goto retry;
		}

		dst_list->shared_count = 0;
		dst_list->shared_max = shared_count;
		for (i = 0; i < src_list->shared_count; ++i) {
			struct dma_fence *fence;

			fence = rcu_dereference(src_list->shared[i]);
			if (test_bit(FENCE_FLAG_SIGNALED_BIT,
				     &fence->flags))
				continue;

			if (!dma_fence_get_rcu(fence)) {
				kfree(dst_list);
				src_list = rcu_dereference(src->fence);
				goto retry;
			}

			if (dma_fence_is_signaled(fence)) {
				dma_fence_put(fence);
				continue;
			}

			dst_list->shared[dst_list->shared_count++] = fence;
		}
	} else {
		dst_list = NULL;
	}

	new = kcl_fence_get_rcu_safe(&src->fence_excl);
	rcu_read_unlock();

	kfree(dst->staged);
	dst->staged = NULL;

	src_list = reservation_object_get_list(dst);
	old = reservation_object_get_excl(dst);

	preempt_disable();
	write_seqcount_begin(&dst->seq);
	/* write_seqcount_begin provides the necessary memory barrier */
	RCU_INIT_POINTER(dst->fence_excl, new);
	RCU_INIT_POINTER(dst->fence, dst_list);
	write_seqcount_end(&dst->seq);
	preempt_enable();

	if (src_list)
		kfree_rcu(src_list, rcu);
	dma_fence_put(old);

	return 0;
}
EXPORT_SYMBOL(_kcl_reservation_object_copy_fences);
#endif

/*
 * Modifications [2016-12-27] (c) [2016]
 * Advanced Micro Devices, Inc.
 */
#ifdef OS_NAME_RHEL_6
static inline int
reservation_object_test_signaled_single(struct fence *passed_fence)
{
	struct fence *fence, *lfence = passed_fence;
	int ret = 1;

	if (!test_bit(FENCE_FLAG_SIGNALED_BIT, &lfence->flags)) {
		fence = fence_get_rcu(lfence);
		if (!fence)
			return -1;

		ret = !!fence_is_signaled(fence);
		fence_put(fence);
	}
	return ret;
}

bool _kcl_reservation_object_test_signaled_rcu(struct reservation_object *obj,
					       bool test_all)
{
	unsigned seq, shared_count;
	int ret = true;

retry:
	shared_count = 0;
	seq = read_seqcount_begin(&obj->seq);
	rcu_read_lock();

	if (test_all) {
		unsigned i;

		struct reservation_object_list *fobj = rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		if (read_seqcount_retry(&obj->seq, seq))
			goto unlock_retry;

		for (i = 0; i < shared_count; ++i) {
			struct fence *fence = rcu_dereference(fobj->shared[i]);

			ret = reservation_object_test_signaled_single(fence);
			if (ret < 0)
				goto unlock_retry;
			else if (!ret)
				break;
		}

		/*
		 * There could be a read_seqcount_retry here, but nothing cares
		 * about whether it's the old or newer fence pointers that are
		 * signaled. That race could still have happened after checking
		 * read_seqcount_retry. If you care, use ww_mutex_lock.
		 */
	}

	if (!shared_count) {
		struct fence *fence_excl = rcu_dereference(obj->fence_excl);

		if (read_seqcount_retry(&obj->seq, seq))
			goto unlock_retry;

		if (fence_excl) {
			ret = reservation_object_test_signaled_single(fence_excl);
			if (ret < 0)
				goto unlock_retry;
		}
	}

	rcu_read_unlock();
	return ret;

unlock_retry:
	rcu_read_unlock();
	goto retry;
}
EXPORT_SYMBOL_GPL(_kcl_reservation_object_test_signaled_rcu);
#endif
