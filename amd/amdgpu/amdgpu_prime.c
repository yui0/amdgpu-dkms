/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * based on nouveau_prime.c
 *
 * Authors: Alex Deucher
 */
#include <drm/drmP.h>

#include "amdgpu.h"
#include <drm/amdgpu_drm.h>
#include <linux/dma-buf.h>

struct sg_table *amdgpu_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	int npages = bo->tbo.num_pages;

	return drm_prime_pages_to_sg(bo->tbo.ttm->pages, npages);
}

void *amdgpu_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	int ret;

	ret = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages,
			  &bo->dma_buf_vmap);
	if (ret)
		return ERR_PTR(ret);

	return bo->dma_buf_vmap.virtual;
}

void amdgpu_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);

	ttm_bo_kunmap(&bo->dma_buf_vmap);
}

struct drm_gem_object *
amdgpu_gem_prime_import_sg_table(struct drm_device *dev,
				 struct dma_buf_attachment *attach,
				 struct sg_table *sg)
{
	struct reservation_object *resv = attach->dmabuf->resv;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_bo *bo;
	struct amdgpu_gem_object *gobj;
	int ret;

	ww_mutex_lock(&resv->lock, NULL);
	ret = amdgpu_bo_create(adev, attach->dmabuf->size, PAGE_SIZE, false,
			       AMDGPU_GEM_DOMAIN_GTT, 0, sg, resv, 0, &bo);
	ww_mutex_unlock(&resv->lock);
	if (ret)
		return ERR_PTR(ret);

	bo->prime_shared_count = 1;

	gobj = kzalloc(sizeof(struct amdgpu_gem_object), GFP_KERNEL);
	if (unlikely(!gobj)) {
		amdgpu_bo_unref(&bo);
		return ERR_PTR(-ENOMEM);
	}

	ret = drm_gem_object_init(adev->ddev, &gobj->base, amdgpu_bo_size(bo));
	if (unlikely(ret)) {
		kfree(gobj);
		amdgpu_bo_unref(&bo);
		return ERR_PTR(ret);
	}

	list_add(&gobj->list, &bo->gem_objects);
	gobj->bo = amdgpu_bo_ref(bo);

	return &gobj->base;
}

int amdgpu_gem_prime_pin(struct drm_gem_object *obj)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	long ret = 0;

	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	/*
	 * Wait for all shared fences to complete before we switch to future
	 * use of exclusive fence on this prime shared bo.
	 */
	ret = reservation_object_wait_timeout_rcu(bo->tbo.resv, true, false,
						  MAX_SCHEDULE_TIMEOUT);
	if (unlikely(ret < 0)) {
		DRM_DEBUG_PRIME("Fence wait failed: %li\n", ret);
		amdgpu_bo_unreserve(bo);
		return ret;
	}

	/* pin buffer into GTT */
	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT, NULL);
	if (likely(ret == 0))
		bo->prime_shared_count++;

	amdgpu_bo_unreserve(bo);
	return ret;
}

void amdgpu_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, true);
	if (unlikely(ret != 0))
		return;

	amdgpu_bo_unpin(bo);
	if (bo->prime_shared_count)
		bo->prime_shared_count--;
	amdgpu_bo_unreserve(bo);
}

struct reservation_object *amdgpu_gem_prime_res_obj(struct drm_gem_object *obj)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);

	return bo->tbo.resv;
}

struct dma_buf *amdgpu_gem_prime_export(struct drm_device *dev,
					struct drm_gem_object *gobj,
					int flags)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(gobj);

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) ||
	    bo->flags & AMDGPU_GEM_CREATE_VM_ALWAYS_VALID)
		return ERR_PTR(-EPERM);

	return drm_gem_prime_export(dev, gobj, flags);
}

struct drm_gem_object *
amdgpu_gem_prime_foreign_bo(struct amdgpu_device *adev, struct amdgpu_bo *bo)
{
	struct amdgpu_gem_object *gobj;
	int r;

	ww_mutex_lock(&bo->tbo.resv->lock, NULL);

	list_for_each_entry(gobj, &bo->gem_objects, list) {
		if (gobj->base.dev != adev->ddev)
			continue;

		ww_mutex_unlock(&bo->tbo.resv->lock);
		drm_gem_object_reference(&gobj->base);
		return &gobj->base;
	}


	gobj = kzalloc(sizeof(struct amdgpu_gem_object), GFP_KERNEL);
	if (unlikely(!gobj)) {
		ww_mutex_unlock(&bo->tbo.resv->lock);
		return ERR_PTR(-ENOMEM);
	}

	r = drm_gem_object_init(adev->ddev, &gobj->base, amdgpu_bo_size(bo));
	if (unlikely(r)) {
		kfree(gobj);
		ww_mutex_unlock(&bo->tbo.resv->lock);
		return ERR_PTR(r);
	}

	list_add(&gobj->list, &bo->gem_objects);
	gobj->bo = amdgpu_bo_ref(bo);
	bo->flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;

	ww_mutex_unlock(&bo->tbo.resv->lock);

	return &gobj->base;
}

struct drm_gem_object *amdgpu_gem_prime_import(struct drm_device *dev,
					       struct dma_buf *dma_buf)
{
	struct amdgpu_device *adev = dev->dev_private;

	if (dma_buf->ops == &drm_gem_prime_dmabuf_ops) {
		struct drm_gem_object *obj = dma_buf->priv;

		if (obj->dev != dev && obj->dev->driver == dev->driver) {
			/* It's a amdgpu_bo from a different driver instance */
			struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);

			return amdgpu_gem_prime_foreign_bo(adev, bo);
		}
	}

	return drm_gem_prime_import(dev, dma_buf);
}
