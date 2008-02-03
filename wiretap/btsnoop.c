/* btsnoop.c
 *
 * $Id$
 *
 * Wiretap Library
 * Copyright (c) 1998 by Gilbert Ramirez <gram@alumni.rice.edu>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <string.h>
#include "wtap-int.h"
#include "file_wrappers.h"
#include "buffer.h"
#include "atm.h"
#include "btsnoop.h"
/* See RFC 1761 for a description of the "snoop" file format. */

/* Magic number in "btsnoop" files. */
static const char btsnoop_magic[] = {
	'b', 't', 's', 'n', 'o', 'o', 'p', '\0'
};

/* "snoop" file header (minus magic number). */
struct btsnoop_hdr {
	guint32	version;	/* version number (should be 1) */
	guint32	datalink;	/* datalink type */
};

/* "snoop" record header. */
struct btsnooprec_hdr {
	guint32	orig_len;	/* actual length of packet */
	guint32	incl_len;	/* number of octets captured in file */
	guint32	flags;	/* packet flags */
	guint32	cum_drops;	/* cumulative number of dropped packets */
	gint64	ts_usec;	/* timestamp microseconds */
};

/* H1 is unframed data with the packet type encoded in the flags field of capture header */
/* It can be used for any datalink by placing logging above the datalink layer of HCI */
#define KHciLoggerDatalinkTypeH1		1001
/* H4 is the serial HCI with packet type encoded in the first byte of each packet */
#define KHciLoggerDatalinkTypeH4		1002
/* CSR's PPP derived bluecore serial protocol - in practice we log in H1 format after deframing */
#define KHciLoggerDatalinkTypeBCSP		1003
/* H5 is the official three wire serial protocol derived from BCSP*/
#define KHciLoggerDatalinkTypeH5		1004

#define KHciLoggerHostToController		0
#define KHciLoggerControllerToHost		0x00000001
#define KHciLoggerACLDataFrame			0
#define KHciLoggerCommandOrEvent		0x00000002

const gint64 KUnixTimeBase = G_GINT64_CONSTANT(0x00dcddb30f2f8000); /* offset from symbian - unix time */

static gboolean btsnoop_read(wtap *wth, int *err, gchar **err_info,
    gint64 *data_offset);
static gboolean btsnoop_seek_read(wtap *wth, gint64 seek_off,
    union wtap_pseudo_header *pseudo_header, guchar *pd, int length,
    int *err, gchar **err_info);
static gboolean snoop_read_rec_data(FILE_T fh, guchar *pd, int length, int *err);

int btsnoop_open(wtap *wth, int *err, gchar **err_info _U_)
{
	int bytes_read;
	char magic[sizeof btsnoop_magic];
	struct btsnoop_hdr hdr;

	int file_encap=WTAP_ENCAP_UNKNOWN;

	/* Read in the string that should be at the start of a "snoop" file */
	errno = WTAP_ERR_CANT_READ;
	bytes_read = file_read(magic, 1, sizeof magic, wth->fh);
	if (bytes_read != sizeof magic) {
		*err = file_error(wth->fh);
		if (*err != 0)
			return -1;
		return 0;
	}
	wth->data_offset += sizeof magic;

	if (memcmp(magic, btsnoop_magic, sizeof btsnoop_magic) != 0) {
		return 0;
	}

	/* Read the rest of the header. */
	errno = WTAP_ERR_CANT_READ;
	bytes_read = file_read(&hdr, 1, sizeof hdr, wth->fh);
	if (bytes_read != sizeof hdr) {
		*err = file_error(wth->fh);
		if (*err != 0)
			return -1;
		return 0;
	}
	wth->data_offset += sizeof hdr;

	/*
	 * Make sure it's a version we support.
	 */
	hdr.version = g_ntohl(hdr.version);
	if (hdr.version != 1) {
		*err = WTAP_ERR_UNSUPPORTED;
		*err_info = g_strdup_printf("btsnoop: version %u unsupported", hdr.version);
		return -1;
	}

	hdr.datalink = g_ntohl(hdr.datalink);
	switch (hdr.datalink) {
	case KHciLoggerDatalinkTypeH1:
		file_encap=WTAP_ENCAP_BLUETOOTH_HCI;
		break;
	case KHciLoggerDatalinkTypeBCSP:
	case KHciLoggerDatalinkTypeH5:
		*err = WTAP_ERR_UNSUPPORTED;
		*err_info = g_strdup_printf("btsnoop: BCSP/H5 capture logs %u unsupported", hdr.version);
		return -1;
	case KHciLoggerDatalinkTypeH4:
		file_encap=WTAP_ENCAP_BLUETOOTH_H4;
		break;
	default:
		*err = WTAP_ERR_UNSUPPORTED;
		*err_info = g_strdup_printf("btsnoop: datalink %u unsupported", hdr.version);
		return -1;
	}

	wth->subtype_read = btsnoop_read;
	wth->subtype_seek_read = btsnoop_seek_read;
	wth->file_encap = file_encap;
	wth->snapshot_length = 0;	/* not available in header */
	wth->tsprecision = WTAP_FILE_TSPREC_USEC;
	wth->file_type = WTAP_FILE_BTSNOOP;
	return 1;
}

static gboolean btsnoop_read(wtap *wth, int *err, gchar **err_info,
    gint64 *data_offset)
{
	guint32 packet_size;
	guint32 flags;
	guint32 orig_size;
	int	bytes_read;
	struct btsnooprec_hdr hdr;
	gint64 ts;

	/* As the send/receive flag is stored in the middle of the capture header 
	but needs to go in the pseudo header for wiretap, the header needs to be reread
	in the seek_read function*/
	*data_offset = wth->data_offset;

	/* Read record header. */
	errno = WTAP_ERR_CANT_READ;
	bytes_read = file_read(&hdr, 1, sizeof hdr, wth->fh);
	if (bytes_read != sizeof hdr) {
		*err = file_error(wth->fh);
		if (*err == 0 && bytes_read != 0)
			*err = WTAP_ERR_SHORT_READ;
		return FALSE;
	}
	wth->data_offset += sizeof hdr;
	if(sizeof hdr!=24)
	{
		*err_info="wrong header size";
		return FALSE;
	}

	packet_size = g_ntohl(hdr.incl_len);
	orig_size = g_ntohl(hdr.orig_len);
	flags = g_ntohl(hdr.flags);
	if (packet_size > WTAP_MAX_PACKET_SIZE) {
		/*
		 * Probably a corrupt capture file; don't blow up trying
		 * to allocate space for an immensely-large packet.
		 */
		*err = WTAP_ERR_BAD_RECORD;
		*err_info = g_strdup_printf("snoop: File has %u-byte packet, bigger than maximum of %u",
		    packet_size, WTAP_MAX_PACKET_SIZE);
		return FALSE;
	}

	buffer_assure_space(wth->frame_buffer, packet_size);
	if (!snoop_read_rec_data(wth->fh, buffer_start_ptr(wth->frame_buffer),
		packet_size, err)) {
		return FALSE;	/* Read error */
	}
	wth->data_offset += packet_size;

	ts = GINT64_FROM_BE(hdr.ts_usec);
	ts -= KUnixTimeBase;

	wth->phdr.ts.secs = (guint)(ts / 1000000);
	wth->phdr.ts.nsecs = (guint)((ts % 1000000) * 1000);
	wth->phdr.caplen = packet_size;
	wth->phdr.len = orig_size;
	if(wth->file_encap == WTAP_ENCAP_BLUETOOTH_H4)
	{
		wth->pseudo_header.p2p.sent = (flags & KHciLoggerControllerToHost) ? FALSE : TRUE;
	}
	else if(wth->file_encap == WTAP_ENCAP_BLUETOOTH_HCI)
	{
		wth->pseudo_header.bthci.sent = (flags & KHciLoggerControllerToHost) ? FALSE : TRUE;
		if(flags & KHciLoggerCommandOrEvent)
		{
			if(wth->pseudo_header.bthci.sent)
			{
				wth->pseudo_header.bthci.channel = BTHCI_CHANNEL_COMMAND;
			}
			else
			{
				wth->pseudo_header.bthci.channel = BTHCI_CHANNEL_EVENT;
			}
		}
		else
		{
			wth->pseudo_header.bthci.channel = BTHCI_CHANNEL_ACL;
		}
	}
	return TRUE;
}

static gboolean btsnoop_seek_read(wtap *wth, gint64 seek_off,
    union wtap_pseudo_header *pseudo_header, guchar *pd, int length,
    int *err, gchar **err_info _U_) {
	int	bytes_read;
	struct btsnooprec_hdr hdr;
	guint32 flags;
	if (file_seek(wth->random_fh, seek_off, SEEK_SET, err) == -1)
		return FALSE;

	/* Read record header. */
	errno = WTAP_ERR_CANT_READ;
	bytes_read = file_read(&hdr, 1, sizeof hdr, wth->random_fh);
	if (bytes_read != sizeof hdr) {
		*err = file_error(wth->random_fh);
		if (*err == 0 && bytes_read != 0)
			*err = WTAP_ERR_SHORT_READ;
		return FALSE;
	}
	flags = g_ntohl(hdr.flags);

	/*
	 * Read the packet data.
	 */
	if (!snoop_read_rec_data(wth->random_fh, pd, length, err))
		return FALSE;	/* failed */

	if(wth->file_encap == WTAP_ENCAP_BLUETOOTH_H4)
	{
		pseudo_header->p2p.sent = (flags & KHciLoggerControllerToHost) ? FALSE : TRUE;
	}
	else if(wth->file_encap == WTAP_ENCAP_BLUETOOTH_HCI)
	{
		pseudo_header->bthci.sent = (flags & KHciLoggerControllerToHost) ? FALSE : TRUE;
		if(flags & KHciLoggerCommandOrEvent)
		{
			if(pseudo_header->bthci.sent)
			{
				pseudo_header->bthci.channel = BTHCI_CHANNEL_COMMAND;
			}
			else
			{
				pseudo_header->bthci.channel = BTHCI_CHANNEL_EVENT;
			}
		}
		else
		{
			pseudo_header->bthci.channel = BTHCI_CHANNEL_ACL;
		}
	}
	return TRUE;
}

static gboolean
snoop_read_rec_data(FILE_T fh, guchar *pd, int length, int *err)
{
	int	bytes_read;

	errno = WTAP_ERR_CANT_READ;
	bytes_read = file_read(pd, 1, length, fh);

	if (bytes_read != length) {
		*err = file_error(fh);
		if (*err == 0)
			*err = WTAP_ERR_SHORT_READ;
		return FALSE;
	}
	return TRUE;
}
