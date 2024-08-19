/*
 * hostapd / WMM (Wi-Fi Multimedia)
 * Copyright 2002-2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#ifdef CONFIG_AP_WMM

#include "utils/supp_common.h"

#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "hostapd.h"
#include "ieee802_11.h"
#include "sta_info.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "wmm.h"

/* TODO: maintain separate sequence and fragment numbers for each AC
 * TODO: IGMP snooping to track which multicasts to forward - and use QOS-DATA
 * if only WMM stations are receiving a certain group */


static inline u8 wmm_aci_aifsn(int aifsn, int acm, int aci)
{
	u8 ret;
	ret = (aifsn << WMM_AC_AIFNS_SHIFT) & WMM_AC_AIFSN_MASK;
	if (acm)
		ret |= WMM_AC_ACM;
	ret |= (aci << WMM_AC_ACI_SHIFT) & WMM_AC_ACI_MASK;
	return ret;
}


static inline u8 wmm_ecw(int ecwmin, int ecwmax)
{
	return ((ecwmin << WMM_AC_ECWMIN_SHIFT) & WMM_AC_ECWMIN_MASK) |
		((ecwmax << WMM_AC_ECWMAX_SHIFT) & WMM_AC_ECWMAX_MASK);
}


/*
 * Add WMM Parameter Element to Beacon, Probe Response, and (Re)Association
 * Response frames.
 */
u8 * hostapd_eid_wmm(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
	struct wmm_parameter_element *wmm =
		(struct wmm_parameter_element *) (pos + 2);
	struct wmm_ac_parameter ac[4];
	int e;

	if (!hapd->conf->wmm_enabled)
		return eid;
	eid[0] = WLAN_EID_VENDOR_SPECIFIC;
	wmm->oui[0] = 0x00;
	wmm->oui[1] = 0x50;
	wmm->oui[2] = 0xf2;
	wmm->oui_type = WMM_OUI_TYPE;
	wmm->oui_subtype = WMM_OUI_SUBTYPE_PARAMETER_ELEMENT;
	wmm->version = WMM_VERSION;
	wmm->qos_info = hapd->parameter_set_count & 0xf;

	if (hapd->conf->wmm_uapsd &&
	    (hapd->iface->drv_flags & WPA_DRIVER_FLAGS_AP_UAPSD))
		wmm->qos_info |= 0x80;

	wmm->reserved = 0;

	/* fill in a parameter set record for each AC */
	for (e = 0; e < 4; e++) {
		struct hostapd_wmm_ac_params *acp =
			&hapd->iconf->wmm_ac_params[e];

		ac[e].aci_aifsn = wmm_aci_aifsn(acp->aifs,
					      acp->admission_control_mandatory,
					      e);
		ac[e].cw = wmm_ecw(acp->cwmin, acp->cwmax);
		ac[e].txop_limit = host_to_le16(acp->txop_limit);
	}	
	os_memcpy(wmm->ac, ac, sizeof(struct wmm_ac_parameter) * 4);

	pos = (u8 *) (wmm + 1);
	eid[1] = pos - eid - 2; /* element length */

	return pos;
}


/*
 * This function is called when a station sends an association request with
 * WMM info element. The function returns 1 on success or 0 on any error in WMM
 * element. eid does not include Element ID and Length octets.
 */
int hostapd_eid_wmm_valid(struct hostapd_data *hapd, const u8 *eid, size_t len)
{
	struct wmm_information_element *wmm;

	da16x_dump("WMM IE", eid, len);

	if (len < sizeof(struct wmm_information_element)) {
		da16x_ap_wmm_prt("[%s] Too short WMM IE (len=%lu)\n",
			   __func__, (unsigned long) len);
		return 0;
	}

	wmm = (struct wmm_information_element *) eid;

	da16x_ap_wmm_prt("[%s] Validating WMM IE: OUI %02x:%02x:%02x  "
		   "OUI type %d  OUI sub-type %d  version %d  QoS info 0x%x\n",
		__func__,
		wmm->oui[0], wmm->oui[1], wmm->oui[2], wmm->oui_type,
		wmm->oui_subtype, wmm->version, wmm->qos_info);

	if (wmm->oui_subtype != WMM_OUI_SUBTYPE_INFORMATION_ELEMENT ||
	    wmm->version != WMM_VERSION) {
		da16x_ap_wmm_prt("[%s] Unsupported WMM IE Subtype/Version\n",
				__func__);
		return 0;
	}

	return 1;
}

#if 0	/* by Shingu */
static void wmm_send_action(struct hostapd_data *hapd, const u8 *addr,
			    const struct wmm_tspec_element *tspec,
			    u8 action_code, u8 dialogue_token, u8 status_code)
{
	u8 buf[256];
	struct _ieee80211_mgmt *m = (struct _ieee80211_mgmt *) buf;
	struct wmm_tspec_element *t = (struct wmm_tspec_element *)
		m->u.action.u.wmm_action.variable;
	int len;

	da16x_ap_wmm_prt("[%s] action response - reason %d\n",
				__func__, status_code);

	os_memset(buf, 0, sizeof(buf));
	m->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					WLAN_FC_STYPE_ACTION);
	os_memcpy(m->da, addr, ETH_ALEN);
	os_memcpy(m->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(m->bssid, hapd->own_addr, ETH_ALEN);
	m->u.action.category = WLAN_ACTION_WMM;
	m->u.action.u.wmm_action.action_code = action_code;
	m->u.action.u.wmm_action.dialog_token = dialogue_token;
	m->u.action.u.wmm_action.status_code = status_code;
	os_memcpy(t, tspec, sizeof(struct wmm_tspec_element));
	len = ((u8 *) (t + 1)) - buf;

	if (hostapd_drv_send_mlme(hapd, m, len, 0) < 0)
		da16x_ap_wmm_prt("[%s] wmm_send_action: send failed\n",
				__func__);
}
#endif

int wmm_process_tspec(struct wmm_tspec_element *tspec)
{
	int medium_time, pps, duration;
	u16 val, surplus;
#ifdef ENABLE_AP_WMM_DBG
	int up, psb, dir, tid;

	up = (tspec->ts_info[1] >> 3) & 0x07;
	psb = (tspec->ts_info[1] >> 2) & 0x01;
	dir = (tspec->ts_info[0] >> 5) & 0x03;
	tid = (tspec->ts_info[0] >> 1) & 0x0f;
#endif /* ENABLE_AP_WMM_DBG */
	
	da16x_ap_wmm_prt("[%s] WMM: TS Info: UP=%d PSB=%d Direction=%d TID=%d\n",
		__func__, up, psb, dir, tid);

	val = le_to_host16(tspec->nominal_msdu_size);

	da16x_ap_wmm_prt("[%s] WMM: Nominal MSDU Size: %d%s\n",
			__func__, val & 0x7fff, val & 0x8000 ? " (fixed)" : "");
	da16x_ap_wmm_prt("[%s] WMM: Mean Data Rate: %u bps\n",
			__func__, le_to_host32(tspec->mean_data_rate));
	da16x_ap_wmm_prt("[%s] WMM: Minimum PHY Rate: %u bps\n",
			__func__, le_to_host32(tspec->minimum_phy_rate));

	val = le_to_host16(tspec->surplus_bandwidth_allowance);
	da16x_ap_wmm_prt("[%s] WMM: Surplus Bandwidth Allowance: %u.%04u\n",
			__func__, val >> 13, 10000 * (val & 0x1fff) / 0x2000);

	val = le_to_host16(tspec->nominal_msdu_size);
	if (val == 0) {
		da16x_ap_wmm_prt("[%s] WMM: Invalid Nominal MSDU Size (0)\n",
				__func__);

		return WMM_ADDTS_STATUS_INVALID_PARAMETERS;
	}
	/* pps = Ceiling((Mean Data Rate / 8) / Nominal MSDU Size) */
	pps = ((le_to_host32(tspec->mean_data_rate) / 8) + val - 1) / val;
	da16x_ap_wmm_prt("[%s] WMM: Packets-per-second estimate for TSPEC: %d\n",
			__func__, pps);

	if (le_to_host32(tspec->minimum_phy_rate) < 1000000) {
		da16x_ap_wmm_prt("[%s] WMM: Too small Minimum PHY Rate\n",
				__func__);

		return WMM_ADDTS_STATUS_INVALID_PARAMETERS;
	}

	duration = (le_to_host16(tspec->nominal_msdu_size) & 0x7fff) * 8 /
		(le_to_host32(tspec->minimum_phy_rate) / 1000000) +
		50 /* FIX: proper SIFS + ACK duration */;

	/* unsigned binary number with an implicit binary point after the
	 * leftmost 3 bits, i.e., 0x2000 = 1.0 */
	surplus = le_to_host16(tspec->surplus_bandwidth_allowance);
	if (surplus <= 0x2000) {
		da16x_ap_wmm_prt("[%s] WMM: Surplus Bandwidth Allowance not "
			   "greater than unity\n", __func__);
		return WMM_ADDTS_STATUS_INVALID_PARAMETERS;
	}

	medium_time = surplus * pps * duration / 0x2000;
	da16x_ap_wmm_prt("[%s] WMM: Estimated medium time: %u\n",
			__func__, medium_time);

	/*
	 * TODO: store list of granted (and still active) TSPECs and check
	 * whether there is available medium time for this request. For now,
	 * just refuse requests that would by themselves take very large
	 * portion of the available bandwidth.
	 */
	if (medium_time > 750000) {
		da16x_ap_wmm_prt("[%s] WMM: Refuse TSPEC request for over "
			   "75%% of available bandwidth\n", __func__);
		return WMM_ADDTS_STATUS_REFUSED;
	}

	/* Convert to 32 microseconds per second unit */
	tspec->medium_time = host_to_le16(medium_time / 32);

	return WMM_ADDTS_STATUS_ADMISSION_ACCEPTED;
}

#if 0	/* by Shingu */
static void wmm_addts_req(struct hostapd_data *hapd,
			  const struct _ieee80211_mgmt *mgmt,
			  struct wmm_tspec_element *tspec, size_t len)
{
	const u8 *end = ((const u8 *) mgmt) + len;
	int res;

	if ((const u8 *) (tspec + 1) > end) {
		da16x_ap_wmm_prt("[%s] WMM: TSPEC overflow in ADDTS Request\n",
				__func__);
		return;
	}

	da16x_ap_wmm_prt("[%s] WMM: ADDTS Request (Dialog Token %d) for TSPEC "
		   "from " MACSTR "\n",
		   __func__,
		   mgmt->u.action.u.wmm_action.dialog_token,
		   MAC2STR(mgmt->sa));

	res = wmm_process_tspec(tspec);
	da16x_ap_wmm_prt("[%s] WMM: ADDTS processing result: %d\n",
			__func__, res);

	wmm_send_action(hapd, mgmt->sa, tspec, WMM_ACTION_CODE_ADDTS_RESP,
			mgmt->u.action.u.wmm_action.dialog_token, res);
}
#endif

void hostapd_wmm_action(struct hostapd_data *hapd,
			const struct _ieee80211_mgmt *mgmt, size_t len)
{
#ifdef ENABLE_AP_WMM_DBG
	int action_code;
#endif /* ENABLE_AP_WMM_DBG */
#if 0	/* by Bright */
	int left = len - IEEE80211_HDRLEN - 4;
	const u8 *pos = ((const u8 *) mgmt) + IEEE80211_HDRLEN + 4;
	struct ieee802_11_elems elems;
#endif	/* 0 */
	struct sta_info *sta = ap_get_sta(hapd, mgmt->sa);

	/* check that the request comes from a valid station */
	if (!sta ||
	    (sta->flags & (WLAN_STA_ASSOC | WLAN_STA_WMM)) !=
	    (WLAN_STA_ASSOC | WLAN_STA_WMM)) {
		da16x_ap_wmm_prt("[%s] wmm action received is not from "
				"associated wmm station\n", __func__);
		/* TODO: respond with action frame refused status code */
		return;
	}

#if 0	/* by Bright */
	/* extract the tspec info element */
	if (ieee802_11_parse_elements(pos, left, &elems, 1) == ParseFailed) {
		da16x_ap_wmm_prt("[%s] hostapd_wmm_action - could not "
				"parse wmm action\n", __func__);
		/* TODO: respond with action frame invalid parameters status
		 * code */
		return;
	}

	if (!elems.wmm_tspec ||
	    elems.wmm_tspec_len != (sizeof(struct wmm_tspec_element) - 2)) {
		da16x_ap_wmm_prt("[%s] hostapd_wmm_action - missing or "
				"wrong length tspec\n", __func__);
		/* TODO: respond with action frame invalid parameters status
		 * code */
		return;
	}
#else
da16x_ap_wmm_prt("[%s] What do I do for ieee802_11_parse_elements() in umac ???\n",
			__func__);
#endif	/* 0 */

	/* TODO: check the request is for an AC with ACM set, if not, refuse
	 * request */
	
#ifdef ENABLE_AP_WMM_DBG
	action_code = mgmt->u.action.u.wmm_action.action_code;
#endif /* ENABLE_AP_WMM_DBG */
	
#if 0	/* by Shingu */
	switch (action_code) {
	case WMM_ACTION_CODE_ADDTS_REQ:
		wmm_addts_req(hapd, mgmt, (struct wmm_tspec_element *)
			      (elems.wmm_tspec - 2), len);
		return;
	/* TODO: needed for client implementation */
	case WMM_ACTION_CODE_ADDTS_RESP:
		wmm_setup_request(hapd, mgmt, len);
		return;
	/* TODO: handle station teardown requests */
	case WMM_ACTION_CODE_DELTS:
		wmm_teardown(hapd, mgmt, len);
		return;
	}
#endif

	da16x_ap_wmm_prt("[%s] hostapd_wmm_action - unknown action code %d\n",
		       __func__, action_code);
}
#endif /* CONFIG_AP_WMM */

/* EOF */
