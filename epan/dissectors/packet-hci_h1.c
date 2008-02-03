/* packet-hci_h1.c
 * Routines for the Bluetooth HCI h1 dissection
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <epan/packet.h>

#include "packet-hci_h1.h"


static int proto_hci_h1 = -1;
static int hf_hci_h1_type = -1;
static int hf_hci_h1_direction = -1;

static gint ett_hci_h1 = -1;

static dissector_table_t hci_h1_table;
static dissector_handle_t data_handle;


static const value_string hci_h1_type_vals[] = {
	{BTHCI_CHANNEL_COMMAND, "HCI Command"},
	{BTHCI_CHANNEL_ACL, "ACL Data"},
	{BTHCI_CHANNEL_SCO, "SCO Data"},
	{BTHCI_CHANNEL_EVENT, "HCI Event"},
	{0, NULL }
};
static const value_string hci_h1_direction_vals[] = {
	{0,	"Sent"},
	{1,	"Rcvd"},
	{0, NULL}
};

static void
dissect_hci_h1(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	guint8 type;
	tvbuff_t *next_tvb;
	proto_item *ti=NULL;
	proto_tree *hci_h1_tree=NULL;

	if(check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "HCI");

	if(check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	type = pinfo->pseudo_header->bthci.channel;

	if(tree){
		ti = proto_tree_add_item(tree, proto_hci_h1, tvb, 0, 1, FALSE);
		hci_h1_tree = proto_item_add_subtree(ti, ett_hci_h1);
	}

	if(check_col(pinfo->cinfo, COL_INFO)){
		col_add_fstr(pinfo->cinfo, COL_INFO, "%s %s",pinfo->p2p_dir==P2P_DIR_SENT?"Sent":"Rcvd",val_to_str(type, hci_h1_type_vals, "Unknown 0x%02x"));
	}
	ti=proto_tree_add_uint(hci_h1_tree, hf_hci_h1_direction, tvb, 0, 0, pinfo->p2p_dir);
	PROTO_ITEM_SET_GENERATED(ti);
	proto_item_append_text(hci_h1_tree, " %s %s", val_to_str(pinfo->p2p_dir, hci_h1_direction_vals, "0x%02x"), val_to_str(type, hci_h1_type_vals, "Unknown 0x%02x"));

	next_tvb = tvb_new_subset(tvb, 0, -1, -1);
	if(!dissector_try_port(hci_h1_table, type, next_tvb, pinfo, tree)) {
		call_dissector(data_handle, next_tvb, pinfo, tree);
	}
}


void
proto_register_hci_h1(void)
{
	static hf_register_info hf[] = {
	{ &hf_hci_h1_type,
		{ "HCI Packet Type",           "hci_h1.type",
		FT_UINT8, BASE_HEX, VALS(hci_h1_type_vals), 0x0,
		"HCI Packet Type", HFILL }},

	{ &hf_hci_h1_direction,
		{ "Direction",           "hci_h1.direction",
		FT_UINT8, BASE_HEX, VALS(hci_h1_direction_vals), 0x0,
		"HCI Packet Direction Sent/Rcvd", HFILL }},

	};

	static gint *ett[] = {
		&ett_hci_h1,
	};

	proto_hci_h1 = proto_register_protocol("Bluetooth HCI",
	    "HCI_H1", "hci_h1");

	register_dissector("hci_h1", dissect_hci_h1, proto_hci_h1);

	proto_register_field_array(proto_hci_h1, hf, array_length(hf));
	proto_register_subtree_array(ett, array_length(ett));

	hci_h1_table = register_dissector_table("hci_h1.type",
		"HCI h1 pdu type", FT_UINT8, BASE_HEX);
}

void
proto_reg_handoff_hci_h1(void)
{
	dissector_handle_t hci_h1_handle;

	data_handle = find_dissector("data");
	hci_h1_handle = find_dissector("hci_h1");
	dissector_add("wtap_encap", WTAP_ENCAP_BLUETOOTH_HCI, hci_h1_handle);
}


