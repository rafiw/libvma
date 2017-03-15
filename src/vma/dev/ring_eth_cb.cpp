/*
 * Copyright (c) 2001-2017 Mellanox Technologies, Ltd. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <dev/ring_eth_cb.h>
#include <dev/qp_mgr_mp.h>
#include <dev/cq_mgr_mp.h>

#undef  MODULE_NAME
#define MODULE_NAME		"ring_eth_cb"
#undef  MODULE_HDR
#define MODULE_HDR		MODULE_NAME "%d:%s() "


#ifndef DEFINED_IBV_OLD_VERBS_MLX_OFED

ring_eth_cb::ring_eth_cb(in_addr_t local_if,
			 ring_resource_creation_info_t *p_ring_info, int count,
			 bool active, uint16_t vlan, uint32_t mtu,
			 ring *parent) throw (vma_error) :
			 ring_eth(local_if, p_ring_info, count, active, vlan,
				  mtu, parent, false),
			 m_strides_num(16), m_stride_size(11), m_res_domain(NULL),
			 m_wq_count(2), m_curr_wq(0), m_curr_d_addr(NULL),
			 m_curr_h_ptr(NULL)
{
	// call function from derived not base
	m_is_mp_ring = true;
	m_buffer_size = (1 << m_stride_size) * (1 << m_strides_num) * m_wq_count + MCE_ALIGNMENT;
	memset(&m_curr_hw_timestamp, 0, sizeof(m_curr_hw_timestamp));
	create_resources(p_ring_info, active);
}

void ring_eth_cb::create_resources(ring_resource_creation_info_t *p_ring_info,
				   bool active) throw (vma_error)
{
	struct ibv_exp_res_domain_init_attr res_domain_attr;

	// check MP capabilities currently all caps are 0 due to a buf
	vma_ibv_device_attr& r_ibv_dev_attr =
			p_ring_info->p_ib_ctx->get_ibv_device_attr();
	if (!r_ibv_dev_attr.max_ctx_res_domain) {
		throw_vma_exception("device doesn't support resource domain");
		return;
	}
	if (!(r_ibv_dev_attr.mp_rq_caps.supported_qps & IBV_EXP_QPT_RAW_PACKET)) {
		throw_vma_exception("device doesn't support RC QP");
	}

	if (m_stride_size <
		r_ibv_dev_attr.mp_rq_caps.min_single_stride_log_num_of_bytes) {
		ring_logwarn("stride byte size is to low, supported %d, given %d",
			     r_ibv_dev_attr.mp_rq_caps.min_single_stride_log_num_of_bytes,
			     m_stride_size);
		throw_vma_exception("stride byte size is to low");
	}
	if (m_stride_size >
		r_ibv_dev_attr.mp_rq_caps.max_single_stride_log_num_of_bytes) {
		ring_logwarn("stride byte size is to high, supported %d, given %d",
			     r_ibv_dev_attr.mp_rq_caps.min_single_stride_log_num_of_bytes,
			     m_stride_size);
		throw_vma_exception("stride byte size is to high");
	}
	if (m_strides_num <
		r_ibv_dev_attr.mp_rq_caps.min_single_wqe_log_num_of_strides) {
		ring_logwarn("strides num is to low, supported %d, given %d",
			     r_ibv_dev_attr.mp_rq_caps.min_single_wqe_log_num_of_strides,
			     m_strides_num);
		throw_vma_exception("strides num is to low");
	}
	if (m_strides_num >
		r_ibv_dev_attr.mp_rq_caps.max_single_wqe_log_num_of_strides) {
		ring_logwarn("strides num is to high, supported %d, given %d",
			     r_ibv_dev_attr.mp_rq_caps.min_single_wqe_log_num_of_strides,
			     m_strides_num);
		throw_vma_exception("strides num is to high");
	}

	res_domain_attr.comp_mask = IBV_EXP_RES_DOMAIN_THREAD_MODEL |
				    IBV_EXP_RES_DOMAIN_MSG_MODEL;

	// driver is in charge of locks
	res_domain_attr.thread_model = IBV_EXP_THREAD_SAFE;

	// currently have no affect
	res_domain_attr.msg_model = IBV_EXP_MSG_HIGH_BW;

	m_res_domain = ibv_exp_create_res_domain(
				p_ring_info->p_ib_ctx->get_ibv_context(),
				&res_domain_attr);
	if (!m_res_domain) {
		throw_vma_exception("failed creating resource domain");
		return;
	}
	// create cyclic buffer get exception on failure
	alloc.alloc_and_reg_mr(m_buffer_size, p_ring_info->p_ib_ctx) ;
	// point m_sge to buffer
	int strides_num = 1 << m_strides_num;
	int strides_length = 1 << m_stride_size;
	ring_logdbg("strides num is %d stride size is %d", strides_num,
		    strides_length);
	// RAFI need to change to nice logic check
	assert(uint32_t(strides_num * strides_length * m_wq_count) < m_buffer_size);
	// create ring simple resources
	ring_simple::create_resources(p_ring_info, active);
	// some detect them as unused
	NOT_IN_USE(strides_num);
	NOT_IN_USE(strides_length);
}

qp_mgr* ring_eth_cb::create_qp_mgr(const ib_ctx_handler *ib_ctx,
				   uint8_t port_num,
				   struct ibv_comp_channel *p_rx_comp_event_channel) throw (vma_error)
{
	return new qp_mgr_mp(this, ib_ctx, port_num, p_rx_comp_event_channel,
			get_tx_num_wr(), get_partition());
}

int ring_eth_cb::drain_and_proccess(cq_type_t cq_type)
{
	NOT_IN_USE(cq_type);
	return 0;
}

int ring_eth_cb::poll_and_process_element_rx(uint64_t* p_cq_poll_sn,
					     void* pv_fd_ready_array)
{
	NOT_IN_USE(p_cq_poll_sn);
	NOT_IN_USE(pv_fd_ready_array);
	return 0;
}

int ring_eth_cb::cyclic_buffer_read(vma_completion_mp_t &completion,
				    size_t min, size_t max, int &flags)
{
	uint32_t offset = 0, p_flags = 0;
	int size = 0;

	// sanity check
	if (unlikely(min > max || max == 0 || flags != MSG_DONTWAIT)) {
		errno = EINVAL;
		ring_logdbg("Illegal values, got min: %d, max: %d, flags %d",
			    min, max, flags);
		if (flags != MSG_DONTWAIT) {
			ring_logdbg("only %d flag is currently supported",
				    MSG_DONTWAIT);
		}
		return -1;
	}

	int ret = ((cq_mgr_mp *)m_p_cq_mgr_rx)->poll_mp_cq(size, offset, p_flags);
	if (unlikely(ret == -1)) {
		ring_logdbg("poll_mp_cq failed with errno %m", errno);
		return -1;
	}
	// no message currently no way to distinguish if FILLER
	if (size == 0) {
		return 0;
	}
	// set it here because we might not have min packets avail in this run
	if (unlikely(m_curr_d_addr == 0)) {
		m_curr_d_addr = (void *)(m_p_qp_mgr->get_rx_sge()[m_curr_wq].addr + offset);
//		m_curr_hw_timestamp = RAFI SET IT HERE
		// When UMR will be added this will be different
		m_curr_h_ptr = m_curr_d_addr;
		m_curr_packets = 1;
		m_curr_size = size;
	} else {
		m_curr_packets++;
		m_curr_size += size;
	}

	if (unlikely(p_flags & IBV_EXP_CQ_RX_MULTI_PACKET_LAST_V1)) {
		reload_wq();
	} else {
		ret = mp_loop(min);
		if (ret == 1) { // there might be more to drain
			mp_loop(max);
		} else if (ret == 0) { // no packets left
			return 0;
		}
	}

	completion.payload_ptr = m_curr_d_addr;
	completion.payload_length = m_curr_size;
	completion.packets = m_curr_packets;
	if (completion.comp_mask & VMA_MP_MASK_HDR_PTR) {
		completion.headers_ptr = m_curr_h_ptr;
		completion.headers_ptr_length = m_curr_size;
	}
	if (completion.comp_mask & VMA_MP_MASK_TIMESTAMP) {
		completion.hw_timestamp = m_curr_hw_timestamp;
	}
	m_curr_d_addr = 0;
	ring_logdbg("Returning completion, buffer ptr %p, data size %zd, "
		    "number of packets %zd WQ index %d",
		    completion.payload_ptr, m_curr_size, m_curr_packets,
		    m_curr_wq);
	return 0;
}

/**
 * loop poll_cq
 * @param limit
 * @return TBD about -1 on error,
 * 	0 if cq is empty
 * 	1 if done looping
 * 	2 if WQ was reloaded
 */
inline int ring_eth_cb::mp_loop(size_t limit)
{
	uint32_t offset = 0, flags = 0;
	int size = 0;
	int ret;
	while (m_curr_packets < limit) {
		ret = ((cq_mgr_mp *)m_p_cq_mgr_rx)->poll_mp_cq(size, offset, flags);
		if (size == 0) {
			ring_logfine("no packet found");
			return 0;
		}
		m_curr_size += size;
		++m_curr_packets;
		if (flags & IBV_EXP_CQ_RX_MULTI_PACKET_LAST_V1) {
			reload_wq();
			return 2;
		}
		if (unlikely(ret == -1)) {
			// RAFI not sure if we should silent this and return 0 and ++m_curr_packets
			return 0;
		}
	}
	ring_logfine("mp_loop finished all iterations");
	return 1;
}

inline void ring_eth_cb::reload_wq()
{
	((qp_mgr_mp *)m_p_qp_mgr)->post_recv(m_curr_wq, 1);
	m_curr_wq = (m_curr_wq + 1) % m_wq_count;
}

ring_eth_cb::~ring_eth_cb()
{
	struct ibv_exp_destroy_res_domain_attr attr;

	memset(&attr, 0, sizeof(attr));
	int res = ibv_exp_destroy_res_domain(
			m_p_qp_mgr->get_ib_ctx_handler()->get_ibv_context(),
			m_res_domain,
			&attr);
	if (res)
		ring_logdbg("call to ibv_exp_destroy_res_domain returned %d", res);
	m_lock_ring_rx.lock();
	flow_udp_uc_del_all();
	flow_udp_mc_del_all();
	flow_tcp_del_all();
	m_lock_ring_rx.unlock();

	// explicitly destroy the qp and cq before this destructor finshes
	// since it will release the memory allocated
	delete m_p_qp_mgr;
	m_p_qp_mgr = NULL;

}
#endif

