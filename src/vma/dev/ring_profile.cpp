/*
 * ring_profile.cpp
 *
 *  Created on: Mar 27, 2017
 *      Author: root
 */

#include <dev/ring_profile.h>

ring_profiles_collection *g_p_ring_profile = NULL;


ring_profile::ring_profile(struct vma_ring_type_attr *ring_desc) {
	memset(&m_ring_desc,0,sizeof(m_ring_desc));
	m_ring_desc.comp_mask = ring_desc->comp_mask;
	m_ring_desc.ring_type = ring_desc->ring_type;
	switch (ring_desc->ring_type) {
	case VMA_RING_CYCLIC_BUFFER: {
		vma_cyclic_buffer_ring_attr &r = m_ring_desc.ring_cyclicb;
		r.comp_mask = ring_desc->ring_cyclicb.comp_mask;
		r.num = ring_desc->ring_cyclicb.num;
		r.stride_bytes = ring_desc->ring_cyclicb.stride_bytes;
		if (r.comp_mask & CB_COMP_HDR_BYTE) {
			r.hdr_bytes = ring_desc->ring_cyclicb.hdr_bytes;
		}
		break;
	}
	case VMA_RING_PACKET:
		m_ring_desc.ring_pktq.comp_mask = ring_desc->ring_pktq.comp_mask;
		break;
	default:

		break;
	}
	create_string();
};

const char* ring_profile::get_vma_ring_type_str()
{
	switch (m_ring_desc.ring_type) {
	case VMA_RING_PACKET:	return "VMA_PKTS_RING";
	case VMA_RING_CYCLIC_BUFFER:	return "VMA_CB_RING";
	default:		return "";
	}
};

ring_profile::ring_profile()
{
	m_ring_desc.ring_type = VMA_RING_PACKET;
	m_ring_desc.comp_mask = VMA_RING_TYPE_MASK;
	m_ring_desc.ring_pktq.comp_mask = 0;
	create_string();
};


void ring_profile::create_string()
{
	ostringstream s;
	if (m_ring_desc.ring_type == VMA_RING_PACKET) {
		s<<get_vma_ring_type_str();
	} else {
		s<<get_vma_ring_type_str()
		 <<" packets_num:"<<m_ring_desc.ring_cyclicb.num
		 <<" stride_bytes:"<<m_ring_desc.ring_cyclicb.stride_bytes
		 <<" hdr size:"<<m_ring_desc.ring_cyclicb.hdr_bytes;
	}
}

ring_profiles_collection::ring_profiles_collection(): m_curr_idx(START_RING_INDEX) {

}

int ring_profiles_collection::add_profile(vma_ring_type_attr *profile)
{
	// key 0 is invalid
	uint64_t key = m_curr_idx;
	m_curr_idx++;
	ring_profile *prof = new ring_profile(profile);
	m_profs_map[key] = prof;
	return key;
}

ring_profile* ring_profiles_collection::get_profile(uint64_t key)
{
	ring_profile_map_t::iterator iter = m_profs_map.find(key);
	if (iter != m_profs_map.end()) {
		return iter->second;
	}
	return NULL;
}

ring_profiles_collection::~ring_profiles_collection()
{
	ring_profile_map_t::iterator iter = m_profs_map.begin();
	for (;iter != m_profs_map.end(); ++iter) {
		delete (iter->second);
	}
}
