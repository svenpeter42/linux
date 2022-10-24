// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2022 Sven Peter <sven@svenpeter.dev> */

#include <linux/completion.h>

#include "afk.h"
#include "dcp.h"
#include "dptxep.h"
#include "parser.h"
#include "trace.h"

struct dcpdptx_connection_cmd {
	__le32 unk;
	__le32 target;
} __attribute__((packed));

struct dcpdptx_hotplug_cmd {
	u8 _pad0[16];
	__le32 unk;
} __attribute__((packed));

int dptxport_validate_connection(struct apple_epic_service *service, u8 core,
				 u8 atc, u8 die)
{
	struct dcpdptx_connection_cmd cmd, resp;
	int ret;
	u32 target = FIELD_PREP(DCPDPTX_REMOTE_PORT_CORE, core) |
		     FIELD_PREP(DCPDPTX_REMOTE_PORT_ATC, atc) |
		     FIELD_PREP(DCPDPTX_REMOTE_PORT_DIE, die) |
		     DCPDPTX_REMOTE_PORT_CONNECTED;

	trace_dptxport_validate_connection(service, core, atc, die);

	cmd.target = cpu_to_le32(target);
	cmd.unk = cpu_to_le32(0x100);
	ret = afk_service_call(service, 0, 14, &cmd, sizeof(cmd), 40, &resp,
			       sizeof(resp), 40);
	if (ret)
		return ret;

	if (le32_to_cpu(resp.target) != target)
		return -EINVAL;
	if (le32_to_cpu(resp.unk) != 0x100)
		return -EINVAL;

	return 0;
}

int dptxport_connect(struct apple_epic_service *service, u8 core, u8 atc,
		     u8 die)
{
	struct dcpdptx_connection_cmd cmd, resp;
	int ret;
	u32 target = FIELD_PREP(DCPDPTX_REMOTE_PORT_CORE, core) |
		     FIELD_PREP(DCPDPTX_REMOTE_PORT_ATC, atc) |
		     FIELD_PREP(DCPDPTX_REMOTE_PORT_DIE, die) |
		     DCPDPTX_REMOTE_PORT_CONNECTED;

	trace_dptxport_connect(service, core, atc, die);

	cmd.target = cpu_to_le32(target);
	cmd.unk = cpu_to_le32(0x100);
	ret = afk_service_call(service, 0, 13, &cmd, sizeof(cmd), 24, &resp,
			       sizeof(resp), 24);
	if (ret)
		return ret;

	if (le32_to_cpu(resp.target) != target)
		return -EINVAL;
	if (le32_to_cpu(resp.unk) != 0x100)
		return -EINVAL;

	return 0;
}

int dptxport_request_display(struct apple_epic_service *service)
{
	return afk_service_call(service, 0, 8, NULL, 0, 16, NULL, 0, 16);
}

int dptxport_release_display(struct apple_epic_service *service)
{
	return afk_service_call(service, 0, 9, NULL, 0, 16, NULL, 0, 16);
}

int dptxport_do_hotplug(struct apple_epic_service *service)
{
	struct dcpdptx_hotplug_cmd cmd, resp;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.unk = cpu_to_le32(1);

	ret = afk_service_call(service, 8, 10, &cmd, sizeof(cmd), 12, &resp,
			       sizeof(resp), 12);
	if (ret)
		return ret;
	if (le32_to_cpu(resp.unk) != 1)
		return -EINVAL;
	return 0;
}

struct dptxport_apcall_link_rate {
	__le32 retcode;
	u8 _unk0[12];
	__le32 link_rate;
	u8 _unk1[12];
} __attribute__((packed));

struct dptxport_apcall_get_support {
	__le32 retcode;
	u8 _unk0[12];
	__le32 supported;
	u8 _unk1[12];
} __attribute__((packed));

static int dptxport_call_get_max_link_rate(struct apple_epic_service *service,
					   void *reply_, size_t reply_size)
{
	struct dptxport_apcall_link_rate *reply = reply_;

	if (reply_size < sizeof(*reply))
		return -EINVAL;

	reply->retcode = cpu_to_le32(0);
	reply->link_rate = cpu_to_le32(0x1e);
	return 0;
}

static int dptxport_call_get_link_rate(struct apple_epic_service *service,
				       void *reply_, size_t reply_size)
{
	struct dptxport_apcall_link_rate *reply = reply_;

	if (reply_size < sizeof(*reply))
		return -EINVAL;

	reply->retcode = cpu_to_le32(0);
	reply->link_rate = cpu_to_le32(0xa);
	return 0;
}

static int dptxport_call_set_link_rate(struct apple_epic_service *service,
				       const void *data, size_t data_size,
				       void *reply_, size_t reply_size)
{
	const struct dptxport_apcall_link_rate *request = data;
	struct dptxport_apcall_link_rate *reply = reply_;
	u32 link_rate;

	if (reply_size < sizeof(*reply))
		return -EINVAL;
	if (data_size < sizeof(*request))
		return -EINVAL;

	link_rate = le32_to_cpu(request->link_rate);
	reply->retcode = cpu_to_le32(0);
	reply->link_rate = cpu_to_le32(0xa);
	return 0;
}

static int dptxport_call_get_supports_hpd(struct apple_epic_service *service,
					  void *reply_, size_t reply_size)
{
	struct dptxport_apcall_get_support *reply = reply_;

	if (reply_size < sizeof(*reply))
		return -EINVAL;

	reply->retcode = cpu_to_le32(0);
	reply->supported = cpu_to_le32(1);
	return 0;
}

static int
dptxport_call_get_supports_downspread(struct apple_epic_service *service,
				      void *reply_, size_t reply_size)
{
	struct dptxport_apcall_get_support *reply = reply_;

	if (reply_size < sizeof(*reply))
		return -EINVAL;

	reply->retcode = cpu_to_le32(0);
	reply->supported = cpu_to_le32(0);
	return 0;
}

static int dptxport_call(struct apple_epic_service *service, u32 idx,
			 const void *data, size_t data_size, void *reply,
			 size_t reply_size)
{
	trace_dptxport_apcall(service, idx, data_size);

	switch (idx) {
	case DPTX_APCALL_ACTIVATE:
	case DPTX_APCALL_DEACTIVATE:
	case DPTX_APCALL_WILL_CHANGE_LINKG_CONFIG:
	case DPTX_APCALL_DID_CHANGE_LINK_CONFIG:
		return 0;
	case DPTX_APCALL_GET_MAX_LINK_RATE:
		return dptxport_call_get_max_link_rate(service, reply,
						       reply_size);
	case DPTX_APCALL_GET_LINK_RATE:
		return dptxport_call_get_link_rate(service, reply, reply_size);
	case DPTX_APCALL_SET_LINK_RATE:
		return dptxport_call_set_link_rate(service, data, data_size,
						   reply, reply_size);
	case DPTX_APCALL_GET_SUPPORTS_HPD:
		return dptxport_call_get_supports_hpd(service, reply,
						      reply_size);
	case DPTX_APCALL_GET_SUPPORTS_DOWN_SPREAD:
		return dptxport_call_get_supports_downspread(service, reply,
							     reply_size);
	default:
		/* just try to ACK and hope for the best... */
		dev_err(service->ep->dcp->dev, "DPTXPort: unhandled call %d\n",
			idx);
		memcpy(reply, data, min(reply_size, data_size));
		if (reply_size > 4)
			memset(reply, 0, 4);
		return 0;
	}
}

struct dcptx_hack_work {
	struct apple_dcp *dcp;
	struct work_struct work;
};

static void hack_work(struct work_struct *work_)
{
	struct dcptx_hack_work *work =
		container_of(work_, struct dcptx_hack_work, work);

	dptxport_validate_connection(work->dcp->dptxport[0], 0, 1, 0);
	dptxport_connect(work->dcp->dptxport[0], 0, 1, 0);
	dptxport_request_display(work->dcp->dptxport[0]);
	dptxport_do_hotplug(work->dcp->dptxport[0]);

	kfree(work);
}

static void dptxport_init(struct apple_epic_service *service, u8 *props,
			  size_t props_size)
{
	s64 unit;
	const char *name, *class;
	struct dcp_parse_ctx ctx;
	int ret;

	ret = parse(props, props_size, &ctx);
	if (ret) {
		dev_err(service->ep->dcp->dev,
			"DPTXPort: failed to parse init props: %d\n", ret);
		return;
	}
	ret = parse_epic_service_init(&ctx, &name, &class, &unit);
	if (ret) {
		dev_err(service->ep->dcp->dev,
			"DPTXPort: failed to extract init props: %d\n", ret);
		return;
	}

	if (strcmp(name, "dcpdptx-port-epic"))
		return;
	if (strcmp(class, "AppleDCPDPTXRemotePort"))
		return;
	kfree(name);
	kfree(class);

	trace_dptxport_init(service->ep->dcp, unit);

	switch (unit) {
	case 0:
	case 1:
		if (service->ep->dcp->dptxport[unit]) {
			dev_err(service->ep->dcp->dev,
				"DPTXPort: unit %lld already exists\n", unit);
			return;
		}
		service->cookie = (void *)unit;
		service->ep->dcp->dptxport[unit] = service;
		if (unit == 0) {
			struct dcptx_hack_work *w =
				kzalloc(sizeof(*w), GFP_KERNEL);
			w->dcp = service->ep->dcp;
			INIT_WORK(&w->work, hack_work);
			schedule_work(&w->work);
		}
		break;
	default:
		dev_err(service->ep->dcp->dev, "DPTXPort: invalid unit %lld\n",
			unit);
	}
}

static const struct apple_epic_service_ops dptxep_ops[] = {
	{
		.name = "AppleDCPDPTXRemotePort",
		.init = dptxport_init,
		.call = dptxport_call,
	},
	{}
};

int dptxep_init(struct apple_dcp *dcp)
{
	dcp->dptxep = afk_init(dcp, DPTX_ENDPOINT, dptxep_ops);
	afk_start(dcp->dptxep);

	return 0;
}
