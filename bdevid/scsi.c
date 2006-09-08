/*
 * Copyright 2006 Red Hat, Inc.
 * Copyright 2003 IBM Corp.
 *
 * Authors:
 *    Patrick Mansfield <patmans@us.ibm.com>
 *    Peter Jones <pjones@redhat.com>
 *
 *    This program is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by the
 *    Free Software Foundation version 2 of the License.
 */ 

#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <bdevid.h>

#include "scsi.h"
#include "scsi_id.h"
#include "scsi_id_version.h"

#define info(...)
#define dbg(...)

/*
 * A priority based list of id, naa, and binary/ascii for the identifier
 * descriptor in VPD page 0x83.
 *
 * Brute force search for a match starting with the first value in the
 * following id_search_list. This is not a performance issue, since there
 * is normally one or some small number of descriptors.
 */
static const struct scsi_id_search_values id_search_list[] = {
    { SCSI_ID_NAA,    SCSI_ID_NAA_IEEE_REG_EXTENDED,    SCSI_ID_BINARY },
    { SCSI_ID_NAA,    SCSI_ID_NAA_IEEE_REG_EXTENDED,    SCSI_ID_ASCII },
    { SCSI_ID_NAA,    SCSI_ID_NAA_IEEE_REG,    SCSI_ID_BINARY },
    { SCSI_ID_NAA,    SCSI_ID_NAA_IEEE_REG,    SCSI_ID_ASCII },
    /*
     * Devices already exist using NAA values that are now marked
     * reserved. These should not conflict with other values, or it is
     * a bug in the device. As long as we find the IEEE extended one
     * first, we really don't care what other ones are used. Using
     * don't care here means that a device that returns multiple
     * non-IEEE descriptors in a random order will get different
     * names.
     */
    { SCSI_ID_NAA,    SCSI_ID_NAA_DONT_CARE,    SCSI_ID_BINARY },
    { SCSI_ID_NAA,    SCSI_ID_NAA_DONT_CARE,    SCSI_ID_ASCII },
    { SCSI_ID_EUI_64,    SCSI_ID_NAA_DONT_CARE,    SCSI_ID_BINARY },
    { SCSI_ID_EUI_64,    SCSI_ID_NAA_DONT_CARE,    SCSI_ID_ASCII },
    { SCSI_ID_T10_VENDOR,    SCSI_ID_NAA_DONT_CARE,    SCSI_ID_BINARY },
    { SCSI_ID_T10_VENDOR,    SCSI_ID_NAA_DONT_CARE,    SCSI_ID_ASCII },
    { SCSI_ID_VENDOR_SPECIFIC,    SCSI_ID_NAA_DONT_CARE,    SCSI_ID_BINARY },
    { SCSI_ID_VENDOR_SPECIFIC,    SCSI_ID_NAA_DONT_CARE,    SCSI_ID_ASCII },
};

static const char hex_str[]="0123456789abcdef";

/*
 * Values returned in the result/status, only the ones used by the code
 * are used here.
 */

#define DID_NO_CONNECT            0x01    /* Unable to connect before timeout */
#define DID_BUS_BUSY            0x02    /* Bus remain busy until timeout */
#define DID_TIME_OUT            0x03    /* Timed out for some other reason */
#define DRIVER_TIMEOUT            0x06
#define DRIVER_SENSE            0x08    /* Sense_buffer has been set */

/* The following "category" function returns one of the following */
#define SG_ERR_CAT_CLEAN        0    /* No errors or other information */
#define SG_ERR_CAT_MEDIA_CHANGED    1    /* interpreted from sense buffer */
#define SG_ERR_CAT_RESET        2    /* interpreted from sense buffer */
#define SG_ERR_CAT_TIMEOUT        3
#define SG_ERR_CAT_RECOVERED        4     /* Successful command after recovered err */
#define SG_ERR_CAT_NOTSUPPORTED     5    /* Illegal / unsupported command */
#define SG_ERR_CAT_SENSE        98    /* Something else in the sense buffer */
#define SG_ERR_CAT_OTHER        99    /* Some other error/warning */

static int sg_err_category_new(int scsi_status, int msg_status, int
                   host_status, int driver_status, const
                   unsigned char *sense_buffer, int sb_len)
{
    scsi_status &= 0x7e;

    /*
     * XXX change to return only two values - failed or OK.
     */

    /*
     * checks msg_status
     */
    if (!scsi_status && !msg_status && !host_status && !driver_status)
        return SG_ERR_CAT_CLEAN;

    if ((scsi_status == SCSI_CHECK_CONDITION) ||
        (scsi_status == SCSI_COMMAND_TERMINATED) ||
        ((driver_status & 0xf) == DRIVER_SENSE)) {
        if (sense_buffer && (sb_len > 2)) {
            int sense_key;
            unsigned char asc;

            if (sense_buffer[0] & 0x2) {
                sense_key = sense_buffer[1] & 0xf;
                asc = sense_buffer[2];
            } else {
                sense_key = sense_buffer[2] & 0xf;
                asc = (sb_len > 12) ? sense_buffer[12] : 0;
            }

            if (sense_key == RECOVERED_ERROR)
                return SG_ERR_CAT_RECOVERED;
            else if (sense_key == UNIT_ATTENTION) {
                if (0x28 == asc)
                    return SG_ERR_CAT_MEDIA_CHANGED;
                if (0x29 == asc)
                    return SG_ERR_CAT_RESET;
            } else if (sense_key == ILLEGAL_REQUEST) {
                return SG_ERR_CAT_NOTSUPPORTED;
            }
        }
        return SG_ERR_CAT_SENSE;
    }
    if (!host_status) {
        if ((host_status == DID_NO_CONNECT) ||
            (host_status == DID_BUS_BUSY) ||
            (host_status == DID_TIME_OUT))
            return SG_ERR_CAT_TIMEOUT;
    }
    if (!driver_status) {
        if (driver_status == DRIVER_TIMEOUT)
            return SG_ERR_CAT_TIMEOUT;
    }
    return SG_ERR_CAT_OTHER;
}

static int sg_err_category3(struct sg_io_hdr *hp)
{
    return sg_err_category_new(hp->status, hp->msg_status,
                   hp->host_status, hp->driver_status,
                   hp->sbp, hp->sb_len_wr);
}

static int scsi_inquiry(int fd, unsigned char evpd, unsigned char page,
            unsigned char *buf, size_t buflen)
{
    unsigned char inq_cmd[INQUIRY_CMDLEN] =
        { INQUIRY_CMD, evpd, page, 0, buflen, 0 };
    unsigned char sense[SENSE_BUFF_LEN];
    struct sg_io_hdr io_hdr;
    int retval;
    int retry = 3; /* rather random */

    if (buflen > SCSI_INQ_BUFF_LEN)
        buflen = SCSI_INQ_BUFF_LEN;

resend:
    dbg("%s evpd %d, page 0x%x\n", dev->kernel_name, evpd, page);

    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(inq_cmd);
    io_hdr.mx_sb_len = sizeof(sense);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = buflen;
    io_hdr.dxferp = buf;
    io_hdr.cmdp = inq_cmd;
    io_hdr.sbp = sense;
    io_hdr.timeout = DEF_TIMEOUT;

    if (ioctl(fd, SG_IO, &io_hdr) < 0) {
        info("%s: ioctl failed: %s", dev->kernel_name, strerror(errno));
        retval = -1;
        goto error;
    }

    retval = sg_err_category3(&io_hdr);

    switch (retval) {
        case SG_ERR_CAT_NOTSUPPORTED:
            buf[1] = 0;
            /* Fallthrough */
        case SG_ERR_CAT_CLEAN:
        case SG_ERR_CAT_RECOVERED:
            retval = 0;
            break;
    }

    if (!retval) {
        retval = buflen;
    } else if (retval > 0) {
        if (--retry > 0) {
            dbg("%s: Retrying ...\n", dev->kernel_name);
            goto resend;
        }
        retval = -1;
    }

error:
    if (retval < 0)
        info("%s: Unable to get INQUIRY vpd %d page 0x%x.",
            dev->kernel_name, evpd, page);

    return retval;
}

/* Get list of supported EVPD pages */
static int do_scsi_page0_inquiry(int fd, unsigned char *buffer, size_t len)
{
    int retval;
    unsigned char page0[SCSI_INQ_BUFF_LEN];

    memset(buffer, '\0', len);
    if ((retval = scsi_inquiry(fd, 1, 0x0, buffer, len) < 0))
        return retval;
    if (buffer[1] != 0 || buffer[3] > len)
        return -1;

    /* Some ill behaved devices return the standard inquiry instead of evpd,
     * so check against the normal data */

    memset(page0, '\0', SCSI_INQ_BUFF_LEN);
    if ((retval = scsi_inquiry(fd, 0, 0x0, page0, SCSI_INQ_BUFF_LEN) < 0))
        return retval;
    if (page0[3] > SCSI_INQ_BUFF_LEN)
        return -1;

    /* XXX ugh, this check sucks... */
    if (buffer[3] > MODEL_LENGTH) {
       if (!strncmp((char *)&buffer[8], (char *)&page0[8], VENDOR_LENGTH))
           return -1;
    }
    return 0;
}

/**
 * check_fill_0x83_id - check the page 0x83 id, if OK allocate and fill
 * serial number.
 **/
static int check_fill_0x83_id(unsigned char *page_83, 
    const struct scsi_id_search_values *id_search, char **serial,
    size_t *max_len)
{
    int i, j, len;

    /*
     * ASSOCIATION must be with the device (value 0)
     */
    if ((page_83[1] & 0x30) != 0)
        return 1;

    if ((page_83[1] & 0x0f) != id_search->id_type)
        return 1;

    /*
     * Possibly check NAA sub-type.
     */
    if ((id_search->naa_type != SCSI_ID_NAA_DONT_CARE) &&
            (id_search->naa_type != (page_83[4] & 0xf0) >> 4))
        return 1;

    /*
     * Check for matching code set - ASCII or BINARY.
     */
    if ((page_83[0] & 0x0f) != id_search->code_set)
        return 1;

    /*
     * page_83[3]: identifier length
     */
    len = page_83[3];
    if ((page_83[0] & 0x0f) != SCSI_ID_ASCII) {
        /*
         * If not ASCII, use two bytes for each binary value.
         */
        len *= 2;
    }

    /*
     * Add one byte for the NUL termination, and one for the id_type.
     */
    len += 2;
    if (*max_len < len) {
        char *s;

        if (!(s = realloc(*serial, len * sizeof(**serial))))
            return 1;
        *serial = s;
        *max_len = len;
    }

    (*serial)[0] = hex_str[id_search->id_type];

    /*
     * For SCSI_ID_VENDOR_SPECIFIC prepend the vendor and model before
     * the id since it is not unique across all vendors and models,
     * this differs from SCSI_ID_T10_VENDOR, where the vendor is
     * included in the identifier.
     */
    if (id_search->id_type == SCSI_ID_VENDOR_SPECIFIC)
        return 1;

    i = 4; /* offset to the start of the identifier */
    j = strlen(*serial);
    if ((page_83[0] & 0x0f) == SCSI_ID_ASCII) {
        /*
         * ASCII descriptor.
         */
        while (i < (4 + page_83[3]))
            (*serial)[j++] = page_83[i++];
    } else {
        /*
         * Binary descriptor, convert to ASCII, using two bytes of
         * ASCII for each byte in the page_83.
         */
        while (i < (4 + page_83[3])) {
            (*serial)[j++] = hex_str[(page_83[i] & 0xf0) >> 4];
            (*serial)[j++] = hex_str[ page_83[i] & 0x0f];
            i++;
        }
    }
    return 0;
}

/* Extract the raw binary from VPD 0x83 pre-SPC devices */
static int check_fill_0x83_prespc3(unsigned char *page_83, 
    const struct scsi_id_search_values *id_search, char **serial,
    size_t *max_len)
{
    int i, j;
    
    (*serial)[0] = hex_str[id_search->id_type];
    /* serial has been memset to zero before */
    j = strlen(*serial);    /* j = 1; */

    for (i = 0; i < page_83[3]; ++i) {
        (*serial)[j++] = hex_str[(page_83[4+i] & 0xf0) >> 4];
        (*serial)[j++] = hex_str[ page_83[4+i] & 0x0f];
    }
    return 0;
}

/* Get device identification VPD page */
static int do_scsi_page83_inquiry(int fd, char **serial, size_t *len)
{
    int retval;
    unsigned int id_ind, j;
    unsigned char page_83[SCSI_INQ_BUFF_LEN];

    memset(page_83, 0, SCSI_INQ_BUFF_LEN);
    if ((retval = scsi_inquiry(fd, 1, PAGE_83, page_83, SCSI_INQ_BUFF_LEN)) < 0)
        return retval;

    if (page_83[1] != PAGE_83)
        return -1;
    
    /*
     * XXX Some devices (IBM 3542) return all spaces for an identifier if
     * the LUN is not actually configured. This leads to identifers of
     * the form: "1            ".
     */

    /*
     * Model 4, 5, and (some) model 6 EMC Symmetrix devices return
     * a page 83 reply according to SCSI-2 format instead of SPC-2/3.
     *
     * The SCSI-2 page 83 format returns an IEEE WWN in binary
     * encoded hexi-decimal in the 16 bytes following the initial
     * 4-byte page 83 reply header.
     *
     * Both the SPC-2 and SPC-3 formats return an IEEE WWN as part
     * of an Identification descriptor.  The 3rd byte of the first
     * Identification descriptor is a reserved (BSZ) byte field.
     *
     * Reference the 7th byte of the page 83 reply to determine
     * whether the reply is compliant with SCSI-2 or SPC-2/3
     * specifications.  A zero value in the 7th byte indicates
     * an SPC-2/3 conformant reply, (i.e., the reserved field of the
     * first Identification descriptor).  This byte will be non-zero
     * for a SCSI-2 conformant page 83 reply from these EMC
     * Symmetrix models since the 7th byte of the reply corresponds
     * to the 4th and 5th nibbles of the 6-byte OUI for EMC, that is,
     * 0x006048.
     */
    
    if (page_83[6] != 0) 
        return check_fill_0x83_prespc3(page_83, id_search_list, serial, len);

    /*
     * Search for a match in the prioritized id_search_list.
     */
    for (id_ind = 0; id_ind < sizeof(id_search_list)/sizeof(id_search_list[0]);
            id_ind++) {
        /*
         * Examine each descriptor returned. There is normally only
         * one or a small number of descriptors.
         */
        for (j = 4; j <= (unsigned int)page_83[3] + 3; j += page_83[j + 3]+4) {
            retval = check_fill_0x83_id(&page_83[j], &id_search_list[id_ind],
                                        serial, len);
            if (retval <= 0)
                return retval;
        }
    }
    return 1;
}

/* Get unit serial number VPD page */
static int do_scsi_page80_inquiry(int fd, char **serial, size_t *max_len)
{
    int retval;
    int ser_ind = 0;
    int i;
    int len;
    unsigned char buf[SCSI_INQ_BUFF_LEN];

    memset(buf, 0, SCSI_INQ_BUFF_LEN);
    retval = scsi_inquiry(fd, 1, PAGE_80, buf, SCSI_INQ_BUFF_LEN);
    if (retval < 0)
        return retval;

    if (buf[1] != PAGE_80)
        return -1;

    len = buf[3];
    if (*max_len < len) {
        char *s;
        
        if (!(s = realloc(*serial, len)))
            return -1;
        *serial = s;
        *max_len = len;
    }

    len = buf[3];
    for (i = 4; i < len + 4; i++, ser_ind++)
        (*serial)[ser_ind] = buf[i];

    return 0;
}

static int result_ok(char **serial, size_t *len)
{
    return 1;
}

static int scsi_get_devattr(struct bdevid_bdev *bdev, char *attr, char **value)
{
    char *sysfs_dir = bdevid_bdev_get_sysfs_dir(bdev);
    char *tmp = NULL;
    FILE *f;
    int n;
    char buf[1024], *s;

    memset(buf, '\0', sizeof(buf));

    if (asprintf(&tmp, "%s/device/%s", sysfs_dir, attr) < 0)
        goto err;

    if (!(f = fopen(tmp, "r"))) 
        goto err;

    if (!fgets(buf, 1023, f))
        goto err;

    n = strlen(buf);
    if (buf[n-1] == '\n')
        buf[n-1] = '\0';

    free(tmp);
    tmp = NULL;

    fclose(f);
    for (s = buf; s[0] == ' '; s++)
        ;
    for (n = strlen(s) -1 ; n >= 0 && s[n] == ' '; n--)
        s[n] = '\0';

    if (!(tmp = strdup(s)))
        goto err;

    *value = tmp;
    return 0;   
err:
    if (tmp) {
        int en = errno;
        free(tmp);
        errno = en;
    }
    return -1;
}

static int scsi_get_vendor(struct bdevid_bdev *bdev, char **vendor)
{
    return scsi_get_devattr(bdev, "vendor", vendor);
}

static int scsi_get_model(struct bdevid_bdev *bdev, char **model)
{
    return scsi_get_devattr(bdev, "model", model);
}

static int scsi_get_unique_id(struct bdevid_bdev *bdev, char **id)
{
    char *serial = NULL;
    size_t len;
    int fd;
    unsigned char page0[SCSI_INQ_BUFF_LEN];
    int ind;
    int ret = -1;

    fd = bdevid_bdev_get_fd(bdev);

    len = 256;
    if (!(serial = calloc(len, sizeof(char))))
        goto err;

    if ((ret = do_scsi_page0_inquiry(fd, page0, SCSI_INQ_BUFF_LEN)) < 0)
        goto err;

    for (ind = 4; ind <= page0[3] + 3; ind++) {
        if (page0[ind] == PAGE_83) {
            ret = do_scsi_page83_inquiry(fd, &serial, &len);
            if (!ret && result_ok(&serial, &len)) {
                *id = serial;
                return 0;
            }
            break;
        }
    }
    for (ind = 4; ind <= page0[3] + 3; ind++) {
        if (page0[ind] == PAGE_80) {
            ret = do_scsi_page80_inquiry(fd, &serial, &len);
            if (!ret && result_ok(&serial, &len)) {
                *id = serial;
                return 0;
            }
            break;
        }
    }
err:
    if (serial)
        free(serial);
    return -1;
}

static struct bdevid_probe_ops scsi_probe_ops = {
    .name = "scsi_probe",
    .get_vendor = scsi_get_vendor,
    .get_model = scsi_get_model,
    .get_unique_id = scsi_get_unique_id,
};

static int scsi_init(struct bdevid_module *bm,
    bdevid_register_func register_probe)
{
    if (register_probe(bm, &scsi_probe_ops) == -1)
        return -1;
    return 0;
}

static struct bdevid_module_ops scsi_module = {
    .magic = BDEVID_MAGIC,
    .name = "scsi",
    .init = scsi_init,
};

BDEVID_MODULE(scsi_module);

/*
 * vim:ts=8:sw=4:sts=4:et
 */
