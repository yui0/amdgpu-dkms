/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0) && !defined(OS_NAME_RHEL_7_4)

#include "dm_services_types.h"
#include "dc.h"

#include "vid.h"
#include "amdgpu.h"
#include "amdgpu_display.h"
#include "atom.h"
#include "kcl_dm.h"
#include "amdgpu_pm.h"

#include "amd_shared.h"
#include "kcl_dm_irq.h"
#include "dm_helpers.h"
#include "dm_services_types.h"
#include "amdgpu_dm_mst_types.h"

#include "ivsrcid/ivsrcid_vislands30.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/types.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_edid.h>

#include "modules/inc/mod_freesync.h"

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#include "ivsrcid/irqsrcs_dcn_1_0.h"

#include "raven1/DCN/dcn_1_0_offset.h"
#include "raven1/DCN/dcn_1_0_sh_mask.h"
#include "vega10/soc15ip.h"

#include "soc15_common.h"
#endif

#include "modules/inc/mod_freesync.h"

#include "i2caux_interface.h"


static enum drm_plane_type dm_plane_type_default[AMDGPU_MAX_PLANES] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
};

static enum drm_plane_type dm_plane_type_carizzo[AMDGPU_MAX_PLANES] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_OVERLAY,/* YUV Capable Underlay */
};

static enum drm_plane_type dm_plane_type_stoney[AMDGPU_MAX_PLANES] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_OVERLAY, /* YUV Capable Underlay */
};

/*
 * dm_vblank_get_counter
 *
 * @brief
 * Get counter for number of vertical blanks
 *
 * @param
 * struct amdgpu_device *adev - [in] desired amdgpu device
 * int disp_idx - [in] which CRTC to get the counter from
 *
 * @return
 * Counter for vertical blanks
 */
static u32 dm_vblank_get_counter(struct amdgpu_device *adev, int crtc)
{
	if (crtc >= adev->mode_info.num_crtc)
		return 0;
	else {
		struct amdgpu_crtc *acrtc = adev->mode_info.crtcs[crtc];

		if (NULL == acrtc->stream) {
			DRM_ERROR("dc_stream is NULL for crtc '%d'!\n", crtc);
			return 0;
		}

		return dc_stream_get_vblank_counter(acrtc->stream);
	}
}

static int dm_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
					u32 *vbl, u32 *position)
{
	uint32_t v_blank_start, v_blank_end, h_position, v_position;

	if ((crtc < 0) || (crtc >= adev->mode_info.num_crtc))
		return -EINVAL;
	else {
		struct amdgpu_crtc *acrtc = adev->mode_info.crtcs[crtc];

		if (NULL == acrtc->stream) {
			DRM_ERROR("dc_stream is NULL for crtc '%d'!\n", crtc);
			return 0;
		}

		/*
		 * TODO rework base driver to use values directly.
		 * for now parse it back into reg-format
		 */
		dc_stream_get_scanoutpos(acrtc->stream,
					 &v_blank_start,
					 &v_blank_end,
					 &h_position,
					 &v_position);

		*position = v_position | (h_position << 16);
		*vbl = v_blank_start | (v_blank_end << 16);
	}

	return 0;
}

static bool dm_is_idle(void *handle)
{
	/* XXX todo */
	return true;
}

static int dm_wait_for_idle(void *handle)
{
	/* XXX todo */
	return 0;
}

static bool dm_check_soft_reset(void *handle)
{
	return false;
}

static int dm_soft_reset(void *handle)
{
	/* XXX todo */
	return 0;
}

static struct amdgpu_crtc *get_crtc_by_otg_inst(
	struct amdgpu_device *adev,
	int otg_inst)
{
	struct drm_device *dev = adev->ddev;
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;

	/*
	 * following if is check inherited from both functions where this one is
	 * used now. Need to be checked why it could happen.
	 */
	if (otg_inst == -1) {
		WARN_ON(1);
		return adev->mode_info.crtcs[0];
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		amdgpu_crtc = to_amdgpu_crtc(crtc);

		if (amdgpu_crtc->otg_inst == otg_inst)
			return amdgpu_crtc;
	}

	return NULL;
}

static void dm_pflip_high_irq(void *interrupt_params)
{
	struct amdgpu_flip_work *works;
	struct amdgpu_crtc *amdgpu_crtc;
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	unsigned long flags;

	amdgpu_crtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_PFLIP);

	/* IRQ could occur when in initial stage */
	/*TODO work and BO cleanup */
	if (amdgpu_crtc == NULL) {
		DRM_DEBUG_DRIVER("CRTC is null, returning.\n");
		return;
	}

	spin_lock_irqsave(&adev->ddev->event_lock, flags);
	works = amdgpu_crtc->pflip_works;

	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED){
		DRM_DEBUG_DRIVER("amdgpu_crtc->pflip_status = %d !=AMDGPU_FLIP_SUBMITTED(%d) on crtc:%d[%p] \n",
						 amdgpu_crtc->pflip_status,
						 AMDGPU_FLIP_SUBMITTED,
						 amdgpu_crtc->crtc_id,
						 amdgpu_crtc);
		spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
		return;
	}

	/* page flip completed. clean up */
	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	amdgpu_crtc->pflip_works = NULL;

	/* wakeup usersapce */
	if (works->event)
		drm_crtc_send_vblank_event(&amdgpu_crtc->base,
					   works->event);

	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);

	DRM_DEBUG_DRIVER("%s - crtc :%d[%p], pflip_stat:AMDGPU_FLIP_NONE, work: %p,\n",
					__func__, amdgpu_crtc->crtc_id, amdgpu_crtc, works);

	drm_crtc_vblank_put(&amdgpu_crtc->base);
	schedule_work(&works->unpin_work);
}

static void dm_crtc_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	uint8_t crtc_index = 0;
	struct amdgpu_crtc *acrtc;

	acrtc = get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VBLANK);

	if (acrtc)
		crtc_index = acrtc->crtc_id;

	drm_handle_vblank(adev->ddev, crtc_index);
}

static int dm_set_clockgating_state(void *handle,
		  enum amd_clockgating_state state)
{
	return 0;
}

static int dm_set_powergating_state(void *handle,
		  enum amd_powergating_state state)
{
	return 0;
}

/* Prototypes of private functions */
static int dm_early_init(void* handle);

static void hotplug_notify_work_func(struct work_struct *work)
{
	struct amdgpu_display_manager *dm = container_of(work, struct amdgpu_display_manager, mst_hotplug_work);
	struct drm_device *dev = dm->ddev;

	drm_kms_helper_hotplug_event(dev);
}

#ifdef ENABLE_FBC
#include "dal_asic_id.h"
/* Allocate memory for FBC compressed data  */
/* TODO: Dynamic allocation */
#define AMDGPU_FBC_SIZE    (3840 * 2160 * 4)

void amdgpu_dm_initialize_fbc(struct amdgpu_device *adev)
{
	int r;
	struct dm_comressor_info *compressor = &adev->dm.compressor;

	if (!compressor->bo_ptr) {
		r = amdgpu_bo_create_kernel(adev, AMDGPU_FBC_SIZE, PAGE_SIZE,
				AMDGPU_GEM_DOMAIN_VRAM, &compressor->bo_ptr,
				&compressor->gpu_addr, &compressor->cpu_addr);

		if (r)
			DRM_ERROR("DM: Failed to initialize fbc\n");
	}

}
#endif


/* Init display KMS
 *
 * Returns 0 on success
 */
int amdgpu_dm_init(struct amdgpu_device *adev)
{
	struct dc_init_data init_data;
	adev->dm.ddev = adev->ddev;
	adev->dm.adev = adev;

	DRM_INFO("DAL is enabled\n");
	/* Zero all the fields */
	memset(&init_data, 0, sizeof(init_data));

	/* initialize DAL's lock (for SYNC context use) */
	spin_lock_init(&adev->dm.dal_lock);

	/* initialize DAL's mutex */
	mutex_init(&adev->dm.dal_mutex);

	if(amdgpu_dm_irq_init(adev)) {
		DRM_ERROR("amdgpu: failed to initialize DM IRQ support.\n");
		goto error;
	}

	init_data.asic_id.chip_family = adev->family;

	init_data.asic_id.pci_revision_id = adev->rev_id;
	init_data.asic_id.hw_internal_rev = adev->external_rev_id;

	init_data.asic_id.vram_width = adev->mc.vram_width;
	/* TODO: initialize init_data.asic_id.vram_type here!!!! */
	init_data.asic_id.atombios_base_address =
		adev->mode_info.atom_context->bios;

	init_data.driver = adev;

	adev->dm.cgs_device = amdgpu_cgs_create_device(adev);

	if (!adev->dm.cgs_device) {
		DRM_ERROR("amdgpu: failed to create cgs device.\n");
		goto error;
	}

	init_data.cgs_device = adev->dm.cgs_device;

	adev->dm.dal = NULL;

	init_data.dce_environment = DCE_ENV_PRODUCTION_DRV;

#ifdef ENABLE_FBC
	if (adev->family == FAMILY_CZ)
		amdgpu_dm_initialize_fbc(adev);
	init_data.fbc_gpu_addr = adev->dm.compressor.gpu_addr;
#endif
	/* Display Core create. */
	adev->dm.dc = dc_create(&init_data);

	if (!adev->dm.dc)
		DRM_INFO("Display Core failed to initialize!\n");

	INIT_WORK(&adev->dm.mst_hotplug_work, hotplug_notify_work_func);

	adev->dm.freesync_module = mod_freesync_create(adev->dm.dc);
	if (!adev->dm.freesync_module) {
		DRM_ERROR(
		"amdgpu: failed to initialize freesync_module.\n");
	} else
		DRM_INFO("amdgpu: freesync_module init done %p.\n",
				adev->dm.freesync_module);

	if (amdgpu_dm_initialize_drm_device(adev)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

	/* Update the actual used number of crtc */
	adev->mode_info.num_crtc = adev->dm.display_indexes_num;

	/* TODO: Add_display_info? */

	/* TODO use dynamic cursor width */
	adev->ddev->mode_config.cursor_width = adev->dm.dc->caps.max_cursor_size;
	adev->ddev->mode_config.cursor_height = adev->dm.dc->caps.max_cursor_size;

	if (drm_vblank_init(adev->ddev, adev->dm.display_indexes_num)) {
		DRM_ERROR(
		"amdgpu: failed to initialize sw for display support.\n");
		goto error;
	}

	DRM_INFO("KMS initialized.\n");

	return 0;
error:
	amdgpu_dm_fini(adev);

	return -1;
}

void amdgpu_dm_fini(struct amdgpu_device *adev)
{
	amdgpu_dm_destroy_drm_device(&adev->dm);
	/*
	 * TODO: pageflip, vlank interrupt
	 *
	 * amdgpu_dm_irq_fini(adev);
	 */

	if (adev->dm.cgs_device) {
		amdgpu_cgs_destroy_device(adev->dm.cgs_device);
		adev->dm.cgs_device = NULL;
	}
	if (adev->dm.freesync_module) {
		mod_freesync_destroy(adev->dm.freesync_module);
		adev->dm.freesync_module = NULL;
	}
	/* DC Destroy TODO: Replace destroy DAL */
	{
		dc_destroy(&adev->dm.dc);
	}
	return;
}

/* moved from amdgpu_dm_kms.c */
void amdgpu_dm_destroy()
{
}

static int dm_sw_init(void *handle)
{
	return 0;
}

static int dm_sw_fini(void *handle)
{
	return 0;
}

static int detect_mst_link_for_all_connectors(struct drm_device *dev)
{
	struct amdgpu_connector *aconnector;
	struct drm_connector *connector;
	int ret = 0;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		   aconnector = to_amdgpu_connector(connector);
		if (aconnector->dc_link->type == dc_connection_mst_branch) {
			DRM_INFO("DM_MST: starting TM on aconnector: %p [id: %d]\n",
					aconnector, aconnector->base.base.id);

			ret = drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, true);
			if (ret < 0) {
				DRM_ERROR("DM_MST: Failed to start MST\n");
				((struct dc_link *)aconnector->dc_link)->type = dc_connection_single;
				return ret;
				}
			}
	}

	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	return ret;
}

static int dm_late_init(void *handle)
{
	struct drm_device *dev = ((struct amdgpu_device *)handle)->ddev;
	int r = detect_mst_link_for_all_connectors(dev);

	return r;
}

static void s3_handle_mst(struct drm_device *dev, bool suspend)
{
	struct amdgpu_connector *aconnector;
	struct drm_connector *connector;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		   aconnector = to_amdgpu_connector(connector);
		   if (aconnector->dc_link->type == dc_connection_mst_branch &&
				   !aconnector->mst_port) {

			   if (suspend)
				   drm_dp_mst_topology_mgr_suspend(&aconnector->mst_mgr);
			   else
				   drm_dp_mst_topology_mgr_resume(&aconnector->mst_mgr);
		   }
	}

	drm_modeset_unlock(&dev->mode_config.connection_mutex);
}

static int dm_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	/* Create DAL display manager */
	amdgpu_dm_init(adev);
	amdgpu_dm_hpd_init(adev);

	return 0;
}

static int dm_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_dm_hpd_fini(adev);

	amdgpu_dm_irq_fini(adev);

	return 0;
}

static int dm_suspend(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct amdgpu_display_manager *dm = &adev->dm;
	int ret = 0;
	struct drm_crtc *crtc;

	s3_handle_mst(adev->ddev, true);

	/* flash all pending vblank events and turn interrupt off
	 * before disabling CRTCs. They will be enabled back in
	 * dm_display_resume
	 */
	drm_modeset_lock_all(adev->ddev);
	list_for_each_entry(crtc, &adev->ddev->mode_config.crtc_list, head) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
		if (acrtc->stream)
				drm_crtc_vblank_off(crtc);
	}
	drm_modeset_unlock_all(adev->ddev);

	amdgpu_dm_irq_suspend(adev);

	dc_set_power_state(
		dm->dc,
		DC_ACPI_CM_POWER_STATE_D3,
		DC_VIDEO_POWER_SUSPEND);

	return ret;
}

struct amdgpu_connector *amdgpu_dm_find_first_crct_matching_connector(
	struct drm_atomic_state *state,
	struct drm_crtc *crtc,
	bool from_state_var)
{
	uint32_t i;
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	struct drm_crtc *crtc_from_state;

	for_each_connector_in_state(
		state,
		connector,
		conn_state,
		i) {
		crtc_from_state =
			from_state_var ?
				conn_state->crtc :
				connector->state->crtc;

		if (crtc_from_state == crtc)
			return to_amdgpu_connector(connector);
	}

	return NULL;
}

static int dm_display_resume(struct drm_device *ddev)
{
	int ret = 0;
	struct drm_connector *connector;

	struct drm_atomic_state *state = drm_atomic_state_alloc(ddev);
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct amdgpu_connector *aconnector;
	struct drm_connector_state *conn_state;

	if (!state)
		return ENOMEM;

	state->acquire_ctx = ddev->mode_config.acquire_ctx;

	/* Construct an atomic state to restore previous display setting */

	/*
	 * Attach connectors to drm_atomic_state
	 * Should be done in the first place in order to make connectors
	 * available in state during crtc state processing. It is used for
	 * making decision if crtc should be disabled in case sink got
	 * disconnected.
	 *
	 * Connectors state crtc with NULL dc_sink should be cleared, because it
	 * will fail validation during commit
	 */
	list_for_each_entry(connector, &ddev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_connector(connector);
		conn_state = drm_atomic_get_connector_state(state, connector);

		ret = PTR_ERR_OR_ZERO(conn_state);
		if (ret)
			goto err;
	}

	/* Attach crtcs to drm_atomic_state*/
	list_for_each_entry(crtc, &ddev->mode_config.crtc_list, head) {
		struct drm_crtc_state *crtc_state =
			drm_atomic_get_crtc_state(state, crtc);

		ret = PTR_ERR_OR_ZERO(crtc_state);
		if (ret)
			goto err;

		/* force a restore */
		crtc_state->mode_changed = true;
	}


	/* Attach planes to drm_atomic_state */
	list_for_each_entry(plane, &ddev->mode_config.plane_list, head) {

		struct drm_crtc *crtc;
		struct drm_gem_object *obj;
		struct drm_framebuffer *fb;
		struct amdgpu_framebuffer *afb;
		struct amdgpu_bo *rbo;
		int r;
		struct drm_plane_state *plane_state = drm_atomic_get_plane_state(state, plane);

		ret = PTR_ERR_OR_ZERO(plane_state);
		if (ret)
			goto err;

		crtc = plane_state->crtc;
		fb = plane_state->fb;

		if (!crtc || !crtc->state || !crtc->state->active)
			continue;

		if (!fb) {
			DRM_DEBUG_KMS("No FB bound\n");
			return 0;
		}

		/*
		 * Pin back the front buffers, cursor buffer was already pinned
		 * back in amdgpu_resume_kms
		 */

		afb = to_amdgpu_framebuffer(fb);

		obj = afb->obj;
		rbo = gem_to_amdgpu_bo(obj);
		r = amdgpu_bo_reserve(rbo, false);
		if (unlikely(r != 0))
		       return r;

		r = amdgpu_bo_pin(rbo, AMDGPU_GEM_DOMAIN_VRAM, NULL);

		amdgpu_bo_unreserve(rbo);

		if (unlikely(r != 0)) {
			DRM_ERROR("Failed to pin framebuffer\n");
			return r;
		}

	}


	/* Call commit internally with the state we just constructed */
	ret = drm_atomic_commit(state);
	if (!ret) {
		/* Enable vblank after pipes powered back on */
		list_for_each_entry(crtc, &ddev->mode_config.crtc_list, head) {
			struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
			if (acrtc->stream)
				drm_crtc_vblank_on(crtc);
		}
		return 0;
        }


err:
	DRM_ERROR("Restoring old state failed with %i\n", ret);
	drm_atomic_state_free(state);

	return ret;
}

static int dm_resume(void *handle)
{
	struct amdgpu_device *adev = handle;
	struct amdgpu_display_manager *dm = &adev->dm;

	/* power on hardware */
	dc_set_power_state(
		dm->dc,
		DC_ACPI_CM_POWER_STATE_D0,
		DC_VIDEO_POWER_ON);

	return 0;
}

int amdgpu_dm_display_resume(struct amdgpu_device *adev )
{
	struct drm_device *ddev = adev->ddev;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct amdgpu_connector *aconnector;
	struct drm_connector *connector;
	int ret = 0;
	struct drm_crtc *crtc;

	/* program HPD filter */
	dc_resume(dm->dc);

	/* On resume we need to  rewrite the MSTM control bits to enamble MST*/
	s3_handle_mst(ddev, false);

	/*
	 * early enable HPD Rx IRQ, should be done before set mode as short
	 * pulse interrupts are used for MST
	 */
	amdgpu_dm_irq_resume_early(adev);

	/* Do detection*/
	list_for_each_entry(connector,
			&ddev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_connector(connector);

		/*
		 * this is the case when traversing through already created
		 * MST connectors, should be skipped
		 */
		if (aconnector->mst_port)
			continue;

		dc_link_detect(aconnector->dc_link, false);
		aconnector->dc_sink = NULL;
		amdgpu_dm_update_connector_after_detect(aconnector);
	}

	drm_modeset_lock_all(ddev);
	ret = dm_display_resume(ddev);
	drm_modeset_unlock_all(ddev);

	amdgpu_dm_irq_resume_late(adev);

	return ret;
}

static const struct amd_ip_funcs amdgpu_dm_funcs = {
	.name = "dm",
	.early_init = dm_early_init,
	.late_init = dm_late_init,
	.sw_init = dm_sw_init,
	.sw_fini = dm_sw_fini,
	.hw_init = dm_hw_init,
	.hw_fini = dm_hw_fini,
	.suspend = dm_suspend,
	.resume = dm_resume,
	.is_idle = dm_is_idle,
	.wait_for_idle = dm_wait_for_idle,
	.check_soft_reset = dm_check_soft_reset,
	.soft_reset = dm_soft_reset,
	.set_clockgating_state = dm_set_clockgating_state,
	.set_powergating_state = dm_set_powergating_state,
};

const struct amdgpu_ip_block_version dm_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &amdgpu_dm_funcs,
};

static const struct drm_mode_config_funcs amdgpu_dm_mode_funcs = {
	.fb_create = amdgpu_user_framebuffer_create,
	.output_poll_changed = amdgpu_output_poll_changed,
	.atomic_check = amdgpu_dm_atomic_check,
	.atomic_commit = amdgpu_dm_atomic_commit
};

void amdgpu_dm_update_connector_after_detect(
	struct amdgpu_connector *aconnector)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct dc_sink *sink;

	/* MST handled by drm_mst framework */
	if (aconnector->mst_mgr.mst_state == true)
		return;


	sink = aconnector->dc_link->local_sink;

	/* Edid mgmt connector gets first update only in mode_valid hook and then
	 * the connector sink is set to either fake or physical sink depends on link status.
	 * don't do it here if u are during boot
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED
			&& aconnector->dc_em_sink) {

		/* For S3 resume with headless use eml_sink to fake stream
		 * because on resume connecotr->sink is set ti NULL
		 */
		mutex_lock(&dev->mode_config.mutex);

		if (sink) {
			if (aconnector->dc_sink) {
				amdgpu_dm_remove_sink_from_freesync_module(
								connector);
				/* retain and release bellow are used for
				 * bump up refcount for sink because the link don't point
				 * to it anymore after disconnect so on next crtc to connector
				 * reshuffle by UMD we will get into unwanted dc_sink release
				 */
				if (aconnector->dc_sink != aconnector->dc_em_sink)
					dc_sink_release(aconnector->dc_sink);
			}
			aconnector->dc_sink = sink;
			amdgpu_dm_add_sink_to_freesync_module(
						connector, aconnector->edid);
		} else {
			amdgpu_dm_remove_sink_from_freesync_module(connector);
			if (!aconnector->dc_sink)
				aconnector->dc_sink = aconnector->dc_em_sink;
			else if (aconnector->dc_sink != aconnector->dc_em_sink)
				dc_sink_retain(aconnector->dc_sink);
		}

		mutex_unlock(&dev->mode_config.mutex);
		return;
	}

	/*
	 * TODO: temporary guard to look for proper fix
	 * if this sink is MST sink, we should not do anything
	 */
	if (sink && sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		return;

	if (aconnector->dc_sink == sink) {
		/* We got a DP short pulse (Link Loss, DP CTS, etc...).
		 * Do nothing!! */
		DRM_INFO("DCHPD: connector_id=%d: dc_sink didn't change.\n",
				aconnector->connector_id);
		return;
	}

	DRM_INFO("DCHPD: connector_id=%d: Old sink=%p New sink=%p\n",
		aconnector->connector_id, aconnector->dc_sink, sink);

	mutex_lock(&dev->mode_config.mutex);

	/* 1. Update status of the drm connector
	 * 2. Send an event and let userspace tell us what to do */
	if (sink) {
		/* TODO: check if we still need the S3 mode update workaround.
		 * If yes, put it here. */
		if (aconnector->dc_sink)
			amdgpu_dm_remove_sink_from_freesync_module(
							connector);

		aconnector->dc_sink = sink;
		if (sink->dc_edid.length == 0)
			aconnector->edid = NULL;
		else {
			aconnector->edid =
				(struct edid *) sink->dc_edid.raw_edid;


			drm_mode_connector_update_edid_property(connector,
					aconnector->edid);
		}
		amdgpu_dm_add_sink_to_freesync_module(connector, aconnector->edid);

	} else {
		amdgpu_dm_remove_sink_from_freesync_module(connector);
		drm_mode_connector_update_edid_property(connector, NULL);
		aconnector->num_modes = 0;
		aconnector->dc_sink = NULL;
	}

	mutex_unlock(&dev->mode_config.mutex);
}

static void handle_hpd_irq(void *param)
{
	struct amdgpu_connector *aconnector = (struct amdgpu_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;

	/* In case of failure or MST no need to update connector status or notify the OS
	 * since (for MST case) MST does this in it's own context.
	 */
	mutex_lock(&aconnector->hpd_lock);
	if (dc_link_detect(aconnector->dc_link, false)) {
		amdgpu_dm_update_connector_after_detect(aconnector);


		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
			drm_kms_helper_hotplug_event(dev);
	}
	mutex_unlock(&aconnector->hpd_lock);

}

static void dm_handle_hpd_rx_irq(struct amdgpu_connector *aconnector)
{
	uint8_t esi[DP_PSR_ERROR_STATUS - DP_SINK_COUNT_ESI] = { 0 };
	uint8_t dret;
	bool new_irq_handled = false;
	int dpcd_addr;
	int dpcd_bytes_to_read;

	const int max_process_count = 30;
	int process_count = 0;

	const struct dc_link_status *link_status = dc_link_get_status(aconnector->dc_link);

	if (link_status->dpcd_caps->dpcd_rev.raw < 0x12) {
		dpcd_bytes_to_read = DP_LANE0_1_STATUS - DP_SINK_COUNT;
		/* DPCD 0x200 - 0x201 for downstream IRQ */
		dpcd_addr = DP_SINK_COUNT;
	} else {
		dpcd_bytes_to_read = DP_PSR_ERROR_STATUS - DP_SINK_COUNT_ESI;
		/* DPCD 0x2002 - 0x2005 for downstream IRQ */
		dpcd_addr = DP_SINK_COUNT_ESI;
	}

	dret = drm_dp_dpcd_read(
		&aconnector->dm_dp_aux.aux,
		dpcd_addr,
		esi,
		dpcd_bytes_to_read);

	while (dret == dpcd_bytes_to_read &&
		process_count < max_process_count) {
		uint8_t retry;
		dret = 0;

		process_count++;

		DRM_DEBUG_KMS("ESI %02x %02x %02x\n", esi[0], esi[1], esi[2]);
		/* handle HPD short pulse irq */
		if (aconnector->mst_mgr.mst_state)
			drm_dp_mst_hpd_irq(
				&aconnector->mst_mgr,
				esi,
				&new_irq_handled);

		if (new_irq_handled) {
			/* ACK at DPCD to notify down stream */
			const int ack_dpcd_bytes_to_write =
				dpcd_bytes_to_read - 1;

			for (retry = 0; retry < 3; retry++) {
				uint8_t wret;

				wret = drm_dp_dpcd_write(
					&aconnector->dm_dp_aux.aux,
					dpcd_addr + 1,
					&esi[1],
					ack_dpcd_bytes_to_write);
				if (wret == ack_dpcd_bytes_to_write)
					break;
			}

			/* check if there is new irq to be handle */
			dret = drm_dp_dpcd_read(
				&aconnector->dm_dp_aux.aux,
				dpcd_addr,
				esi,
				dpcd_bytes_to_read);

			new_irq_handled = false;
		} else
			break;
	}

	if (process_count == max_process_count)
		DRM_DEBUG_KMS("Loop exceeded max iterations\n");
}

static void handle_hpd_rx_irq(void *param)
{
	struct amdgpu_connector *aconnector = (struct amdgpu_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	const struct dc_link *dc_link = aconnector->dc_link;
	bool is_mst_root_connector = aconnector->mst_mgr.mst_state;

	/* TODO:Temporary add mutex to protect hpd interrupt not have a gpio
	 * conflict, after implement i2c helper, this mutex should be
	 * retired.
	 */
	if (aconnector->dc_link->type != dc_connection_mst_branch)
		mutex_lock(&aconnector->hpd_lock);

	if (dc_link_handle_hpd_rx_irq(aconnector->dc_link, NULL) &&
			!is_mst_root_connector) {
		/* Downstream Port status changed. */
		if (dc_link_detect(aconnector->dc_link, false)) {
			amdgpu_dm_update_connector_after_detect(aconnector);


			drm_modeset_lock_all(dev);
			dm_restore_drm_connector_state(dev, connector);
			drm_modeset_unlock_all(dev);

			drm_kms_helper_hotplug_event(dev);
		}
	}
	if ((dc_link->cur_link_settings.lane_count != LANE_COUNT_UNKNOWN) ||
				(dc_link->type == dc_connection_mst_branch))
		dm_handle_hpd_rx_irq(aconnector);

	if (aconnector->dc_link->type != dc_connection_mst_branch)
		mutex_unlock(&aconnector->hpd_lock);
}

static void register_hpd_handlers(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev->ddev;
	struct drm_connector *connector;
	struct amdgpu_connector *aconnector;
	const struct dc_link *dc_link;
	struct dc_interrupt_params int_params = {0};

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head)	{

		aconnector = to_amdgpu_connector(connector);
		dc_link = aconnector->dc_link;

		if (DC_IRQ_SOURCE_INVALID != dc_link->irq_source_hpd) {
			int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
			int_params.irq_source = dc_link->irq_source_hpd;

			amdgpu_dm_irq_register_interrupt(adev, &int_params,
					handle_hpd_irq,
					(void *) aconnector);
		}

		if (DC_IRQ_SOURCE_INVALID != dc_link->irq_source_hpd_rx) {

			/* Also register for DP short pulse (hpd_rx). */
			int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
			int_params.irq_source =	dc_link->irq_source_hpd_rx;

			amdgpu_dm_irq_register_interrupt(adev, &int_params,
					handle_hpd_rx_irq,
					(void *) aconnector);
		}
	}
}

/* Register IRQ sources and initialize IRQ callbacks */
static int dce110_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
	unsigned client_id = AMDGPU_IH_CLIENTID_LEGACY;

	if (adev->asic_type == CHIP_VEGA10 ||
	    adev->asic_type == CHIP_RAVEN)
		client_id = AMDGPU_IH_CLIENTID_DCE;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/* Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling. */

	/* Use VBLANK interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_VERTICAL_INTERRUPT0; i <= VISLANDS30_IV_SRCID_D6_VERTICAL_INTERRUPT0; i++) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->crtc_irq);

		if (r) {
			DRM_ERROR("Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.vblank_params[int_params.irq_source - DC_IRQ_SOURCE_VBLANK1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_crtc_high_irq, c_irq_params);
	}

	/* Use GRPH_PFLIP interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_GRPH_PFLIP;
			i <= VISLANDS30_IV_SRCID_D6_GRPH_PFLIP; i += 2) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->pageflip_irq);
		if (r) {
			DRM_ERROR("Failed to add page flip irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DC_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_pflip_high_irq, c_irq_params);

	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, client_id,
			VISLANDS30_IV_SRCID_HOTPLUG_DETECT_A, &adev->hpd_irq);
	if (r) {
		DRM_ERROR("Failed to add hpd irq id!\n");
		return r;
	}

	register_hpd_handlers(adev);

	return 0;
}

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
/* Register IRQ sources and initialize IRQ callbacks */
static int dcn10_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/* Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling.
	 * */

	/* Use VSTARTUP interrupt */
	for (i = DCN_1_0__SRCID__DC_D1_OTG_VSTARTUP;
			i <= DCN_1_0__SRCID__DC_D1_OTG_VSTARTUP + adev->mode_info.num_crtc - 1;
			i++) {
		r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_DCE, i, &adev->crtc_irq);

		if (r) {
			DRM_ERROR("Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.vblank_params[int_params.irq_source - DC_IRQ_SOURCE_VBLANK1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_crtc_high_irq, c_irq_params);
	}

	/* Use GRPH_PFLIP interrupt */
	for (i = DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT;
			i <= DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT + adev->mode_info.num_crtc - 1;
			i++) {
		r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_DCE, i, &adev->pageflip_irq);
		if (r) {
			DRM_ERROR("Failed to add page flip irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DC_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_pflip_high_irq, c_irq_params);

	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_DCE, DCN_1_0__SRCID__DC_HPD1_INT,
			&adev->hpd_irq);
	if (r) {
		DRM_ERROR("Failed to add hpd irq id!\n");
		return r;
	}

	register_hpd_handlers(adev);

	return 0;
}
#endif

static int amdgpu_dm_mode_config_init(struct amdgpu_device *adev)
{
	int r;

	adev->mode_info.mode_config_initialized = true;

	adev->ddev->mode_config.funcs = (void *)&amdgpu_dm_mode_funcs;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	adev->ddev->mode_config.preferred_depth = 24;
	adev->ddev->mode_config.prefer_shadow = 1;
	/* indicate support of immediate flip */
	adev->ddev->mode_config.async_page_flip = true;

	adev->ddev->mode_config.fb_base = adev->mc.aper_base;

	r = amdgpu_modeset_create_props(adev);
	if (r)
		return r;

	return 0;
}

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

static int amdgpu_dm_backlight_update_status(struct backlight_device *bd)
{
	struct amdgpu_display_manager *dm = bl_get_data(bd);

	if (dc_link_set_backlight_level(dm->backlight_link,
			bd->props.brightness, 0, 0))
		return 0;
	else
		return 1;
}

static int amdgpu_dm_backlight_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)
static struct backlight_ops amdgpu_dm_backlight_ops = {
#else
static const struct backlight_ops amdgpu_dm_backlight_ops = {
#endif
	.get_brightness = amdgpu_dm_backlight_get_brightness,
	.update_status	= amdgpu_dm_backlight_update_status,
};

void amdgpu_dm_register_backlight_device(struct amdgpu_display_manager *dm)
{
	char bl_name[16];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
	struct backlight_properties props = { 0 };

	props.max_brightness = AMDGPU_MAX_BL_LEVEL;
	props.type = BACKLIGHT_RAW;
#endif

	snprintf(bl_name, sizeof(bl_name), "amdgpu_bl%d",
			dm->adev->ddev->primary->index);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
	dm->backlight_dev = backlight_device_register(bl_name,
			dm->adev->ddev->dev,
			dm,
			&amdgpu_dm_backlight_ops,
			&props);
#else
	dm->backlight_dev = backlight_device_register(bl_name,
			dm->adev->ddev->dev,
			dm,
			&amdgpu_dm_backlight_ops);
#endif

	if (NULL == dm->backlight_dev)
		DRM_ERROR("DM: Backlight registration failed!\n");
	else
		DRM_INFO("DM: Registered Backlight device: %s\n", bl_name);
}

#endif

/* In this architecture, the association
 * connector -> encoder -> crtc
 * id not really requried. The crtc and connector will hold the
 * display_index as an abstraction to use with DAL component
 *
 * Returns 0 on success
 */
int amdgpu_dm_initialize_drm_device(struct amdgpu_device *adev)
{
	struct amdgpu_display_manager *dm = &adev->dm;
	uint32_t i;
	struct amdgpu_connector *aconnector = NULL;
	struct amdgpu_encoder *aencoder = NULL;
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	uint32_t link_cnt;
	unsigned long possible_crtcs;

	link_cnt = dm->dc->caps.max_links;
	if (amdgpu_dm_mode_config_init(dm->adev)) {
		DRM_ERROR("DM: Failed to initialize mode config\n");
		return -1;
	}

	for (i = 0; i < dm->dc->caps.max_planes; i++) {
		mode_info->planes[i] = kzalloc(sizeof(struct amdgpu_plane),
								 GFP_KERNEL);
		if (!mode_info->planes[i]) {
			DRM_ERROR("KMS: Failed to allocate plane\n");
			goto fail_free_planes;
		}
		mode_info->planes[i]->base.type = mode_info->plane_type[i];

		/*
		 * HACK: IGT tests expect that each plane can only have one
		 * one possible CRTC. For now, set one CRTC for each
		 * plane that is not an underlay, but still allow multiple
		 * CRTCs for underlay planes.
		 */
		possible_crtcs = 1 << i;
		if (i >= dm->dc->caps.max_streams)
			possible_crtcs = 0xff;

		if (amdgpu_dm_plane_init(dm, mode_info->planes[i], possible_crtcs)) {
			DRM_ERROR("KMS: Failed to initialize plane\n");
			goto fail_free_planes;
		}
	}

	for (i = 0; i < dm->dc->caps.max_streams; i++)
		if (amdgpu_dm_crtc_init(dm, &mode_info->planes[i]->base, i)) {
			DRM_ERROR("KMS: Failed to initialize crtc\n");
			goto fail_free_planes;
		}

	dm->display_indexes_num = dm->dc->caps.max_streams;

	/* loops over all connectors on the board */
	for (i = 0; i < link_cnt; i++) {

		if (i > KCL_DM_MAX_DISPLAY_INDEX) {
			DRM_ERROR(
				"KMS: Cannot support more than %d display indexes\n",
					KCL_DM_MAX_DISPLAY_INDEX);
			continue;
		}

		aconnector = kzalloc(sizeof(*aconnector), GFP_KERNEL);
		if (!aconnector)
			goto fail_free_planes;

		aencoder = kzalloc(sizeof(*aencoder), GFP_KERNEL);
		if (!aencoder) {
			goto fail_free_connector;
		}

		if (amdgpu_dm_encoder_init(dm->ddev, aencoder, i)) {
			DRM_ERROR("KMS: Failed to initialize encoder\n");
			goto fail_free_encoder;
		}

		if (amdgpu_dm_connector_init(dm, aconnector, i, aencoder)) {
			DRM_ERROR("KMS: Failed to initialize connector\n");
			goto fail_free_encoder;
		}

		if (dc_link_detect(dc_get_link_at_index(dm->dc, i), true))
			amdgpu_dm_update_connector_after_detect(aconnector);
	}

	/* Software is initialized. Now we can register interrupt handlers. */
	switch (adev->asic_type) {
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
	case CHIP_POLARIS12:
	case CHIP_VEGA10:
		if (dce110_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail_free_encoder;
		}
		break;
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case CHIP_RAVEN:
		if (dcn10_register_irq_handlers(dm->adev)) {
			DRM_ERROR("DM: Failed to initialize IRQ\n");
			goto fail_free_encoder;
		}
		break;
#endif
	default:
		DRM_ERROR("Usupported ASIC type: 0x%X\n", adev->asic_type);
		goto fail_free_encoder;
	}

	drm_mode_config_reset(dm->ddev);

	return 0;
fail_free_encoder:
	kfree(aencoder);
fail_free_connector:
	kfree(aconnector);
fail_free_planes:
	for (i = 0; i < dm->dc->caps.max_planes; i++)
		kfree(mode_info->planes[i]);
	return -1;
}

void amdgpu_dm_destroy_drm_device(struct amdgpu_display_manager *dm)
{
	drm_mode_config_cleanup(dm->ddev);
	return;
}

/******************************************************************************
 * amdgpu_display_funcs functions
 *****************************************************************************/

/**
 * dm_bandwidth_update - program display watermarks
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate and program the display watermarks and line buffer allocation.
 */
static void dm_bandwidth_update(struct amdgpu_device *adev)
{
	/* TODO: implement later */
}

static void dm_set_backlight_level(struct amdgpu_encoder *amdgpu_encoder,
				     u8 level)
{
	/* TODO: translate amdgpu_encoder to display_index and call DAL */
}

static u8 dm_get_backlight_level(struct amdgpu_encoder *amdgpu_encoder)
{
	/* TODO: translate amdgpu_encoder to display_index and call DAL */
	return 0;
}

/******************************************************************************
 * Page Flip functions
 ******************************************************************************/

/**
 * dm_page_flip - called by amdgpu_flip_work_func(), which is triggered
 * 			via DRM IOCTL, by user mode.
 *
 * @adev: amdgpu_device pointer
 * @crtc_id: crtc to cleanup pageflip on
 * @crtc_base: new address of the crtc (GPU MC address)
 *
 * Does the actual pageflip (surface address update).
 */
static void dm_page_flip(struct amdgpu_device *adev,
			 int crtc_id, u64 crtc_base, bool async)
{
	struct amdgpu_crtc *acrtc;
	struct dc_stream_state *stream;
	struct dc_flip_addrs addr = { {0} };

	/*
	 * TODO risk of concurrency issues
	 *
	 * This should guarded by the dal_mutex but we can't do this since the
	 * caller uses a spin_lock on event_lock.
	 *
	 * If we wait on the dal_mutex a second page flip interrupt might come,
	 * spin on the event_lock, disabling interrupts while it does so. At
	 * this point the core can no longer be pre-empted and return to the
	 * thread that waited on the dal_mutex and we're deadlocked.
	 *
	 * With multiple cores the same essentially happens but might just take
	 * a little longer to lock up all cores.
	 *
	 * The reason we should lock on dal_mutex is so that we can be sure
	 * nobody messes with acrtc->stream after we read and check its value.
	 *
	 * We might be able to fix our concurrency issues with a work queue
	 * where we schedule all work items (mode_set, page_flip, etc.) and
	 * execute them one by one. Care needs to be taken to still deal with
	 * any potential concurrency issues arising from interrupt calls.
	 */

	acrtc = adev->mode_info.crtcs[crtc_id];
	stream = acrtc->stream;

	/*
	 * Received a page flip call after the display has been reset.
	 * Just return in this case. Everything should be clean-up on reset.
	 */

	if (!stream) {
		WARN_ON(1);
		return;
	}

	addr.address.grph.addr.low_part = lower_32_bits(crtc_base);
	addr.address.grph.addr.high_part = upper_32_bits(crtc_base);
	addr.flip_immediate = async;

	DRM_DEBUG_DRIVER("%s Flipping to hi: 0x%x, low: 0x%x \n",
			 __func__,
			 addr.address.grph.addr.high_part,
			 addr.address.grph.addr.low_part);

	dc_flip_plane_addrs(
			adev->dm.dc,
			dc_stream_get_status(stream)->plane_states,
			&addr, 1);
}

static int amdgpu_notify_freesync(struct drm_device *dev, void *data,
				struct drm_file *filp)
{
	struct mod_freesync_params freesync_params;
	uint8_t num_streams;
	uint8_t i;

	struct amdgpu_device *adev = dev->dev_private;
	struct drm_amdgpu_freesync *args = data;
	int r = 0;

	freesync_params.state  = FREESYNC_STATE_FULLSCREEN;
	if (args->op == AMDGPU_FREESYNC_FULLSCREEN_ENTER)
		freesync_params.enable = true;
	else
		freesync_params.enable = false;

	num_streams = dc_get_current_stream_count(adev->dm.dc);

	for (i = 0; i < num_streams; i++) {
		struct dc_stream_state *stream;
		stream = dc_get_stream_at_index(adev->dm.dc, i);

		mod_freesync_update_state(adev->dm.freesync_module,
					  &stream, 1, &freesync_params);
	}

	return r;
}

static const struct amdgpu_display_funcs dm_display_funcs = {
	.bandwidth_update = dm_bandwidth_update, /* called unconditionally */
	.vblank_get_counter = dm_vblank_get_counter,/* called unconditionally */
	.vblank_wait = NULL,
	.backlight_set_level =
		dm_set_backlight_level,/* called unconditionally */
	.backlight_get_level =
		dm_get_backlight_level,/* called unconditionally */
	.hpd_sense = NULL,/* called unconditionally */
	.hpd_set_polarity = NULL, /* called unconditionally */
	.hpd_get_gpio_reg = NULL, /* VBIOS parsing. DAL does it. */
	.page_flip = dm_page_flip, /* called unconditionally */
	.page_flip_get_scanoutpos =
		dm_crtc_get_scanoutpos,/* called unconditionally */
	.add_encoder = NULL, /* VBIOS parsing. DAL does it. */
	.add_connector = NULL, /* VBIOS parsing. DAL does it. */
	.notify_freesync = amdgpu_notify_freesync,

};


#if defined(CONFIG_DEBUG_KERNEL_DC)

static ssize_t s3_debug_store(
	struct device *device,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret;
	int s3_state;
	struct pci_dev *pdev = to_pci_dev(device);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct amdgpu_device *adev = drm_dev->dev_private;

	ret = kstrtoint(buf, 0, &s3_state);

	if (ret == 0) {
		if (s3_state) {
			dm_resume(adev);
			amdgpu_dm_display_resume(adev);
			drm_kms_helper_hotplug_event(adev->ddev);
		} else
			dm_suspend(adev);
	}

	return ret == 0 ? count : 0;
}

DEVICE_ATTR_WO(s3_debug);

#endif

static int dm_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_dm_set_irq_funcs(adev);

	switch (adev->asic_type) {
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_KAVERI:
		adev->mode_info.num_crtc = 4;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_FIJI:
	case CHIP_TONGA:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_CARRIZO:
		adev->mode_info.num_crtc = 3;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		adev->mode_info.plane_type = dm_plane_type_carizzo;
		break;
	case CHIP_STONEY:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		adev->mode_info.plane_type = dm_plane_type_stoney;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		adev->mode_info.num_crtc = 5;
		adev->mode_info.num_hpd = 5;
		adev->mode_info.num_dig = 5;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_POLARIS10:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
	case CHIP_VEGA10:
		adev->mode_info.num_crtc = 6;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case CHIP_RAVEN:
		adev->mode_info.num_crtc = 4;
		adev->mode_info.num_hpd = 4;
		adev->mode_info.num_dig = 4;
		adev->mode_info.plane_type = dm_plane_type_default;
		break;
#endif
	default:
		DRM_ERROR("Usupported ASIC type: 0x%X\n", adev->asic_type);
		return -EINVAL;
	}

	if (adev->mode_info.funcs == NULL)
		adev->mode_info.funcs = &dm_display_funcs;

	/* Note: Do NOT change adev->audio_endpt_rreg and
	 * adev->audio_endpt_wreg because they are initialised in
	 * amdgpu_device_init() */
#if defined(CONFIG_DEBUG_KERNEL_DC)
	device_create_file(
		adev->ddev->dev,
		&dev_attr_s3_debug);
#endif

	return 0;
}

bool amdgpu_dm_acquire_dal_lock(struct amdgpu_display_manager *dm)
{
	/* TODO */
	return true;
}

bool amdgpu_dm_release_dal_lock(struct amdgpu_display_manager *dm)
{
	/* TODO */
	return true;
}

struct dm_connector_state {
	struct drm_connector_state base;

	enum amdgpu_rmx_type scaling;
	uint8_t underscan_vborder;
	uint8_t underscan_hborder;
	bool underscan_enable;
};

#define to_dm_connector_state(x)\
	container_of((x), struct dm_connector_state, base)

#define AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET 1

void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

static void dm_set_cursor(
	struct amdgpu_crtc *amdgpu_crtc,
	uint64_t gpu_addr,
	uint32_t width,
	uint32_t height)
{
	struct dc_cursor_attributes attributes;
	struct dc_cursor_position position;
	struct drm_crtc *crtc = &amdgpu_crtc->base;
	int x, y;
	int xorigin = 0, yorigin = 0;

	amdgpu_crtc->cursor_width = width;
	amdgpu_crtc->cursor_height = height;

	attributes.address.high_part = upper_32_bits(gpu_addr);
	attributes.address.low_part  = lower_32_bits(gpu_addr);
	attributes.width             = width;
	attributes.height            = height;
	attributes.color_format      = CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA;
	attributes.rotation_angle    = 0;
	attributes.attribute_flags.value = 0;

	attributes.pitch = attributes.width;

	x = amdgpu_crtc->cursor_x;
	y = amdgpu_crtc->cursor_y;

	/* avivo cursor are offset into the total surface */
	x += crtc->primary->state->src_x >> 16;
	y += crtc->primary->state->src_y >> 16;

	if (x < 0) {
		xorigin = min(-x, amdgpu_crtc->max_cursor_width - 1);
		x = 0;
	}
	if (y < 0) {
		yorigin = min(-y, amdgpu_crtc->max_cursor_height - 1);
		y = 0;
	}

	position.enable = true;
	position.x = x;
	position.y = y;

	position.x_hotspot = xorigin;
	position.y_hotspot = yorigin;

	if (!dc_stream_set_cursor_attributes(
				amdgpu_crtc->stream,
				&attributes)) {
		DRM_ERROR("DC failed to set cursor attributes\n");
	}

	if (!dc_stream_set_cursor_position(
				amdgpu_crtc->stream,
				&position)) {
		DRM_ERROR("DC failed to set cursor position\n");
	}
}

static int dm_crtc_unpin_cursor_bo_old(
	struct amdgpu_crtc *amdgpu_crtc)
{
	struct amdgpu_bo *robj;
	int ret = 0;

	if (NULL != amdgpu_crtc && NULL != amdgpu_crtc->cursor_bo) {
		robj = gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo);

		ret = amdgpu_bo_reserve(robj, false);

		if (likely(ret == 0)) {
			ret = amdgpu_bo_unpin(robj);

			if (unlikely(ret != 0)) {
				DRM_ERROR(
					"%s: unpin failed (ret=%d), bo %p\n",
					__func__,
					ret,
					amdgpu_crtc->cursor_bo);
			}

			amdgpu_bo_unreserve(robj);
		} else {
			DRM_ERROR(
				"%s: reserve failed (ret=%d), bo %p\n",
				__func__,
				ret,
				amdgpu_crtc->cursor_bo);
		}

		drm_gem_object_unreference_unlocked(amdgpu_crtc->cursor_bo);
		amdgpu_crtc->cursor_bo = NULL;
	}

	return ret;
}

static int dm_crtc_pin_cursor_bo_new(
	struct drm_crtc *crtc,
	struct drm_file *file_priv,
	uint32_t handle,
	struct drm_gem_object **ret_obj)
{
	struct amdgpu_crtc *amdgpu_crtc;
	struct amdgpu_bo *robj;
	struct drm_gem_object *obj;
	int ret = -EINVAL;

	if (NULL != crtc) {
		struct drm_device *dev = crtc->dev;
		struct amdgpu_device *adev = dev->dev_private;
		uint64_t gpu_addr;

		amdgpu_crtc = to_amdgpu_crtc(crtc);

		obj = kcl_drm_gem_object_lookup(crtc->dev, file_priv, handle);

		if (!obj) {
			DRM_ERROR(
				"Cannot find cursor object %x for crtc %d\n",
				handle,
				amdgpu_crtc->crtc_id);
			goto release;
		}
		robj = gem_to_amdgpu_bo(obj);

		ret  = amdgpu_bo_reserve(robj, false);

		if (unlikely(ret != 0)) {
			drm_gem_object_unreference_unlocked(obj);
		DRM_ERROR("dm_crtc_pin_cursor_bo_new ret %x, handle %x\n",
				 ret, handle);
			goto release;
		}

		ret = amdgpu_bo_pin_restricted(robj, AMDGPU_GEM_DOMAIN_VRAM, 0,
						adev->mc.visible_vram_size,
						&gpu_addr);

		if (ret == 0) {
			amdgpu_crtc->cursor_addr = gpu_addr;
			*ret_obj  = obj;
		}
		amdgpu_bo_unreserve(robj);
		if (ret)
			drm_gem_object_unreference_unlocked(obj);

	}
release:

	return ret;
}

static int dm_crtc_cursor_set(
	struct drm_crtc *crtc,
	struct drm_file *file_priv,
	uint32_t handle,
	uint32_t width,
	uint32_t height)
{
	struct drm_gem_object *new_cursor_gem;
	struct dc_cursor_position position;

	int ret;

	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	ret = EINVAL;
	new_cursor_gem	= NULL;

	DRM_DEBUG_KMS("%s: crtc_id=%d with handle %d and size %d to %d, bo_object %p\n",
		      __func__,
		      amdgpu_crtc->crtc_id,
		      handle,
		      width,
		      height,
		      amdgpu_crtc->cursor_bo);

	if (!handle) {
		/* turn off cursor */
		position.enable = false;
		position.x = 0;
		position.y = 0;

		if (amdgpu_crtc->stream) {
			/*set cursor visible false*/
			dc_stream_set_cursor_position(
				amdgpu_crtc->stream,
				&position);
		}
		/*unpin old cursor buffer and update cache*/
		ret = dm_crtc_unpin_cursor_bo_old(amdgpu_crtc);
		goto release;

	}

	if ((width > amdgpu_crtc->max_cursor_width) ||
		(height > amdgpu_crtc->max_cursor_height)) {
		DRM_ERROR(
			"%s: bad cursor width or height %d x %d\n",
			__func__,
			width,
			height);
		goto release;
	}
	/*try to pin new cursor bo*/
	ret = dm_crtc_pin_cursor_bo_new(crtc, file_priv, handle, &new_cursor_gem);
	/*if map not successful then return an error*/
	if (ret)
		goto release;

	/*program new cursor bo to hardware*/
	dm_set_cursor(amdgpu_crtc, amdgpu_crtc->cursor_addr, width, height);

	/*un map old, not used anymore cursor bo ,
	 * return memory and mapping back */
	dm_crtc_unpin_cursor_bo_old(amdgpu_crtc);

	/*assign new cursor bo to our internal cache*/
	amdgpu_crtc->cursor_bo = new_cursor_gem;

release:
	return ret;

}

static int dm_crtc_cursor_move(struct drm_crtc *crtc,
				     int x, int y)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	int xorigin = 0, yorigin = 0;
	struct dc_cursor_position position;

	amdgpu_crtc->cursor_x = x;
	amdgpu_crtc->cursor_y = y;

	/* avivo cursor are offset into the total surface */
	x += crtc->primary->state->src_x >> 16;
	y += crtc->primary->state->src_y >> 16;

	/*
	 * TODO: for cursor debugging unguard the following
	 */
#if 0
	DRM_DEBUG_KMS(
		"%s: x %d y %d c->x %d c->y %d\n",
		__func__,
		x,
		y,
		crtc->x,
		crtc->y);
#endif

	if (x < 0) {
		xorigin = min(-x, amdgpu_crtc->max_cursor_width - 1);
		x = 0;
	}
	if (y < 0) {
		yorigin = min(-y, amdgpu_crtc->max_cursor_height - 1);
		y = 0;
	}

	position.enable = true;
	position.x = x;
	position.y = y;

	position.x_hotspot = xorigin;
	position.y_hotspot = yorigin;

	if (amdgpu_crtc->stream) {
		if (!dc_stream_set_cursor_position(
					amdgpu_crtc->stream,
					&position)) {
			DRM_ERROR("DC failed to set cursor position\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void dm_crtc_cursor_reset(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	DRM_DEBUG_KMS(
		"%s: with cursor_bo %p\n",
		__func__,
		amdgpu_crtc->cursor_bo);

	if (amdgpu_crtc->cursor_bo && amdgpu_crtc->stream) {
		dm_set_cursor(
		amdgpu_crtc,
		amdgpu_crtc->cursor_addr,
		amdgpu_crtc->cursor_width,
		amdgpu_crtc->cursor_height);
	}
}
static bool fill_rects_from_plane_state(
	const struct drm_plane_state *state,
	struct dc_plane_state *plane_state)
{
	plane_state->src_rect.x = state->src_x >> 16;
	plane_state->src_rect.y = state->src_y >> 16;
	/*we ignore for now mantissa and do not to deal with floating pixels :(*/
	plane_state->src_rect.width = state->src_w >> 16;

	if (plane_state->src_rect.width == 0)
		return false;

	plane_state->src_rect.height = state->src_h >> 16;
	if (plane_state->src_rect.height == 0)
		return false;

	plane_state->dst_rect.x = state->crtc_x;
	plane_state->dst_rect.y = state->crtc_y;

	if (state->crtc_w == 0)
		return false;

	plane_state->dst_rect.width = state->crtc_w;

	if (state->crtc_h == 0)
		return false;

	plane_state->dst_rect.height = state->crtc_h;

	plane_state->clip_rect = plane_state->dst_rect;

	switch (state->rotation) {
	case BIT(DRM_ROTATE_0):
		plane_state->rotation = ROTATION_ANGLE_0;
		break;
	case BIT(DRM_ROTATE_90):
		plane_state->rotation = ROTATION_ANGLE_90;
		break;
	case BIT(DRM_ROTATE_180):
		plane_state->rotation = ROTATION_ANGLE_180;
		break;
	case BIT(DRM_ROTATE_270):
		plane_state->rotation = ROTATION_ANGLE_270;
		break;
	default:
		plane_state->rotation = ROTATION_ANGLE_0;
		break;
	}

	return true;
}
static bool get_fb_info(
	const struct amdgpu_framebuffer *amdgpu_fb,
	uint64_t *tiling_flags,
	uint64_t *fb_location)
{
	struct amdgpu_bo *rbo = gem_to_amdgpu_bo(amdgpu_fb->obj);
	int r = amdgpu_bo_reserve(rbo, false);

	if (unlikely(r != 0)){
		DRM_ERROR("Unable to reserve buffer\n");
		return false;
	}

	if (fb_location)
		*fb_location = amdgpu_bo_gpu_offset(rbo);

	if (tiling_flags)
		amdgpu_bo_get_tiling_flags(rbo, tiling_flags);

	amdgpu_bo_unreserve(rbo);

	return true;
}
static void fill_plane_attributes_from_fb(
	struct amdgpu_device *adev,
	struct dc_plane_state *plane_state,
	const struct amdgpu_framebuffer *amdgpu_fb, bool addReq)
{
	uint64_t tiling_flags;
	uint64_t fb_location = 0;
	unsigned int awidth;
	const struct drm_framebuffer *fb = &amdgpu_fb->base;

	get_fb_info(
		amdgpu_fb,
		&tiling_flags,
		addReq == true ? &fb_location:NULL);


	switch (fb->pixel_format) {
	case DRM_FORMAT_C8:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS;
		break;
	case DRM_FORMAT_RGB565:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010;
		break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		plane_state->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010;
		break;
	case DRM_FORMAT_NV21:
		plane_state->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr;
		break;
	case DRM_FORMAT_NV12:
		plane_state->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb;
		break;
	default:
		DRM_ERROR("Unsupported screen depth %d\n", fb->bits_per_pixel);
		return;
	}

	if (plane_state->format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		plane_state->address.type = PLN_ADDR_TYPE_GRAPHICS;
		plane_state->address.grph.addr.low_part = lower_32_bits(fb_location);
		plane_state->address.grph.addr.high_part = upper_32_bits(fb_location);
		plane_state->plane_size.grph.surface_size.x = 0;
		plane_state->plane_size.grph.surface_size.y = 0;
		plane_state->plane_size.grph.surface_size.width = fb->width;
		plane_state->plane_size.grph.surface_size.height = fb->height;
		plane_state->plane_size.grph.surface_pitch =
				fb->pitches[0] / (fb->bits_per_pixel / 8);
		/* TODO: unhardcode */
		plane_state->color_space = COLOR_SPACE_SRGB;

	} else {
		awidth = ALIGN(fb->width, 64);
		plane_state->address.type = PLN_ADDR_TYPE_VIDEO_PROGRESSIVE;
		plane_state->address.video_progressive.luma_addr.low_part
						= lower_32_bits(fb_location);
		plane_state->address.video_progressive.chroma_addr.low_part
						= lower_32_bits(fb_location) +
							(awidth * fb->height);
		plane_state->plane_size.video.luma_size.x = 0;
		plane_state->plane_size.video.luma_size.y = 0;
		plane_state->plane_size.video.luma_size.width = awidth;
		plane_state->plane_size.video.luma_size.height = fb->height;
		/* TODO: unhardcode */
		plane_state->plane_size.video.luma_pitch = awidth;

		plane_state->plane_size.video.chroma_size.x = 0;
		plane_state->plane_size.video.chroma_size.y = 0;
		plane_state->plane_size.video.chroma_size.width = awidth;
		plane_state->plane_size.video.chroma_size.height = fb->height;
		plane_state->plane_size.video.chroma_pitch = awidth / 2;

		/* TODO: unhardcode */
		plane_state->color_space = COLOR_SPACE_YCBCR709;
	}

	memset(&plane_state->tiling_info, 0, sizeof(plane_state->tiling_info));

	/* Fill GFX8 params */
	if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == DC_ARRAY_2D_TILED_THIN1) {
		unsigned int bankw, bankh, mtaspect, tile_split, num_banks;

		bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
		bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
		mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
		tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
		num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);

		/* XXX fix me for VI */
		plane_state->tiling_info.gfx8.num_banks = num_banks;
		plane_state->tiling_info.gfx8.array_mode =
				DC_ARRAY_2D_TILED_THIN1;
		plane_state->tiling_info.gfx8.tile_split = tile_split;
		plane_state->tiling_info.gfx8.bank_width = bankw;
		plane_state->tiling_info.gfx8.bank_height = bankh;
		plane_state->tiling_info.gfx8.tile_aspect = mtaspect;
		plane_state->tiling_info.gfx8.tile_mode =
				DC_ADDR_SURF_MICRO_TILING_DISPLAY;
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE)
			== DC_ARRAY_1D_TILED_THIN1) {
		plane_state->tiling_info.gfx8.array_mode = DC_ARRAY_1D_TILED_THIN1;
	}

	plane_state->tiling_info.gfx8.pipe_config =
			AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);

	if (adev->asic_type == CHIP_VEGA10 ||
	    adev->asic_type == CHIP_RAVEN) {
		/* Fill GFX9 params */
		plane_state->tiling_info.gfx9.num_pipes =
			adev->gfx.config.gb_addr_config_fields.num_pipes;
		plane_state->tiling_info.gfx9.num_banks =
			adev->gfx.config.gb_addr_config_fields.num_banks;
		plane_state->tiling_info.gfx9.pipe_interleave =
			adev->gfx.config.gb_addr_config_fields.pipe_interleave_size;
		plane_state->tiling_info.gfx9.num_shader_engines =
			adev->gfx.config.gb_addr_config_fields.num_se;
		plane_state->tiling_info.gfx9.max_compressed_frags =
			adev->gfx.config.gb_addr_config_fields.max_compress_frags;
		plane_state->tiling_info.gfx9.num_rb_per_se =
			adev->gfx.config.gb_addr_config_fields.num_rb_per_se;
		plane_state->tiling_info.gfx9.swizzle =
			AMDGPU_TILING_GET(tiling_flags, SWIZZLE_MODE);
		plane_state->tiling_info.gfx9.shaderEnable = 1;
	}

	plane_state->visible = true;
	plane_state->scaling_quality.h_taps_c = 0;
	plane_state->scaling_quality.v_taps_c = 0;

	/* is this needed? is plane_state zeroed at allocation? */
	plane_state->scaling_quality.h_taps = 0;
	plane_state->scaling_quality.v_taps = 0;
	plane_state->stereo_format = PLANE_STEREO_FORMAT_NONE;

}

static void fill_gamma_from_crtc(
	const struct drm_crtc *crtc,
	struct dc_plane_state *plane_state)
{
	int i;
	struct dc_gamma *gamma;
	uint16_t *red, *green, *blue;
	int end = (crtc->gamma_size > GAMMA_RGB_256_ENTRIES) ?
			GAMMA_RGB_256_ENTRIES : crtc->gamma_size;

	red = crtc->gamma_store;
	green = red + crtc->gamma_size;
	blue = green + crtc->gamma_size;

	gamma = dc_create_gamma();

	if (gamma == NULL)
		return;

	gamma->type = GAMMA_RGB_256;
	gamma->num_entries = GAMMA_RGB_256_ENTRIES;
	for (i = 0; i < end; i++) {
		gamma->entries.red[i] = dal_fixed31_32_from_int((unsigned short)red[i]);
		gamma->entries.green[i] = dal_fixed31_32_from_int((unsigned short)green[i]);
		gamma->entries.blue[i] = dal_fixed31_32_from_int((unsigned short)blue[i]);
	}

	plane_state->gamma_correction = gamma;
}

static void fill_plane_attributes(
			struct amdgpu_device *adev,
			struct dc_plane_state *dc_plane_state,
			struct drm_plane_state *state, bool addrReq)
{
	const struct amdgpu_framebuffer *amdgpu_fb =
		to_amdgpu_framebuffer(state->fb);
	const struct drm_crtc *crtc = state->crtc;
	struct dc_transfer_func *input_tf;

	fill_rects_from_plane_state(state, dc_plane_state);
	fill_plane_attributes_from_fb(
		crtc->dev->dev_private,
		dc_plane_state,
		amdgpu_fb,
		addrReq);

	input_tf = dc_create_transfer_func();

	if (input_tf == NULL)
		return;

	input_tf->type = TF_TYPE_PREDEFINED;
	input_tf->tf = TRANSFER_FUNCTION_SRGB;

	dc_plane_state->in_transfer_func = input_tf;

	/* In case of gamma set, update gamma value */
	if (crtc->mode.private_flags &
		AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET) {
		fill_gamma_from_crtc(crtc, dc_plane_state);
	}
}

/*****************************************************************************/

struct amdgpu_connector *aconnector_from_drm_crtc_id(
		const struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_connector *connector;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_connector *aconnector;

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head)	{

		aconnector = to_amdgpu_connector(connector);

		if (aconnector->base.state->crtc != &acrtc->base)
			continue;

		/* Found the connector */
		return aconnector;
	}

	/* If we get here, not found. */
	return NULL;
}

static void update_stream_scaling_settings(
		const struct drm_display_mode *mode,
		const struct dm_connector_state *dm_state,
		struct dc_stream_state *stream)
{
	enum amdgpu_rmx_type rmx_type;

	struct rect src = { 0 }; /* viewport in composition space*/
	struct rect dst = { 0 }; /* stream addressable area */

	/* no mode. nothing to be done */
	if (!mode)
		return;

	/* Full screen scaling by default */
	src.width = mode->hdisplay;
	src.height = mode->vdisplay;
	dst.width = stream->timing.h_addressable;
	dst.height = stream->timing.v_addressable;

	rmx_type = dm_state->scaling;
	if (rmx_type == RMX_ASPECT || rmx_type == RMX_OFF) {
		if (src.width * dst.height <
				src.height * dst.width) {
			/* height needs less upscaling/more downscaling */
			dst.width = src.width *
					dst.height / src.height;
		} else {
			/* width needs less upscaling/more downscaling */
			dst.height = src.height *
					dst.width / src.width;
		}
	} else if (rmx_type == RMX_CENTER) {
		dst = src;
	}

	dst.x = (stream->timing.h_addressable - dst.width) / 2;
	dst.y = (stream->timing.v_addressable - dst.height) / 2;

	if (dm_state->underscan_enable) {
		dst.x += dm_state->underscan_hborder / 2;
		dst.y += dm_state->underscan_vborder / 2;
		dst.width -= dm_state->underscan_hborder;
		dst.height -= dm_state->underscan_vborder;
	}

	stream->src = src;
	stream->dst = dst;

	DRM_DEBUG_KMS("Destination Rectangle x:%d  y:%d  width:%d  height:%d\n",
			dst.x, dst.y, dst.width, dst.height);

}

static void dm_dc_plane_state_commit(
		struct dc *dc,
		struct drm_crtc *crtc)
{
	struct dc_plane_state *dc_plane_state;
	struct dc_plane_state *dc_plane_states[1];
	const struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct dc_stream_state *dc_stream = acrtc->stream;

	if (!dc_stream) {
		dm_error(
			"%s: Failed to obtain stream on crtc (%d)!\n",
			__func__,
			acrtc->crtc_id);
		goto fail;
	}

	dc_plane_state = dc_create_plane_state(dc);

	if (!dc_plane_state) {
		dm_error(
			"%s: Failed to create a plane state!\n",
			__func__);
		goto fail;
	}

	/* Surface programming */
	fill_plane_attributes(
			crtc->dev->dev_private,
			dc_plane_state,
			crtc->primary->state,
			true);
	if (crtc->mode.private_flags &
		AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET) {
		/* reset trigger of gamma */
		crtc->mode.private_flags &=
			~AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET;
	}

	dc_plane_states[0] = dc_plane_state;

	if (false == dc_commit_planes_to_stream(
			dc,
			dc_plane_states,
			1,
			dc_stream)) {
		dm_error(
			"%s: Failed to attach plane state!\n",
			__func__);
	}

	dc_plane_state_release(dc_plane_state);
fail:
	return;
}

static enum dc_color_depth convert_color_depth_from_display_info(
		const struct drm_connector *connector)
{
	uint32_t bpc = connector->display_info.bpc;

	/* Limited color depth to 8bit
	 * TODO: Still need to handle deep color
	 */
	if (bpc > 8)
		bpc = 8;

	switch (bpc) {
	case 0:
		/* Temporary Work around, DRM don't parse color depth for
		 * EDID revision before 1.4
		 * TODO: Fix edid parsing
		 */
		return COLOR_DEPTH_888;
	case 6:
		return COLOR_DEPTH_666;
	case 8:
		return COLOR_DEPTH_888;
	case 10:
		return COLOR_DEPTH_101010;
	case 12:
		return COLOR_DEPTH_121212;
	case 14:
		return COLOR_DEPTH_141414;
	case 16:
		return COLOR_DEPTH_161616;
	default:
		return COLOR_DEPTH_UNDEFINED;
	}
}

static enum dc_aspect_ratio get_aspect_ratio(
		const struct drm_display_mode *mode_in)
{
	int32_t width = mode_in->crtc_hdisplay * 9;
	int32_t height = mode_in->crtc_vdisplay * 16;

	if ((width - height) < 10 && (width - height) > -10)
		return ASPECT_RATIO_16_9;
	else
		return ASPECT_RATIO_4_3;
}

static enum dc_color_space get_output_color_space(
				const struct dc_crtc_timing *dc_crtc_timing)
{
	enum dc_color_space color_space = COLOR_SPACE_SRGB;

	switch (dc_crtc_timing->pixel_encoding)	{
	case PIXEL_ENCODING_YCBCR422:
	case PIXEL_ENCODING_YCBCR444:
	case PIXEL_ENCODING_YCBCR420:
	{
		/*
		 * 27030khz is the separation point between HDTV and SDTV
		 * according to HDMI spec, we use YCbCr709 and YCbCr601
		 * respectively
		 */
		if (dc_crtc_timing->pix_clk_khz > 27030) {
			if (dc_crtc_timing->flags.Y_ONLY)
				color_space =
					COLOR_SPACE_YCBCR709_LIMITED;
			else
				color_space = COLOR_SPACE_YCBCR709;
		} else {
			if (dc_crtc_timing->flags.Y_ONLY)
				color_space =
					COLOR_SPACE_YCBCR601_LIMITED;
			else
				color_space = COLOR_SPACE_YCBCR601;
		}

	}
	break;
	case PIXEL_ENCODING_RGB:
		color_space = COLOR_SPACE_SRGB;
		break;

	default:
		WARN_ON(1);
		break;
	}

	return color_space;
}

/*****************************************************************************/

static void fill_stream_properties_from_drm_display_mode(
	struct dc_stream_state *stream,
	const struct drm_display_mode *mode_in,
	const struct drm_connector *connector)
{
	struct dc_crtc_timing *timing_out = &stream->timing;

	memset(timing_out, 0, sizeof(struct dc_crtc_timing));

	timing_out->h_border_left = 0;
	timing_out->h_border_right = 0;
	timing_out->v_border_top = 0;
	timing_out->v_border_bottom = 0;
	/* TODO: un-hardcode */

	if ((connector->display_info.color_formats & DRM_COLOR_FORMAT_YCRCB444)
			&& stream->sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR444;
	else
		timing_out->pixel_encoding = PIXEL_ENCODING_RGB;

	timing_out->timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing_out->display_color_depth = convert_color_depth_from_display_info(
			connector);
	timing_out->scan_type = SCANNING_TYPE_NODATA;
	timing_out->hdmi_vic = 0;
	timing_out->vic = drm_match_cea_mode(mode_in);

	timing_out->h_addressable = mode_in->crtc_hdisplay;
	timing_out->h_total = mode_in->crtc_htotal;
	timing_out->h_sync_width =
		mode_in->crtc_hsync_end - mode_in->crtc_hsync_start;
	timing_out->h_front_porch =
		mode_in->crtc_hsync_start - mode_in->crtc_hdisplay;
	timing_out->v_total = mode_in->crtc_vtotal;
	timing_out->v_addressable = mode_in->crtc_vdisplay;
	timing_out->v_front_porch =
		mode_in->crtc_vsync_start - mode_in->crtc_vdisplay;
	timing_out->v_sync_width =
		mode_in->crtc_vsync_end - mode_in->crtc_vsync_start;
	timing_out->pix_clk_khz = mode_in->crtc_clock;
	timing_out->aspect_ratio = get_aspect_ratio(mode_in);
	if (mode_in->flags & DRM_MODE_FLAG_PHSYNC)
		timing_out->flags.HSYNC_POSITIVE_POLARITY = 1;
	if (mode_in->flags & DRM_MODE_FLAG_PVSYNC)
		timing_out->flags.VSYNC_POSITIVE_POLARITY = 1;

	stream->output_color_space = get_output_color_space(timing_out);

	{
		struct dc_transfer_func *tf = dc_create_transfer_func();

		tf->type = TF_TYPE_PREDEFINED;
		tf->tf = TRANSFER_FUNCTION_SRGB;
		stream->out_transfer_func = tf;
	}
}

static void fill_audio_info(
	struct audio_info *audio_info,
	const struct drm_connector *drm_connector,
	const struct dc_sink *dc_sink)
{
	int i = 0;
	int cea_revision = 0;
	const struct dc_edid_caps *edid_caps = &dc_sink->edid_caps;

	audio_info->manufacture_id = edid_caps->manufacturer_id;
	audio_info->product_id = edid_caps->product_id;

	cea_revision = drm_connector->display_info.cea_rev;

	while (i < AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS &&
		edid_caps->display_name[i]) {
		audio_info->display_name[i] = edid_caps->display_name[i];
		i++;
	}

	if (cea_revision >= 3) {
		audio_info->mode_count = edid_caps->audio_mode_count;

		for (i = 0; i < audio_info->mode_count; ++i) {
			audio_info->modes[i].format_code =
					(enum audio_format_code)
					(edid_caps->audio_modes[i].format_code);
			audio_info->modes[i].channel_count =
					edid_caps->audio_modes[i].channel_count;
			audio_info->modes[i].sample_rates.all =
					edid_caps->audio_modes[i].sample_rate;
			audio_info->modes[i].sample_size =
					edid_caps->audio_modes[i].sample_size;
		}
	}

	audio_info->flags.all = edid_caps->speaker_flags;

	/* TODO: We only check for the progressive mode, check for interlace mode too */
	if (drm_connector->latency_present[0]) {
		audio_info->video_latency = drm_connector->video_latency[0];
		audio_info->audio_latency = drm_connector->audio_latency[0];
	}

	/* TODO: For DP, video and audio latency should be calculated from DPCD caps */

}

static void copy_crtc_timing_for_drm_display_mode(
		const struct drm_display_mode *src_mode,
		struct drm_display_mode *dst_mode)
{
	dst_mode->crtc_hdisplay = src_mode->crtc_hdisplay;
	dst_mode->crtc_vdisplay = src_mode->crtc_vdisplay;
	dst_mode->crtc_clock = src_mode->crtc_clock;
	dst_mode->crtc_hblank_start = src_mode->crtc_hblank_start;
	dst_mode->crtc_hblank_end = src_mode->crtc_hblank_end;
	dst_mode->crtc_hsync_start = src_mode->crtc_hsync_start;
	dst_mode->crtc_hsync_end = src_mode->crtc_hsync_end;
	dst_mode->crtc_htotal = src_mode->crtc_htotal;
	dst_mode->crtc_hskew = src_mode->crtc_hskew;
	dst_mode->crtc_vblank_start = src_mode->crtc_vblank_start;
	dst_mode->crtc_vblank_end = src_mode->crtc_vblank_end;
	dst_mode->crtc_vsync_start = src_mode->crtc_vsync_start;
	dst_mode->crtc_vsync_end = src_mode->crtc_vsync_end;
	dst_mode->crtc_vtotal = src_mode->crtc_vtotal;
}

static void decide_crtc_timing_for_drm_display_mode(
		struct drm_display_mode *drm_mode,
		const struct drm_display_mode *native_mode,
		bool scale_enabled)
{
	if (scale_enabled) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else if (native_mode->clock == drm_mode->clock &&
			native_mode->htotal == drm_mode->htotal &&
			native_mode->vtotal == drm_mode->vtotal) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else {
		/* no scaling nor amdgpu inserted, no need to patch */
	}
}

static struct dc_stream_state *create_stream_for_sink(
		struct amdgpu_connector *aconnector,
		const struct drm_display_mode *drm_mode,
		const struct dm_connector_state *dm_state)
{
	struct drm_display_mode *preferred_mode = NULL;
	const struct drm_connector *drm_connector;
	struct dc_stream_state *stream = NULL;
	struct drm_display_mode mode = *drm_mode;
	bool native_mode_found = false;

	if (aconnector == NULL) {
		DRM_ERROR("aconnector is NULL!\n");
		goto drm_connector_null;
	}

	if (dm_state == NULL) {
		DRM_ERROR("dm_state is NULL!\n");
		goto dm_state_null;
	}

	drm_connector = &aconnector->base;
	stream = dc_create_stream_for_sink(aconnector->dc_sink);

	if (stream == NULL) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto stream_create_fail;
	}

	list_for_each_entry(preferred_mode, &aconnector->base.modes, head) {
		/* Search for preferred mode */
		if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED) {
			native_mode_found = true;
			break;
		}
	}
	if (!native_mode_found)
		preferred_mode = list_first_entry_or_null(
				&aconnector->base.modes,
				struct drm_display_mode,
				head);

	if (preferred_mode == NULL) {
		/* This may not be an error, the use case is when we we have no
		 * usermode calls to reset and set mode upon hotplug. In this
		 * case, we call set mode ourselves to restore the previous mode
		 * and the modelist may not be filled in in time.
		 */
		DRM_INFO("No preferred mode found\n");
	} else {
		decide_crtc_timing_for_drm_display_mode(
				&mode, preferred_mode,
				dm_state->scaling != RMX_OFF);
	}

	fill_stream_properties_from_drm_display_mode(stream,
			&mode, &aconnector->base);
	update_stream_scaling_settings(&mode, dm_state, stream);

	fill_audio_info(
		&stream->audio_info,
		drm_connector,
		aconnector->dc_sink);

stream_create_fail:
dm_state_null:
drm_connector_null:
	return stream;
}

void amdgpu_dm_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static void amdgpu_dm_atomic_crtc_gamma_set(
		struct drm_crtc *crtc,
		u16 *red,
		u16 *green,
		u16 *blue,
		uint32_t start,
		uint32_t size)
{
	struct drm_device *dev = crtc->dev;
	struct drm_property *prop = dev->mode_config.prop_crtc_id;

	crtc->state->mode.private_flags |= AMDGPU_CRTC_MODE_PRIVATE_FLAGS_GAMMASET;

	drm_atomic_helper_crtc_set_property(crtc, prop, 0);
}

static int dm_crtc_funcs_atomic_set_property(
	struct drm_crtc *crtc,
	struct drm_crtc_state *crtc_state,
	struct drm_property *property,
	uint64_t val)
{
	struct drm_plane_state *plane_state;

	crtc_state->planes_changed = true;

	/*
	 * Bit of magic done here. We need to ensure
	 * that planes get update after mode is set.
	 * So, we need to add primary plane to state,
	 * and this way atomic_update would be called
	 * for it
	 */
	plane_state =
		drm_atomic_get_plane_state(
			crtc_state->state,
			crtc->primary);

	if (!plane_state)
		return -EINVAL;

	return 0;
}


static int amdgpu_atomic_helper_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t flags)
{
	struct drm_plane *plane = crtc->primary;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	struct drm_crtc_state *crtc_state;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	ret = drm_crtc_vblank_get(crtc);
	if (ret)
		return ret;

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(crtc);
retry:
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}
	crtc_state->event = event;

	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state)) {
		ret = PTR_ERR(plane_state);
		goto fail;
	}

	ret = drm_atomic_set_crtc_for_plane(plane_state, crtc);
	if (ret != 0)
		goto fail;
	drm_atomic_set_fb_for_plane(plane_state, fb);

	/* Make sure we don't accidentally do a full modeset. */
	state->allow_modeset = false;
	if (!crtc_state->active) {
		DRM_DEBUG_ATOMIC("[CRTC:%d] disabled, rejecting legacy flip\n",
				 crtc->base.id);
		ret = -EINVAL;
		goto fail;
	}
	acrtc->flip_flags = flags;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0) && !defined(OS_NAME_RHEL_7_4)
	ret = drm_atomic_async_commit(state);
#else
	ret = drm_atomic_nonblocking_commit(state);
#endif
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful async commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	if (ret)
		drm_crtc_vblank_put(crtc);

	drm_atomic_state_free(state);

	return ret;
backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	/*
	 * Someone might have exchanged the framebuffer while we dropped locks
	 * in the backoff code. We need to fix up the fb refcount tracking the
	 * core does for us.
	 */
	plane->old_fb = plane->fb;

	goto retry;
}

/* Implemented only the options currently availible for the driver */
static const struct drm_crtc_funcs amdgpu_dm_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.cursor_set = dm_crtc_cursor_set,
	.cursor_move = dm_crtc_cursor_move,
	.destroy = amdgpu_dm_crtc_destroy,
	.gamma_set = amdgpu_dm_atomic_crtc_gamma_set,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = amdgpu_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.atomic_set_property = dm_crtc_funcs_atomic_set_property
};

static enum drm_connector_status
amdgpu_dm_connector_detect(struct drm_connector *connector, bool force)
{
	bool connected;
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);

	/* Notes:
	 * 1. This interface is NOT called in context of HPD irq.
	 * 2. This interface *is called* in context of user-mode ioctl. Which
	 * makes it a bad place for *any* MST-related activit. */

	if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
		connected = (aconnector->dc_sink != NULL);
	else
		connected = (aconnector->base.force == DRM_FORCE_ON);

	return (connected ? connector_status_connected :
			connector_status_disconnected);
}

/* Compare user free sync property with immunable property free sync capable
 * and if display is not free sync capable sets free sync property to 0
 */
static int amdgpu_freesync_update_property_atomic(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;

	return drm_object_property_set_value(&connector->base,
					     adev->mode_info.freesync_property,
					     0);


}

static int amdgpu_freesync_set_property_atomic(
				struct drm_connector *connector,
				struct drm_connector_state *connector_state,
				struct drm_property *property, uint64_t val)
{
	struct mod_freesync_user_enable user_enable;
	struct drm_device *dev;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	int ret;
	uint64_t val_capable;

	dev  = connector->dev;
	adev = dev->dev_private;
	ret  = -EINVAL;

	if (adev->dm.freesync_module && connector_state->crtc) {
		ret = drm_object_property_get_value(
				&connector->base,
				adev->mode_info.freesync_capable_property,
				&val_capable);
		/* if user free sync val property is enable, but the capable
		 * prop is not, then fail the call
		 */
		if (ret != 0 || (val_capable == 0 && val != 0)) {
			ret  = -EINVAL;
			goto release;
		}

		user_enable.enable_for_gaming = val ? true : false;
		user_enable.enable_for_static = user_enable.enable_for_gaming;
		user_enable.enable_for_video  = user_enable.enable_for_gaming;
		ret  = -EINVAL;
		acrtc = to_amdgpu_crtc(connector_state->crtc);
		if (connector_state->connector == connector && acrtc->stream) {
			mod_freesync_set_user_enable(adev->dm.freesync_module,
						     &acrtc->stream, 1,
						     &user_enable);
			ret = 0;
		}
	}
release:
	return ret;
}

int amdgpu_dm_connector_atomic_set_property(
	struct drm_connector *connector,
	struct drm_connector_state *connector_state,
	struct drm_property *property,
	uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct dm_connector_state *dm_old_state =
		to_dm_connector_state(connector->state);
	struct dm_connector_state *dm_new_state =
		to_dm_connector_state(connector_state);

	int ret = -EINVAL;

	if (property == dev->mode_config.scaling_mode_property) {
		enum amdgpu_rmx_type rmx_type;

		switch (val) {
		case DRM_MODE_SCALE_CENTER:
			rmx_type = RMX_CENTER;
			break;
		case DRM_MODE_SCALE_ASPECT:
			rmx_type = RMX_ASPECT;
			break;
		case DRM_MODE_SCALE_FULLSCREEN:
			rmx_type = RMX_FULL;
			break;
		case DRM_MODE_SCALE_NONE:
		default:
			rmx_type = RMX_OFF;
			break;
		}

		if (dm_old_state->scaling == rmx_type)
			return 0;

		dm_new_state->scaling = rmx_type;
		ret = 0;
	} else if (property == adev->mode_info.underscan_hborder_property) {
		dm_new_state->underscan_hborder = val;
		ret = 0;
	} else if (property == adev->mode_info.underscan_vborder_property) {
		dm_new_state->underscan_vborder = val;
		ret = 0;
	} else if (property == adev->mode_info.underscan_property) {
		dm_new_state->underscan_enable = val;
		ret = 0;
	} else if (property == adev->mode_info.freesync_property) {
		ret = amdgpu_freesync_set_property_atomic(connector,
							  connector_state,
							  property, val);
		return ret;
	} else if (property == adev->mode_info.freesync_capable_property) {
		ret = -EINVAL;
		return ret;
	}

	return ret;
}

int amdgpu_dm_connector_atomic_get_property(
	struct drm_connector *connector,
	const struct drm_connector_state *state,
	struct drm_property *property,
	uint64_t *val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct dm_connector_state *dm_state =
		to_dm_connector_state(state);
	int ret = -EINVAL;
	int i;

	if (property == dev->mode_config.scaling_mode_property) {
		switch (dm_state->scaling) {
		case RMX_CENTER:
			*val = DRM_MODE_SCALE_CENTER;
			break;
		case RMX_ASPECT:
			*val = DRM_MODE_SCALE_ASPECT;
			break;
		case RMX_FULL:
			*val = DRM_MODE_SCALE_FULLSCREEN;
			break;
		case RMX_OFF:
		default:
			*val = DRM_MODE_SCALE_NONE;
			break;
		}
		ret = 0;
	} else if (property == adev->mode_info.underscan_hborder_property) {
		*val = dm_state->underscan_hborder;
		ret = 0;
	} else if (property == adev->mode_info.underscan_vborder_property) {
		*val = dm_state->underscan_vborder;
		ret = 0;
	} else if (property == adev->mode_info.underscan_property) {
		*val = dm_state->underscan_enable;
		ret = 0;
	} else if ((property == adev->mode_info.freesync_property) ||
		   (property == adev->mode_info.freesync_capable_property)) {
		for (i = 0; i < connector->base.properties->count; i++) {
			if (connector->base.properties->properties[i] == property) {
				*val = connector->base.properties->values[i];
				ret = 0;
			}
		}
	}
	return ret;
}

void amdgpu_dm_connector_destroy(struct drm_connector *connector)
{
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);
	const struct dc_link *link = aconnector->dc_link;
	struct amdgpu_device *adev = connector->dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

	if (link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) {
		amdgpu_dm_register_backlight_device(dm);

		if (dm->backlight_dev) {
			backlight_device_unregister(dm->backlight_dev);
			dm->backlight_dev = NULL;
		}

	}
#endif
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	kfree(state);

	state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (state) {
		state->scaling = RMX_OFF;
		state->underscan_enable = false;
		state->underscan_hborder = 0;
		state->underscan_vborder = 0;

		connector->state = &state->base;
		connector->state->connector = connector;
	}
}

struct drm_connector_state *amdgpu_dm_connector_atomic_duplicate_state(
	struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	struct dm_connector_state *new_state =
			kmemdup(state, sizeof(*state), GFP_KERNEL);

	if (new_state) {
		__drm_atomic_helper_connector_duplicate_state(connector,
								      &new_state->base);
		return &new_state->base;
	}

	return NULL;
}

static const struct drm_connector_funcs amdgpu_dm_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = amdgpu_dm_connector_funcs_reset,
	.detect = amdgpu_dm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = drm_atomic_helper_connector_set_property,
	.destroy = amdgpu_dm_connector_destroy,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property,
	.atomic_get_property = amdgpu_dm_connector_atomic_get_property
};

static struct drm_encoder *best_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	DRM_DEBUG_KMS("Finding the best encoder\n");

	/* pick the encoder ids */
	if (enc_id) {
		obj = drm_mode_object_find(connector->dev, enc_id, DRM_MODE_OBJECT_ENCODER);
		if (!obj) {
			DRM_ERROR("Couldn't find a matching encoder for our connector\n");
			return NULL;
		}
		encoder = obj_to_encoder(obj);
		return encoder;
	}
	DRM_ERROR("No encoder id\n");
	return NULL;
}

static int get_modes(struct drm_connector *connector)
{
	return amdgpu_dm_connector_get_modes(connector);
}

static void create_eml_sink(struct amdgpu_connector *aconnector)
{
	struct dc_sink_init_data init_params = {
			.link = aconnector->dc_link,
			.sink_signal = SIGNAL_TYPE_VIRTUAL
	};
	struct edid *edid = (struct edid *) aconnector->base.edid_blob_ptr->data;

	if (!aconnector->base.edid_blob_ptr ||
		!aconnector->base.edid_blob_ptr->data) {
		DRM_ERROR("No EDID firmware found on connector: %s ,forcing to OFF!\n",
				aconnector->base.name);

		aconnector->base.force = DRM_FORCE_OFF;
		aconnector->base.override_edid = false;
		return;
	}

	aconnector->edid = edid;

	aconnector->dc_em_sink = dc_link_add_remote_sink(
		aconnector->dc_link,
		(uint8_t *)edid,
		(edid->extensions + 1) * EDID_LENGTH,
		&init_params);

	if (aconnector->base.force
					== DRM_FORCE_ON)
		aconnector->dc_sink = aconnector->dc_link->local_sink ?
		aconnector->dc_link->local_sink :
		aconnector->dc_em_sink;
}

static void handle_edid_mgmt(struct amdgpu_connector *aconnector)
{
	struct dc_link *link = (struct dc_link *)aconnector->dc_link;

	/* In case of headless boot with force on for DP managed connector
	 * Those settings have to be != 0 to get initial modeset
	 */
	if (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		link->verified_link_cap.lane_count = LANE_COUNT_FOUR;
		link->verified_link_cap.link_rate = LINK_RATE_HIGH2;
	}


	aconnector->base.override_edid = true;
	create_eml_sink(aconnector);
}

int amdgpu_dm_connector_mode_valid(
		struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	int result = MODE_ERROR;
	struct dc_sink *dc_sink;
	struct amdgpu_device *adev = connector->dev->dev_private;
	struct dc_validation_set val_set = { 0 };
	/* TODO: Unhardcode stream count */
	struct dc_stream_state *stream;
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);
	struct validate_context *context;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	/* Only run this the first time mode_valid is called to initilialize
	 * EDID mgmt
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED &&
		!aconnector->dc_em_sink)
		handle_edid_mgmt(aconnector);

	dc_sink = to_amdgpu_connector(connector)->dc_sink;

	if (dc_sink == NULL) {
		DRM_ERROR("dc_sink is NULL!\n");
		goto null_sink;
	}

	stream = dc_create_stream_for_sink(dc_sink);
	if (stream == NULL) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto stream_create_fail;
	}

	drm_mode_set_crtcinfo(mode, 0);
	fill_stream_properties_from_drm_display_mode(stream, mode, connector);

	val_set.stream = stream;
	val_set.plane_count = 0;
	stream->src.width = mode->hdisplay;
	stream->src.height = mode->vdisplay;
	stream->dst = stream->src;

	context = dc_get_validate_context(adev->dm.dc, &val_set, 1);

	if (context) {
		result = MODE_OK;
		dc_resource_validate_ctx_destruct(context);
		dm_free(context);
	}

	dc_stream_release(stream);

stream_create_fail:
null_sink:
	/* TODO: error handling*/
	return result;
}

static const struct drm_connector_helper_funcs
amdgpu_dm_connector_helper_funcs = {
	/*
	 * If hotplug a second bigger display in FB Con mode, bigger resolution
	 * modes will be filtered by drm_mode_validate_size(), and those modes
	 * is missing after user start lightdm. So we need to renew modes list.
	 * in get_modes call back, not just return the modes count
	 */
	.get_modes = get_modes,
	.mode_valid = amdgpu_dm_connector_mode_valid,
	.best_encoder = best_encoder
};

static void dm_crtc_helper_disable(struct drm_crtc *crtc)
{
}

static int dm_crtc_helper_atomic_check(
	struct drm_crtc *crtc,
	struct drm_crtc_state *state)
{
	return 0;
}

static bool dm_crtc_helper_mode_fixup(
	struct drm_crtc *crtc,
	const struct drm_display_mode *mode,
	struct drm_display_mode *adjusted_mode)
{
	return true;
}

static const struct drm_crtc_helper_funcs amdgpu_dm_crtc_helper_funcs = {
	.disable = dm_crtc_helper_disable,
	.atomic_check = dm_crtc_helper_atomic_check,
	.mode_fixup = dm_crtc_helper_mode_fixup
};

static void dm_encoder_helper_disable(struct drm_encoder *encoder)
{

}

static int dm_encoder_helper_atomic_check(
	struct drm_encoder *encoder,
	struct drm_crtc_state *crtc_state,
	struct drm_connector_state *conn_state)
{
	return 0;
}

const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs = {
	.disable = dm_encoder_helper_disable,
	.atomic_check = dm_encoder_helper_atomic_check
};

static void dm_drm_plane_reset(struct drm_plane *plane)
{
	struct dm_plane_state *amdgpu_state = NULL;
	struct amdgpu_device *adev = plane->dev->dev_private;

	if (plane->state)
		plane->funcs->atomic_destroy_state(plane, plane->state);

	amdgpu_state = kzalloc(sizeof(*amdgpu_state), GFP_KERNEL);

	if (amdgpu_state) {
		plane->state = &amdgpu_state->base;
		plane->state->plane = plane;
		plane->state->rotation = DRM_ROTATE_0;

		amdgpu_state->dc_state = dc_create_plane_state(adev->dm.dc);
		WARN_ON(!amdgpu_state->dc_state);
	} else {
		WARN_ON(1);
	}
}

static struct drm_plane_state *
dm_drm_plane_duplicate_state(struct drm_plane *plane)
{
	struct dm_plane_state *dm_plane_state, *old_dm_plane_state;
	struct amdgpu_device *adev = plane->dev->dev_private;

	old_dm_plane_state = to_dm_plane_state(plane->state);
	dm_plane_state = kzalloc(sizeof(*dm_plane_state), GFP_KERNEL);
	if (!dm_plane_state)
		return NULL;

	if (old_dm_plane_state->dc_state) {
		struct dc_plane_state *dc_plane_state = dc_create_plane_state(adev->dm.dc);
		if (WARN_ON(!dc_plane_state))
			return NULL;

		__drm_atomic_helper_plane_duplicate_state(plane, &dm_plane_state->base);

		memcpy(dc_plane_state, old_dm_plane_state->dc_state, sizeof(*dc_plane_state));

		if (old_dm_plane_state->dc_state->gamma_correction)
			dc_gamma_retain(dc_plane_state->gamma_correction);

		if (old_dm_plane_state->dc_state->in_transfer_func)
			dc_transfer_func_retain(dc_plane_state->in_transfer_func);

		dm_plane_state->dc_state = dc_plane_state;

		/* TODO Check for inferred values to be reset */
	} else {
		WARN_ON(1);
		return NULL;
	}

	return &dm_plane_state->base;
}

void dm_drm_plane_destroy_state(struct drm_plane *plane,
					   struct drm_plane_state *state)
{
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(state);

	if (dm_plane_state->dc_state) {
		struct dc_plane_state *dc_plane_state = dm_plane_state->dc_state;

		if (dc_plane_state->gamma_correction)
			dc_gamma_release(&dc_plane_state->gamma_correction);

		if (dc_plane_state->in_transfer_func)
			dc_transfer_func_release(dc_plane_state->in_transfer_func);

		dc_plane_state_release(dc_plane_state);
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 7, 0)
	__drm_atomic_helper_plane_destroy_state(state);
#else
	__drm_atomic_helper_plane_destroy_state(plane, state);
#endif
	kfree(dm_plane_state);
}

static const struct drm_plane_funcs dm_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_cleanup,
	.set_property	= drm_atomic_helper_plane_set_property,
	.reset = dm_drm_plane_reset,
	.atomic_duplicate_state = dm_drm_plane_duplicate_state,
	.atomic_destroy_state = dm_drm_plane_destroy_state,
};

static void clear_unrelated_fields(struct drm_plane_state *state)
{
	state->crtc = NULL;
	state->fb = NULL;
	state->state = NULL;
	state->fence = NULL;
}

static bool page_flip_needed(
	const struct drm_plane_state *new_state,
	const struct drm_plane_state *old_state,
	struct drm_pending_vblank_event *event,
	bool commit_plane_state_required)
{
	struct drm_plane_state old_state_tmp;
	struct drm_plane_state new_state_tmp;

	struct amdgpu_framebuffer *amdgpu_fb_old;
	struct amdgpu_framebuffer *amdgpu_fb_new;
	struct amdgpu_crtc *acrtc_new;

	uint64_t old_tiling_flags;
	uint64_t new_tiling_flags;

	bool page_flip_required;

	if (!old_state)
		return false;

	if (!old_state->fb)
		return false;

	if (!new_state)
		return false;

	if (!new_state->fb)
		return false;

	old_state_tmp = *old_state;
	new_state_tmp = *new_state;

	if (!event)
		return false;

	amdgpu_fb_old = to_amdgpu_framebuffer(old_state->fb);
	amdgpu_fb_new = to_amdgpu_framebuffer(new_state->fb);

	if (!get_fb_info(amdgpu_fb_old, &old_tiling_flags, NULL))
		return false;

	if (!get_fb_info(amdgpu_fb_new, &new_tiling_flags, NULL))
		return false;

	if (commit_plane_state_required == true &&
	    old_tiling_flags != new_tiling_flags)
		return false;

	clear_unrelated_fields(&old_state_tmp);
	clear_unrelated_fields(&new_state_tmp);

	page_flip_required = memcmp(&old_state_tmp,
				    &new_state_tmp,
				    sizeof(old_state_tmp)) == 0 ? true:false;
	if (new_state->crtc && page_flip_required == false) {
		acrtc_new = to_amdgpu_crtc(new_state->crtc);
		if (acrtc_new->flip_flags & DRM_MODE_PAGE_FLIP_ASYNC)
			page_flip_required = true;
	}
	return page_flip_required;
}

#if defined(OS_NAME_RHEL_6) || defined(OS_NAME_RHEL_7_3)
static int dm_plane_helper_prepare_fb(
	struct drm_plane *plane,
	struct drm_plane_state *new_state)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
static int dm_plane_helper_prepare_fb(
	struct drm_plane *plane,
	const struct drm_plane_state *new_state)
#else
static int dm_plane_helper_prepare_fb(
	struct drm_plane *plane,
	struct drm_framebuffer *fb,
	const struct drm_plane_state *new_state)
#endif
{
	struct amdgpu_framebuffer *afb;
	struct drm_gem_object *obj;
	struct amdgpu_bo *rbo;
	int r;

	if (!new_state->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	afb = to_amdgpu_framebuffer(new_state->fb);

	obj = afb->obj;
	rbo = gem_to_amdgpu_bo(obj);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_pin(rbo, AMDGPU_GEM_DOMAIN_VRAM, NULL);

	amdgpu_bo_unreserve(rbo);

	if (unlikely(r != 0)) {
		DRM_ERROR("Failed to pin framebuffer\n");
		return r;
	}

	return 0;
}
#if defined(OS_NAME_RHEL_6) || defined(OS_NAME_RHEL_7_3)
static void dm_plane_helper_cleanup_fb(
	struct drm_plane *plane,
	struct drm_plane_state *old_state)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
static void dm_plane_helper_cleanup_fb(
	struct drm_plane *plane,
	const struct drm_plane_state *old_state)
#else
static void dm_plane_helper_cleanup_fb(
	struct drm_plane *plane,
	struct drm_framebuffer *fb,
	const struct drm_plane_state *old_state)
#endif
{
	struct amdgpu_bo *rbo;
	struct amdgpu_framebuffer *afb;
	int r;

	if (!old_state->fb)
		return;

	afb = to_amdgpu_framebuffer(old_state->fb);
	rbo = gem_to_amdgpu_bo(afb->obj);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r)) {
		DRM_ERROR("failed to reserve rbo before unpin\n");
		return;
	}

	amdgpu_bo_unpin(rbo);
	amdgpu_bo_unreserve(rbo);
}

int dm_create_validation_set_for_connector(struct drm_connector *connector,
		struct drm_display_mode *mode, struct dc_validation_set *val_set)
{
	int result = MODE_ERROR;
	struct dc_sink *dc_sink =
			to_amdgpu_connector(connector)->dc_sink;
	/* TODO: Unhardcode stream count */
	struct dc_stream_state *stream;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	if (dc_sink == NULL) {
		DRM_ERROR("dc_sink is NULL!\n");
		return result;
	}

	stream = dc_create_stream_for_sink(dc_sink);

	if (stream == NULL) {
		DRM_ERROR("Failed to create stream for sink!\n");
		return result;
	}

	drm_mode_set_crtcinfo(mode, 0);

	fill_stream_properties_from_drm_display_mode(stream, mode, connector);

	val_set->stream = stream;

	stream->src.width = mode->hdisplay;
	stream->src.height = mode->vdisplay;
	stream->dst = stream->src;

	return MODE_OK;
}

int dm_plane_atomic_check(struct drm_plane *plane,
			    struct drm_plane_state *state)
{
	struct amdgpu_device *adev = plane->dev->dev_private;
	struct dc *dc = adev->dm.dc;
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(state);

	if (!dm_plane_state->dc_state)
		return 0;

	if (dc_validate_plane(dc, dm_plane_state->dc_state))
		return 0;

	return -EINVAL;
}

static const struct drm_plane_helper_funcs dm_plane_helper_funcs = {
	.prepare_fb = dm_plane_helper_prepare_fb,
	.cleanup_fb = dm_plane_helper_cleanup_fb,
	.atomic_check = dm_plane_atomic_check,
};

/*
 * TODO: these are currently initialized to rgb formats only.
 * For future use cases we should either initialize them dynamically based on
 * plane capabilities, or initialize this array to all formats, so internal drm
 * check will succeed, and let DC to implement proper check
 */
static uint32_t rgb_formats[] = {
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
};

static uint32_t yuv_formats[] = {
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
};

int amdgpu_dm_plane_init(struct amdgpu_display_manager *dm,
			struct amdgpu_plane *aplane,
			unsigned long possible_crtcs)
{
	int res = -EPERM;

	switch (aplane->base.type) {
	case DRM_PLANE_TYPE_PRIMARY:
		aplane->base.format_default = true;

		res = kcl_drm_universal_plane_init(
				dm->adev->ddev,
				&aplane->base,
				possible_crtcs,
				&dm_plane_funcs,
				rgb_formats,
				ARRAY_SIZE(rgb_formats),
				aplane->base.type, NULL);
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		res = kcl_drm_universal_plane_init(
				dm->adev->ddev,
				&aplane->base,
				possible_crtcs,
				&dm_plane_funcs,
				yuv_formats,
				ARRAY_SIZE(yuv_formats),
				aplane->base.type, NULL);
		break;
	case DRM_PLANE_TYPE_CURSOR:
		DRM_ERROR("KMS: Cursor plane not implemented.");
		break;
	}

	drm_plane_helper_add(&aplane->base, &dm_plane_helper_funcs);

	return res;
}

int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			struct drm_plane *plane,
			uint32_t crtc_index)
{
	struct amdgpu_crtc *acrtc;
	int res = -ENOMEM;

	acrtc = kzalloc(sizeof(struct amdgpu_crtc), GFP_KERNEL);
	if (!acrtc)
		goto fail;

	res = kcl_drm_crtc_init_with_planes(
			dm->ddev,
			&acrtc->base,
			plane,
			NULL,
			&amdgpu_dm_crtc_funcs, NULL);

	if (res)
		goto fail;

	drm_crtc_helper_add(&acrtc->base, &amdgpu_dm_crtc_helper_funcs);

	acrtc->max_cursor_width = dm->adev->dm.dc->caps.max_cursor_size;
	acrtc->max_cursor_height = dm->adev->dm.dc->caps.max_cursor_size;

	acrtc->crtc_id = crtc_index;
	acrtc->base.enabled = false;

	dm->adev->mode_info.crtcs[crtc_index] = acrtc;
	drm_mode_crtc_set_gamma_size(&acrtc->base, 256);

	return 0;
fail:
	kfree(acrtc);
	acrtc->crtc_id = -1;
	return res;
}

static int to_drm_connector_type(enum signal_type st)
{
	switch (st) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return DRM_MODE_CONNECTOR_HDMIA;
	case SIGNAL_TYPE_EDP:
		return DRM_MODE_CONNECTOR_eDP;
	case SIGNAL_TYPE_RGB:
		return DRM_MODE_CONNECTOR_VGA;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		return DRM_MODE_CONNECTOR_DisplayPort;
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
		return DRM_MODE_CONNECTOR_DVID;
	case SIGNAL_TYPE_VIRTUAL:
		return DRM_MODE_CONNECTOR_VIRTUAL;

	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static void amdgpu_dm_get_native_mode(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper =
		connector->helper_private;
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	encoder = helper->best_encoder(connector);

	if (encoder == NULL)
		return;

	amdgpu_encoder = to_amdgpu_encoder(encoder);

	amdgpu_encoder->native_mode.clock = 0;

	if (!list_empty(&connector->probed_modes)) {
		struct drm_display_mode *preferred_mode = NULL;
		list_for_each_entry(preferred_mode,
				    &connector->probed_modes,
				    head) {
			if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED)
				amdgpu_encoder->native_mode = *preferred_mode;

			break;
		}

	}
}

static struct drm_display_mode *amdgpu_dm_create_common_mode(
		struct drm_encoder *encoder, char *name,
		int hdisplay, int vdisplay)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;

	mode = drm_mode_duplicate(dev, native_mode);

	if (mode == NULL)
		return NULL;

	mode->hdisplay = hdisplay;
	mode->vdisplay = vdisplay;
	mode->type &= ~DRM_MODE_TYPE_PREFERRED;
	strncpy(mode->name, name, DRM_DISPLAY_MODE_LEN);

	return mode;

}

static void amdgpu_dm_connector_add_common_modes(struct drm_encoder *encoder,
					struct drm_connector *connector)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;
	struct amdgpu_connector *amdgpu_connector =
				to_amdgpu_connector(connector);
	int i;
	int n;
	struct mode_size {
		char name[DRM_DISPLAY_MODE_LEN];
		int w;
		int h;
	} common_modes[] = {
		{  "640x480",  640,  480},
		{  "800x600",  800,  600},
		{ "1024x768", 1024,  768},
		{ "1280x720", 1280,  720},
		{ "1280x800", 1280,  800},
		{"1280x1024", 1280, 1024},
		{ "1440x900", 1440,  900},
		{"1680x1050", 1680, 1050},
		{"1600x1200", 1600, 1200},
		{"1920x1080", 1920, 1080},
		{"1920x1200", 1920, 1200}
	};

	n = ARRAY_SIZE(common_modes);

	for (i = 0; i < n; i++) {
		struct drm_display_mode *curmode = NULL;
		bool mode_existed = false;

		if (common_modes[i].w > native_mode->hdisplay ||
		    common_modes[i].h > native_mode->vdisplay ||
		   (common_modes[i].w == native_mode->hdisplay &&
		    common_modes[i].h == native_mode->vdisplay))
			continue;

		list_for_each_entry(curmode, &connector->probed_modes, head) {
			if (common_modes[i].w == curmode->hdisplay &&
			    common_modes[i].h == curmode->vdisplay) {
				mode_existed = true;
				break;
			}
		}

		if (mode_existed)
			continue;

		mode = amdgpu_dm_create_common_mode(encoder,
				common_modes[i].name, common_modes[i].w,
				common_modes[i].h);
		drm_mode_probed_add(connector, mode);
		amdgpu_connector->num_modes++;
	}
}

static void amdgpu_dm_connector_ddc_get_modes(
	struct drm_connector *connector,
	struct edid *edid)
{
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);

	if (edid) {
		/* empty probed_modes */
		INIT_LIST_HEAD(&connector->probed_modes);
		amdgpu_connector->num_modes =
				drm_add_edid_modes(connector, edid);

		drm_edid_to_eld(connector, edid);

		amdgpu_dm_get_native_mode(connector);
	} else
		amdgpu_connector->num_modes = 0;
}

int amdgpu_dm_connector_get_modes(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper =
			connector->helper_private;
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);
	struct drm_encoder *encoder;
	struct edid *edid = amdgpu_connector->edid;

	encoder = helper->best_encoder(connector);

	amdgpu_dm_connector_ddc_get_modes(connector, edid);
	amdgpu_dm_connector_add_common_modes(encoder, connector);
	return amdgpu_connector->num_modes;
}

void amdgpu_dm_connector_init_helper(
	struct amdgpu_display_manager *dm,
	struct amdgpu_connector *aconnector,
	int connector_type,
	struct dc_link *link,
	int link_index)
{
	struct amdgpu_device *adev = dm->ddev->dev_private;

	aconnector->connector_id = link_index;
	aconnector->dc_link = link;
	aconnector->base.interlace_allowed = false;
	aconnector->base.doublescan_allowed = false;
	aconnector->base.stereo_allowed = false;
	aconnector->base.dpms = DRM_MODE_DPMS_OFF;
	aconnector->hpd.hpd = AMDGPU_HPD_NONE; /* not used */

	mutex_init(&aconnector->hpd_lock);

	/* configure suport HPD hot plug connector_>polled default value is 0
	 * which means HPD hot plug not supported
	 */
	switch (connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	case DRM_MODE_CONNECTOR_DVID:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	default:
		break;
	}

	drm_object_attach_property(&aconnector->base.base,
				dm->ddev->mode_config.scaling_mode_property,
				DRM_MODE_SCALE_NONE);

	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_property,
				UNDERSCAN_OFF);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_hborder_property,
				0);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_vborder_property,
				0);

	if (connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
		drm_object_attach_property(&aconnector->base.base,
					  adev->mode_info.freesync_property, 0);
		drm_object_attach_property(&aconnector->base.base,
				      adev->mode_info.freesync_capable_property,
					   0);
	}
}

int amdgpu_dm_i2c_xfer(struct i2c_adapter *i2c_adap,
		      struct i2c_msg *msgs, int num)
{
	struct amdgpu_i2c_adapter *i2c = i2c_get_adapdata(i2c_adap);
	struct ddc_service *ddc_service = i2c->ddc_service;
	struct i2c_command cmd;
	int i;
	int result = -EIO;

	cmd.payloads = kcalloc(num, sizeof(struct i2c_payload), GFP_KERNEL);

	if (!cmd.payloads)
		return result;

	cmd.number_of_payloads = num;
	cmd.engine = I2C_COMMAND_ENGINE_DEFAULT;
	cmd.speed = 100;

	for (i = 0; i < num; i++) {
		cmd.payloads[i].write = !(msgs[i].flags & I2C_M_RD);
		cmd.payloads[i].address = msgs[i].addr;
		cmd.payloads[i].length = msgs[i].len;
		cmd.payloads[i].data = msgs[i].buf;
	}

	if (dal_i2caux_submit_i2c_command(
			ddc_service->ctx->i2caux,
			ddc_service->ddc_pin,
			&cmd))
		result = num;

	kfree(cmd.payloads);
	return result;
}

u32 amdgpu_dm_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm amdgpu_dm_i2c_algo = {
	.master_xfer = amdgpu_dm_i2c_xfer,
	.functionality = amdgpu_dm_i2c_func,
};

static struct amdgpu_i2c_adapter *create_i2c(
		struct ddc_service *ddc_service,
		int link_index,
		int *res)
{
	struct amdgpu_device *adev = ddc_service->ctx->driver_context;
	struct amdgpu_i2c_adapter *i2c;

	i2c = kzalloc(sizeof(struct amdgpu_i2c_adapter), GFP_KERNEL);
	i2c->base.owner = THIS_MODULE;
	i2c->base.class = I2C_CLASS_DDC;
	i2c->base.dev.parent = &adev->pdev->dev;
	i2c->base.algo = &amdgpu_dm_i2c_algo;
	snprintf(i2c->base.name, sizeof(i2c->base.name), "AMDGPU DM i2c hw bus %d", link_index);
	i2c_set_adapdata(&i2c->base, i2c);
	i2c->ddc_service = ddc_service;

	return i2c;
}

/* Note: this function assumes that dc_link_detect() was called for the
 * dc_link which will be represented by this aconnector.
 */
int amdgpu_dm_connector_init(
	struct amdgpu_display_manager *dm,
	struct amdgpu_connector *aconnector,
	uint32_t link_index,
	struct amdgpu_encoder *aencoder)
{
	int res = 0;
	int connector_type;
	struct dc *dc = dm->dc;
	struct dc_link *link = dc_get_link_at_index(dc, link_index);
	struct amdgpu_i2c_adapter *i2c;
	((struct dc_link *)link)->priv = aconnector;

	DRM_DEBUG_KMS("%s()\n", __func__);

	i2c = create_i2c(link->ddc, link->link_index, &res);
	aconnector->i2c = i2c;
	res = i2c_add_adapter(&i2c->base);

	if (res) {
		DRM_ERROR("Failed to register hw i2c %d\n", link->link_index);
		goto out_free;
	}

	connector_type = to_drm_connector_type(link->connector_signal);

	res = drm_connector_init(
			dm->ddev,
			&aconnector->base,
			&amdgpu_dm_connector_funcs,
			connector_type);

	if (res) {
		DRM_ERROR("connector_init failed\n");
		aconnector->connector_id = -1;
		goto out_free;
	}

	drm_connector_helper_add(
			&aconnector->base,
			&amdgpu_dm_connector_helper_funcs);

	amdgpu_dm_connector_init_helper(
		dm,
		aconnector,
		connector_type,
		link,
		link_index);

	drm_mode_connector_attach_encoder(
		&aconnector->base, &aencoder->base);

	drm_connector_register(&aconnector->base);

	if (connector_type == DRM_MODE_CONNECTOR_DisplayPort
		|| connector_type == DRM_MODE_CONNECTOR_eDP)
		amdgpu_dm_initialize_dp_connector(dm, aconnector);

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

	/* NOTE: this currently will create backlight device even if a panel
	 * is not connected to the eDP/LVDS connector.
	 *
	 * This is less than ideal but we don't have sink information at this
	 * stage since detection happens after. We can't do detection earlier
	 * since MST detection needs connectors to be created first.
	 */
	if (link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) {
		/* Event if registration failed, we should continue with
		 * DM initialization because not having a backlight control
		 * is better then a black screen.
		 */
		amdgpu_dm_register_backlight_device(dm);

		if (dm->backlight_dev)
			dm->backlight_link = link;
	}
#endif

out_free:
	if (res) {
		kfree(i2c);
		aconnector->i2c = NULL;
	}
	return res;
}

int amdgpu_dm_get_encoder_crtc_mask(struct amdgpu_device *adev)
{
	switch (adev->mode_info.num_crtc) {
	case 1:
		return 0x1;
	case 2:
		return 0x3;
	case 3:
		return 0x7;
	case 4:
		return 0xf;
	case 5:
		return 0x1f;
	case 6:
	default:
		return 0x3f;
	}
}

int amdgpu_dm_encoder_init(
	struct drm_device *dev,
	struct amdgpu_encoder *aencoder,
	uint32_t link_index)
{
	struct amdgpu_device *adev = dev->dev_private;

	int res = kcl_drm_encoder_init(dev,
				   &aencoder->base,
				   &amdgpu_dm_encoder_funcs,
				   DRM_MODE_ENCODER_TMDS,
				   NULL);

	aencoder->base.possible_crtcs = amdgpu_dm_get_encoder_crtc_mask(adev);

	if (!res)
		aencoder->encoder_id = link_index;
	else
		aencoder->encoder_id = -1;

	drm_encoder_helper_add(&aencoder->base, &amdgpu_dm_encoder_helper_funcs);

	return res;
}

static bool modeset_required(struct drm_crtc_state *crtc_state)
{
	if (!(crtc_state->mode_changed || crtc_state->active_changed
#if !defined(OS_NAME_RHEL_7_2)
				|| crtc_state->connectors_changed
#endif
				))
		return false;

	if (!crtc_state->enable)
		return false;

	return crtc_state->active;
}

static bool modereset_required(struct drm_crtc_state *crtc_state)
{
	if (!(crtc_state->mode_changed || crtc_state->active_changed
#if !defined(OS_NAME_RHEL_7_2)
				|| crtc_state->connectors_changed
#endif
				))
		return false;

	return !crtc_state->enable || !crtc_state->active;
}

typedef bool (*predicate)(struct amdgpu_crtc *acrtc);

static void wait_while_pflip_status(struct amdgpu_device *adev,
		struct amdgpu_crtc *acrtc, predicate f) {
	int count = 0;
	while (f(acrtc)) {
		/* Spin Wait*/
		msleep(1);
		count++;
		if (count == 1000) {
			DRM_ERROR("%s - crtc:%d[%p], pflip_stat:%d, probable hang!\n",
										__func__, acrtc->crtc_id,
										acrtc,
										acrtc->pflip_status);

			/* we do not expect to hit this case except on Polaris with PHY PLL
			 * 1. DP to HDMI passive dongle connected
			 * 2. unplug (headless)
			 * 3. plug in DP
			 * 3a. on plug in, DP will try verify link by training, and training
			 * would disable PHY PLL which HDMI rely on to drive TG
			 * 3b. this will cause flip interrupt cannot be generated, and we
			 * exit when timeout expired.  however we do not have code to clean
			 * up flip, flip clean up will happen when the address is written
			 * with the restore mode change
			 */
			WARN_ON(1);
			break;
		}
	}

	DRM_DEBUG_DRIVER("%s - Finished waiting for:%d msec, crtc:%d[%p], pflip_stat:%d \n",
											__func__,
											count,
											acrtc->crtc_id,
											acrtc,
											acrtc->pflip_status);
}

static bool pflip_in_progress_predicate(struct amdgpu_crtc *acrtc)
{
	return acrtc->pflip_status != AMDGPU_FLIP_NONE;
}

static void manage_dm_interrupts(
	struct amdgpu_device *adev,
	struct amdgpu_crtc *acrtc,
	bool enable)
{
	/*
	 * this is not correct translation but will work as soon as VBLANK
	 * constant is the same as PFLIP
	 */
	int irq_type =
		amdgpu_crtc_idx_to_irq_type(
			adev,
			acrtc->crtc_id);

	if (enable) {
		drm_crtc_vblank_on(&acrtc->base);
		amdgpu_irq_get(
			adev,
			&adev->pageflip_irq,
			irq_type);
	} else {
		wait_while_pflip_status(adev, acrtc,
				pflip_in_progress_predicate);

		amdgpu_irq_put(
			adev,
			&adev->pageflip_irq,
			irq_type);
		drm_crtc_vblank_off(&acrtc->base);
	}
}


static bool pflip_pending_predicate(struct amdgpu_crtc *acrtc)
{
	return acrtc->pflip_status == AMDGPU_FLIP_PENDING;
}

static bool is_scaling_state_different(
		const struct dm_connector_state *dm_state,
		const struct dm_connector_state *old_dm_state)
{
	if (dm_state->scaling != old_dm_state->scaling)
		return true;
	if (!dm_state->underscan_enable && old_dm_state->underscan_enable) {
		if (old_dm_state->underscan_hborder != 0 && old_dm_state->underscan_vborder != 0)
			return true;
	} else if (dm_state->underscan_enable && !old_dm_state->underscan_enable) {
		if (dm_state->underscan_hborder != 0 && dm_state->underscan_vborder != 0)
			return true;
	} else if (dm_state->underscan_hborder != old_dm_state->underscan_hborder ||
		   dm_state->underscan_vborder != old_dm_state->underscan_vborder)
		return true;
	return false;
}

static void remove_stream(struct amdgpu_device *adev, struct amdgpu_crtc *acrtc)
{
	/*
	 * we evade vblanks and pflips on crtc that
	 * should be changed
	 */
	manage_dm_interrupts(adev, acrtc, false);

	/* this is the update mode case */
	if (adev->dm.freesync_module)
		mod_freesync_remove_stream(adev->dm.freesync_module,
					   acrtc->stream);

	dc_stream_release(acrtc->stream);
	acrtc->stream = NULL;
	acrtc->otg_inst = -1;
	acrtc->enabled = false;
}

void dc_commit_plane_states(struct drm_atomic_state *state,
	struct drm_device *dev, struct amdgpu_display_manager *dm)
{
	uint32_t i;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	struct amdgpu_device *adev = dev->dev_private;

	/* update planes when needed */
	for_each_plane_in_state(state, plane, old_plane_state, i) {
		struct drm_plane_state *plane_state = plane->state;
		struct drm_crtc *crtc = plane_state->crtc;
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
		struct drm_framebuffer *fb = plane_state->fb;
		struct drm_connector *connector;
		struct dm_connector_state *con_state = NULL;

		if (!fb || !crtc || !crtc->state->planes_changed ||
			!crtc->state->active)
			continue;

		/* Surfaces are created under two scenarios:
		 * 1. This commit is not a page flip.
		 * 2. This commit is a page flip, and streams are created.
		 */
		if (!page_flip_needed(
				plane_state,
				old_plane_state,
				crtc->state->event, true) ||
				modeset_required(crtc->state)) {
			list_for_each_entry(connector,
					    &dev->mode_config.connector_list,
					    head) {
				if (connector->state->crtc == crtc) {
					con_state = to_dm_connector_state(
						connector->state);
					break;
				}
			}

			/*
			 * This situation happens in the following case:
			 * we are about to get set mode for connector who's only
			 * possible crtc (in encoder crtc mask) is used by
			 * another connector, that is why it will try to
			 * re-assing crtcs in order to make configuration
			 * supported. For our implementation we need to make all
			 * encoders support all crtcs, then this issue will
			 * never arise again. But to guard code from this issue
			 * check is left.
			 *
			 * Also it should be needed when used with actual
			 * drm_atomic_commit ioctl in future
			 */
			if (!con_state)
				continue;

			/*
			 * if flip is pending (ie, still waiting for fence to return
			 * before address is submitted) here, we cannot commit_surface
			 * as commit_surface will pre-maturely write out the future
			 * address. wait until flip is submitted before proceeding.
			 */
			wait_while_pflip_status(adev, acrtc, pflip_pending_predicate);

			dm_dc_plane_state_commit(dm->dc, crtc);
		}
	}
}

int amdgpu_dm_atomic_commit(
	struct drm_device *dev,
	struct drm_atomic_state *state,
	bool nonblock)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	struct drm_plane_state *old_plane_state;
	uint32_t i;
	int32_t ret = 0;
	uint32_t commit_streams_count = 0;
	uint32_t new_crtcs_count = 0;
	uint32_t flip_crtcs_count = 0;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	struct dc_stream_state *commit_streams[MAX_STREAMS];
	struct amdgpu_crtc *new_crtcs[MAX_STREAMS];
	struct dc_stream_state *new_stream;
	struct drm_crtc *flip_crtcs[MAX_STREAMS];
	struct amdgpu_flip_work *work[MAX_STREAMS] = {0};
	struct amdgpu_bo *new_abo[MAX_STREAMS] = {0};
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state;

	/* In this step all new fb would be pinned */

	/*
	 * TODO: Revisit when we support true asynchronous commit.
	 * Right now we receive async commit only from pageflip, in which case
	 * we should not pin/unpin the fb here, it should be done in
	 * amdgpu_crtc_flip and from the vblank irq handler.
	 */
	if (!nonblock) {
		ret = drm_atomic_helper_prepare_planes(dev, state);
		if (ret)
			return ret;
	}

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(dev, state);

	/*
	 * From this point state become old state really. New state is
	 * initialized to appropriate objects and could be accessed from there
	 */

	/*
	 * there is no fences usage yet in state. We can skip the following line
	 * wait_for_fences(dev, state);
	 */

	kcl_drm_atomic_helper_update_legacy_modeset_state(dev, state);

	/* update changed items */
	for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
		struct amdgpu_crtc *acrtc;
		struct amdgpu_connector *aconnector = NULL;
		struct drm_crtc_state *new_state = crtc->state;

		acrtc = to_amdgpu_crtc(crtc);

		aconnector =
			amdgpu_dm_find_first_crct_matching_connector(
				state,
				crtc,
				false);

		DRM_DEBUG_KMS(
			"amdgpu_crtc id:%d crtc_state_flags: enable:%d, active:%d, "
			"planes_changed:%d, mode_changed:%d, active_changed:%d"
#if !defined(OS_NAME_RHEL_7_2)
			", connectors_changed:%d"
#endif
			"\n",
			acrtc->crtc_id,
			new_state->enable,
			new_state->active,
			new_state->planes_changed,
			new_state->mode_changed,
			new_state->active_changed
#if !defined(OS_NAME_RHEL_7_2)
			, new_state->connectors_changed
#endif
			);

		/* handles headless hotplug case, updating new_state and
		 * aconnector as needed
		 */

		if (modeset_required(new_state)) {
			struct dm_connector_state *dm_state = NULL;
			new_stream = NULL;

			if (aconnector)
				dm_state = to_dm_connector_state(aconnector->base.state);

			new_stream = create_stream_for_sink(
					aconnector,
					&crtc->state->mode,
					dm_state);

			DRM_INFO("Atomic commit: SET crtc id %d: [%p]\n", acrtc->crtc_id, acrtc);

			if (!new_stream) {
				/*
				 * this could happen because of issues with
				 * userspace notifications delivery.
				 * In this case userspace tries to set mode on
				 * display which is disconnect in fact.
				 * dc_sink in NULL in this case on aconnector.
				 * We expect reset mode will come soon.
				 *
				 * This can also happen when unplug is done
				 * during resume sequence ended
				 *
				 * In this case, we want to pretend we still
				 * have a sink to keep the pipe running so that
				 * hw state is consistent with the sw state
				 */
				DRM_DEBUG_KMS("%s: Failed to create new stream for crtc %d\n",
						__func__, acrtc->base.base.id);
				break;
			}

			if (acrtc->stream)
				remove_stream(adev, acrtc);

			/*
			 * this loop saves set mode crtcs
			 * we needed to enable vblanks once all
			 * resources acquired in dc after dc_commit_streams
			 */
			new_crtcs[new_crtcs_count] = acrtc;
			new_crtcs_count++;

			acrtc->stream = new_stream;
			acrtc->enabled = true;
			acrtc->hw_mode = crtc->state->mode;
			crtc->hwmode = crtc->state->mode;
		} else if (modereset_required(new_state)) {
			DRM_INFO("Atomic commit: RESET. crtc id %d:[%p]\n", acrtc->crtc_id, acrtc);
			/* i.e. reset mode */
			if (acrtc->stream)
				remove_stream(adev, acrtc);
		}
	} /* for_each_crtc_in_state() */

	/* Handle scaling and underscan changes */
	for_each_connector_in_state(state, connector, old_conn_state, i) {
		struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);
		struct dm_connector_state *con_new_state =
				to_dm_connector_state(aconnector->base.state);
		struct dm_connector_state *con_old_state =
				to_dm_connector_state(old_conn_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(con_new_state->base.crtc);
		struct drm_crtc_state *crtc_state;
		struct dc_stream_status *status = NULL;

		if (!acrtc)
			continue;

		/* Skip any modesets/resets */
		crtc_state = acrtc->base.state;
		if (crtc_state->mode_changed || crtc_state->active_changed
#if !defined(OS_NAME_RHEL_7_2)
				|| crtc_state->connectors_changed
#endif
		   )
			continue;

		/* Skip any thing not scale or underscan changes */
		if (!is_scaling_state_different(con_new_state, con_old_state))
			continue;

		update_stream_scaling_settings(&con_new_state->base.crtc->mode,
				con_new_state, (struct dc_stream_state *)acrtc->stream);

		status = dc_stream_get_status(acrtc->stream);
		WARN_ON(!status);
		WARN_ON(!status->plane_count);

		/* TODO How it works with MPO ? */
		if (!dc_commit_planes_to_stream(
					dm->dc,
					status->plane_states,
					status->plane_count,
					acrtc->stream))
			dm_error("%s: Failed to update stream scaling!\n", __func__);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (acrtc->stream) {
			commit_streams[commit_streams_count] = acrtc->stream;
			++commit_streams_count;
		}
	}

	/*
	 * Add streams after required streams from new and replaced streams
	 * are removed from freesync module
	 */
	if (adev->dm.freesync_module) {
		for (i = 0; i < new_crtcs_count; i++) {
			struct amdgpu_connector *aconnector = NULL;
			new_stream = new_crtcs[i]->stream;
			aconnector =
				amdgpu_dm_find_first_crct_matching_connector(
					state,
					&new_crtcs[i]->base,
					false);
			if (!aconnector) {
				DRM_INFO("Atomic commit: Failed to find connector for acrtc id:%d "
					 "skipping freesync init\n",
					 new_crtcs[i]->crtc_id);
				continue;
			}

			mod_freesync_add_stream(adev->dm.freesync_module,
						new_stream, &aconnector->caps);
		}
	}

	/* DC is optimized not to do anything if 'streams' didn't change. */
	dc_commit_streams(dm->dc, commit_streams, commit_streams_count);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (acrtc->stream != NULL)
			acrtc->otg_inst =
				dc_stream_get_status(acrtc->stream)->primary_otg_inst;
	}

	dc_commit_plane_states(state, dev, dm);

	for (i = 0; i < new_crtcs_count; i++) {
		/*
		 * loop to enable interrupts on newly arrived crtc
		 */
		struct amdgpu_crtc *acrtc = new_crtcs[i];

		if (adev->dm.freesync_module)
			mod_freesync_notify_mode_change(
				adev->dm.freesync_module, &acrtc->stream, 1);

		manage_dm_interrupts(adev, acrtc, true);
		dm_crtc_cursor_reset(&acrtc->base);

	}

	/* Do actual flip */
	flip_crtcs_count = 0;
	for_each_plane_in_state(state, plane, old_plane_state, i) {
		struct drm_plane_state *plane_state = plane->state;
		struct drm_crtc *crtc = plane_state->crtc;
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
		struct drm_framebuffer *fb = plane_state->fb;

		if (!fb || !crtc || !crtc->state->planes_changed ||
			!crtc->state->active)
			continue;

		if (page_flip_needed(
				plane_state,
				old_plane_state,
				crtc->state->event,
				false)) {
			ret = amdgpu_crtc_page_flip(crtc,
						    fb,
						    crtc->state->event,
						    acrtc->flip_flags);
			/*clean up the flags for next usage*/
			acrtc->flip_flags = 0;
			if (ret)
				return ret;
		}
	}

	/* In this state all old framebuffers would be unpinned */

	/* TODO: Revisit when we support true asynchronous commit.*/
	if (!nonblock)
		drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_state_free(state);

	return ret;
}
/*
 * This functions handle all cases when set mode does not come upon hotplug.
 * This include when the same display is unplugged then plugged back into the
 * same port and when we are running without usermode desktop manager supprot
 */
void dm_restore_drm_connector_state(struct drm_device *dev, struct drm_connector *connector)
{
	struct drm_crtc *crtc;
	struct amdgpu_device *adev = dev->dev_private;
	struct dc *dc = adev->dm.dc;
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);
	struct amdgpu_crtc *disconnected_acrtc;
	const struct dc_sink *sink;
	struct dc_stream_state *commit_streams[MAX_STREAMS];
	struct dc_stream_state *current_stream;
	uint32_t commit_streams_count = 0;

	if (!aconnector->dc_sink || !connector->state || !connector->encoder)
		return;

	disconnected_acrtc = to_amdgpu_crtc(connector->encoder->crtc);

	if (!disconnected_acrtc || !disconnected_acrtc->stream)
		return;

	sink = disconnected_acrtc->stream->sink;

	/*
	 * If the previous sink is not released and different from the current,
	 * we deduce we are in a state where we can not rely on usermode call
	 * to turn on the display, so we do it here
	 */
	if (sink != aconnector->dc_sink) {
		struct dm_connector_state *dm_state =
				to_dm_connector_state(aconnector->base.state);

		struct dc_stream_state *new_stream =
			create_stream_for_sink(
				aconnector,
				&disconnected_acrtc->base.state->mode,
				dm_state);

		DRM_INFO("Headless hotplug, restoring connector state\n");
		/*
		 * we evade vblanks and pflips on crtc that
		 * should be changed
		 */
		manage_dm_interrupts(adev, disconnected_acrtc, false);
		/* this is the update mode case */

		current_stream = disconnected_acrtc->stream;

		disconnected_acrtc->stream = new_stream;
		disconnected_acrtc->enabled = true;
		disconnected_acrtc->hw_mode = disconnected_acrtc->base.state->mode;

		commit_streams_count = 0;

		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

			if (acrtc->stream) {
				commit_streams[commit_streams_count] = acrtc->stream;
				++commit_streams_count;
			}
		}

		/* DC is optimized not to do anything if 'streams' didn't change. */
		if (!dc_commit_streams(dc, commit_streams,
				commit_streams_count)) {
			DRM_INFO("Failed to restore connector state!\n");
			dc_stream_release(disconnected_acrtc->stream);
			disconnected_acrtc->stream = current_stream;
			manage_dm_interrupts(adev, disconnected_acrtc, true);
			return;
		}

		if (adev->dm.freesync_module) {
			mod_freesync_remove_stream(adev->dm.freesync_module,
				current_stream);

			mod_freesync_add_stream(adev->dm.freesync_module,
						new_stream, &aconnector->caps);
		}

		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

			if (acrtc->stream != NULL) {
				acrtc->otg_inst =
					dc_stream_get_status(acrtc->stream)->primary_otg_inst;
			}
		}

		dc_stream_release(current_stream);

		dm_dc_plane_state_commit(dc, &disconnected_acrtc->base);

		manage_dm_interrupts(adev, disconnected_acrtc, true);
		dm_crtc_cursor_reset(&disconnected_acrtc->base);

	}
}

static uint32_t add_val_sets_plane(
	struct dc_validation_set *val_sets,
	uint32_t set_count,
	const struct dc_stream_state *stream,
	struct dc_plane_state *plane_state)
{
	uint32_t i = 0;

	while (i < set_count) {
		if (val_sets[i].stream == stream)
			break;
		++i;
	}

	val_sets[i].plane_states[val_sets[i].plane_count] = plane_state;
	val_sets[i].plane_count++;

	return val_sets[i].plane_count;
}

static uint32_t update_in_val_sets_stream(
	struct dc_validation_set *val_sets,
	struct drm_crtc **crtcs,
	uint32_t set_count,
	struct dc_stream_state *old_stream,
	struct dc_stream_state *new_stream,
	struct drm_crtc *crtc)
{
	uint32_t i = 0;

	while (i < set_count) {
		if (val_sets[i].stream == old_stream)
			break;
		++i;
	}

	val_sets[i].stream = new_stream;
	crtcs[i] = crtc;

	if (i == set_count) {
		/* nothing found. add new one to the end */
		return set_count + 1;
	}

	return set_count;
}

static uint32_t remove_from_val_sets(
	struct dc_validation_set *val_sets,
	uint32_t set_count,
	const struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < set_count; i++)
		if (val_sets[i].stream == stream)
			break;

	if (i == set_count) {
		/* nothing found */
		return set_count;
	}

	set_count--;

	for (; i < set_count; i++)
		val_sets[i] = val_sets[i + 1];

	return set_count;
}

int amdgpu_dm_atomic_check(struct drm_device *dev,
			struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i, j;
	int ret;
	int set_count;
	int new_stream_count;
	struct dc_validation_set set[MAX_STREAMS] = {{ 0 }};
	struct dc_stream_state *new_streams[MAX_STREAMS] = { 0 };
	struct drm_crtc *crtc_set[MAX_STREAMS] = { 0 };
	struct amdgpu_device *adev = dev->dev_private;
	struct dc *dc = adev->dm.dc;
	bool need_to_validate = false;
	struct validate_context *context;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;

	ret = drm_atomic_helper_check(dev, state);

	if (ret) {
		DRM_ERROR("Atomic state validation failed with error :%d !\n",
				ret);
		return ret;
	}

	ret = -EINVAL;

	/* copy existing configuration */
	new_stream_count = 0;
	set_count = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (acrtc->stream) {
			set[set_count].stream = acrtc->stream;
			crtc_set[set_count] = crtc;
			++set_count;
		}
	}

	/* update changed items */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct amdgpu_crtc *acrtc = NULL;
		struct amdgpu_connector *aconnector = NULL;

		acrtc = to_amdgpu_crtc(crtc);

		aconnector = amdgpu_dm_find_first_crct_matching_connector(state, crtc, true);

		DRM_DEBUG_KMS(
			"amdgpu_crtc id:%d crtc_state_flags: enable:%d, active:%d, "
			"planes_changed:%d, mode_changed:%d, active_changed:%d"
#if !defined(OS_NAME_RHEL_7_2)
			", connectors_changed:%d"
#endif
			"\n",
			acrtc->crtc_id,
			crtc_state->enable,
			crtc_state->active,
			crtc_state->planes_changed,
			crtc_state->mode_changed,
			crtc_state->active_changed
#if !defined(OS_NAME_RHEL_7_2)
			, crtc_state->connectors_changed
#endif
			);

		if (modeset_required(crtc_state)) {
			struct dc_stream *new_stream = NULL;
			struct drm_connector_state *conn_state = NULL;
			struct dm_connector_state *dm_state = NULL;

			if (aconnector) {
				conn_state = drm_atomic_get_connector_state(state, &aconnector->base);
				if (IS_ERR(conn_state))
					return ret;
				dm_state = to_dm_connector_state(conn_state);
			}

			new_stream = create_stream_for_sink(aconnector, &crtc_state->mode, dm_state);

			/*
			 * we can have no stream on ACTION_SET if a display
			 * was disconnected during S3, in this case it not and
			 * error, the OS will be updated after detection, and
			 * do the right thing on next atomic commit
			 */
			if (!new_stream) {
				DRM_DEBUG_KMS("%s: Failed to create new stream for crtc %d\n",
						__func__, acrtc->base.base.id);
				break;
			}

			new_streams[new_stream_count] = new_stream;
			set_count = update_in_val_sets_stream(
					set,
					crtc_set,
					set_count,
					acrtc->stream,
					new_stream,
					crtc);

			new_stream_count++;
			need_to_validate = true;
		} else if (modereset_required(crtc_state)) {
			/* i.e. reset mode */
			if (acrtc->stream) {
				set_count = remove_from_val_sets(
						set,
						set_count,
						acrtc->stream);
			}
		}
	}

	/* Check scaling and underscan changes */
	for_each_connector_in_state(state, connector, conn_state, i) {
		struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);
		struct dm_connector_state *con_old_state =
				to_dm_connector_state(aconnector->base.state);
		struct dm_connector_state *con_new_state =
				to_dm_connector_state(conn_state);
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(con_new_state->base.crtc);
		struct drm_crtc_state *crtc_state;
		struct dc_stream_state *new_stream;

		if (!acrtc)
			continue;

		/* Skip any modesets/resets */
		crtc_state = acrtc->base.state;
		if (crtc_state->mode_changed || crtc_state->active_changed
#if !defined(OS_NAME_RHEL_7_2)
				|| crtc_state->connectors_changed
#endif
		   )
			continue;

		/* Skip any thing not scale or underscan changes */
		if (!is_scaling_state_different(con_new_state, con_old_state))
			continue;

		new_stream = create_stream_for_sink(
				aconnector,
				&acrtc->base.state->mode,
				con_new_state);

		if (!new_stream) {
			DRM_ERROR("%s: Failed to create new stream for crtc %d\n",
					__func__, acrtc->base.base.id);
			continue;
		}

		new_streams[new_stream_count] = new_stream;
		set_count = update_in_val_sets_stream(
				set,
				crtc_set,
				set_count,
				acrtc->stream,
				new_stream,
				&acrtc->base);
		new_stream_count++;
		need_to_validate = true;
	}

	for (i = 0; i < set_count; i++) {
		for_each_plane_in_state(state, plane, plane_state, j) {
			struct drm_plane_state *old_plane_state = plane->state;
			struct drm_crtc *crtc = plane_state->crtc;
			struct drm_framebuffer *fb = plane_state->fb;
			struct drm_connector *connector;
			struct dm_connector_state *dm_state = NULL;
			struct drm_crtc_state *crtc_state;


			if (!fb || !crtc || crtc_set[i] != crtc ||
				!crtc->state->planes_changed || !crtc->state->active)
				continue;

			/* Surfaces are created under two scenarios:
			 * 1. This commit is not a page flip.
			 * 2. This commit is a page flip, and streams are created.
			 */
			crtc_state = drm_atomic_get_crtc_state(state, crtc);
			if (!page_flip_needed(plane_state, old_plane_state,
					crtc_state->event, true) ||
					modeset_required(crtc_state)) {
				struct dc_plane_state *dc_plane_state;

				list_for_each_entry(connector,
					&dev->mode_config.connector_list, head)	{
					if (connector->state->crtc == crtc) {
						dm_state = to_dm_connector_state(
							connector->state);
						break;
					}
				}

				/*
				 * This situation happens in the following case:
				 * we are about to get set mode for connector who's only
				 * possible crtc (in encoder crtc mask) is used by
				 * another connector, that is why it will try to
				 * re-assing crtcs in order to make configuration
				 * supported. For our implementation we need to make all
				 * encoders support all crtcs, then this issue will
				 * never arise again. But to guard code from this issue
				 * check is left.
				 *
				 * Also it should be needed when used with actual
				 * drm_atomic_commit ioctl in future
				 */
				if (!dm_state)
					continue;

				dc_plane_state = dc_create_plane_state(dc);
				fill_plane_attributes(
					crtc->dev->dev_private,
					dc_plane_state,
					plane_state,
					false);

				add_val_sets_plane(
							set,
							set_count,
							set[i].stream,
							dc_plane_state);

				need_to_validate = true;
			}
		}
	}

	context = dc_get_validate_context(dc, set, set_count);

	if (need_to_validate == false || set_count == 0 || context)
		ret = 0;

	if (context) {
		dc_resource_validate_ctx_destruct(context);
		dm_free(context);
	}

	for (i = 0; i < set_count; i++)
		for (j = 0; j < set[i].plane_count; j++)
			dc_plane_state_release(set[i].plane_states[j]);

	for (i = 0; i < new_stream_count; i++)
		dc_stream_release(new_streams[i]);

	if (ret != 0)
		DRM_ERROR("Atomic check failed.\n");

	return ret;
}

static bool is_dp_capable_without_timing_msa(
		struct dc *dc,
		struct amdgpu_connector *amdgpu_connector)
{
	uint8_t dpcd_data;
	bool capable = false;

	if (amdgpu_connector->dc_link &&
		dm_helpers_dp_read_dpcd(
				NULL,
				amdgpu_connector->dc_link,
				DP_DOWN_STREAM_PORT_COUNT,
				&dpcd_data,
				sizeof(dpcd_data))) {
		capable = (dpcd_data & DP_MSA_TIMING_PAR_IGNORED) ? true:false;
	}

	return capable;
}
void amdgpu_dm_add_sink_to_freesync_module(
		struct drm_connector *connector,
		struct edid *edid)
{
	int i;
	uint64_t val_capable;
	bool edid_check_required;
	struct detailed_timing *timing;
	struct detailed_non_pixel *data;
	struct detailed_data_monitor_range *range;
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);

	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;

	edid_check_required = false;
	if (!amdgpu_connector->dc_sink) {
		DRM_ERROR("dc_sink NULL, could not add free_sync module.\n");
		return;
	}
	if (!adev->dm.freesync_module)
		return;
	/*
	 * if edid non zero restrict freesync only for dp and edp
	 */
	if (edid) {
		if (amdgpu_connector->dc_sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT
			|| amdgpu_connector->dc_sink->sink_signal == SIGNAL_TYPE_EDP) {
			edid_check_required = is_dp_capable_without_timing_msa(
						adev->dm.dc,
						amdgpu_connector);
		}
	}
	val_capable = 0;
	if (edid_check_required == true && (edid->version > 1 ||
	   (edid->version == 1 && edid->revision > 1))) {
		for (i = 0; i < 4; i++) {

			timing	= &edid->detailed_timings[i];
			data	= &timing->data.other_data;
			range	= &data->data.range;
			/*
			 * Check if monitor has continuous frequency mode
			 */
			if (data->type != EDID_DETAIL_MONITOR_RANGE)
				continue;
			/*
			 * Check for flag range limits only. If flag == 1 then
			 * no additional timing information provided.
			 * Default GTF, GTF Secondary curve and CVT are not
			 * supported
			 */
			if (range->flags != 1)
				continue;

			amdgpu_connector->min_vfreq = range->min_vfreq;
			amdgpu_connector->max_vfreq = range->max_vfreq;
			amdgpu_connector->pixel_clock_mhz =
				range->pixel_clock_mhz * 10;
			break;
		}

		if (amdgpu_connector->max_vfreq -
				amdgpu_connector->min_vfreq > 10) {
			amdgpu_connector->caps.supported = true;
			amdgpu_connector->caps.min_refresh_in_micro_hz =
					amdgpu_connector->min_vfreq * 1000000;
			amdgpu_connector->caps.max_refresh_in_micro_hz =
					amdgpu_connector->max_vfreq * 1000000;
				val_capable = 1;
		}
	}
	drm_object_property_set_value(&connector->base,
				      adev->mode_info.freesync_capable_property,
				      val_capable);
	amdgpu_freesync_update_property_atomic(connector);

}

void amdgpu_dm_remove_sink_from_freesync_module(
		struct drm_connector *connector)
{
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);

	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;

	if (!amdgpu_connector->dc_sink || !adev->dm.freesync_module) {
		DRM_ERROR("dc_sink NULL or no free_sync module.\n");
		return;
	}

	amdgpu_connector->min_vfreq = 0;
	amdgpu_connector->max_vfreq = 0;
	amdgpu_connector->pixel_clock_mhz = 0;

	memset(&amdgpu_connector->caps, 0, sizeof(amdgpu_connector->caps));

	drm_object_property_set_value(&connector->base,
				      adev->mode_info.freesync_capable_property,
				      0);
	amdgpu_freesync_update_property_atomic(connector);

}

#endif
