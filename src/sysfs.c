// Copyright(c) 2017, Intel Corporation
//
// Redistribution  and  use  in source  and  binary  forms,  with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of  source code  must retain the  above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation  nor the names of its contributors
//   may be used to  endorse or promote  products derived  from this  software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
// LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
// CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif // HAVE_CONFIG_H

#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <opae/types.h>

#include "types_int.h"
#include "sysfs_int.h"
#include "log_int.h"
#include "common_int.h"

//
// sysfs access (read/write) functions
//

fpga_result sysfs_read_int(const char *path, int *i)
{
	int fd;
	int res;
	char buf[SYSFS_PATH_MAX];
	int b;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		FPGA_MSG("open(%s) failed", path);
		return FPGA_NOT_FOUND;
	}

	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		FPGA_MSG("seek failed");
		goto out_close;
	}

	b = 0;

	do {
		res = read(fd, buf+b, sizeof(buf)-b);
		if (res <= 0) {
			FPGA_MSG("Read from %s failed", path);
			goto out_close;
		}
		b += res;
		if ((b > sizeof(buf)) || (b <= 0)) {
			FPGA_MSG("Unexpected size reading from %s", path);
			goto out_close;
		}
	} while (buf[b-1] != '\n' && buf[b-1] != '\0' && b < sizeof(buf));

	// erase \n
	buf[b-1] = 0;

	*i = atoi(buf);

	close(fd);
	return FPGA_OK;

out_close:
	close(fd);
	return FPGA_NOT_FOUND;
}

fpga_result sysfs_read_u32(const char *path, uint32_t *u)
{
	int fd;
	int res;
	char buf[SYSFS_PATH_MAX];
	int b;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		FPGA_MSG("open(%s) failed", path);
		return FPGA_NOT_FOUND;
	}

	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		FPGA_MSG("seek failed");
		goto out_close;
	}

	b = 0;

	do {
		res = read(fd, buf+b, sizeof(buf)-b);
		if (res <= 0) {
			FPGA_MSG("Read from %s failed", path);
			goto out_close;
		}
		b += res;
		if ((b > sizeof(buf)) || (b <= 0)) {
			FPGA_MSG("Unexpected size reading from %s", path);
			goto out_close;
		}
	} while (buf[b-1] != '\n' && buf[b-1] != '\0' && b < sizeof(buf));

	// erase \n
	buf[b-1] = 0;

	*u = strtoul(buf, NULL, 0);

	close(fd);
	return FPGA_OK;

out_close:
	close(fd);
	return FPGA_NOT_FOUND;
}

fpga_result __FIXME_MAKE_VISIBLE__ sysfs_read_u64(const char *path, uint64_t *u)
{
	int fd                     = -1;
	int res                    = 0;
	char buf[SYSFS_PATH_MAX]   = {0};
	int b                      = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		FPGA_MSG("open(%s) failed", path);
		return FPGA_NOT_FOUND;
	}

	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		FPGA_MSG("seek failed");
		goto out_close;
	}

	do {
		res = read(fd, buf+b, sizeof(buf)-b);
		if (res <= 0) {
			FPGA_MSG("Read from %s failed", path);
			goto out_close;
		}
		b += res;
		if ((b > sizeof(buf)) || (b <= 0)) {
			FPGA_MSG("Unexpected size reading from %s", path);
			goto out_close;
		}
	} while (buf[b-1] != '\n' && buf[b-1] != '\0' && b < sizeof(buf));

	// erase \n
	buf[b-1] = 0;

	*u = strtoull(buf, NULL, 0);

	close(fd);
	return FPGA_OK;

out_close:
	close(fd);
	return FPGA_NOT_FOUND;
}

fpga_result __FIXME_MAKE_VISIBLE__ sysfs_write_u64(const char *path, uint64_t u)
{
	int fd                     = -1;
	int res                    = 0;
	char buf[SYSFS_PATH_MAX]   = {0};
	int b                      = 0;

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		FPGA_MSG("open: %s", strerror(errno));
		return FPGA_NOT_FOUND;
	}

	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		FPGA_MSG("seek: %s", strerror(errno));
		goto out_close;
	}

	snprintf(buf, sizeof(buf), "0x%lx", u);

	do {
		res = write(fd, buf + b, sizeof(buf) -b);
		if (res <= 0) {
			FPGA_ERR("Failed to write");
			goto out_close;
		}
		b += res;

		if (b > sizeof(buf) || b <= 0) {
			FPGA_MSG("Unexpected size reading from %s", path);
			goto out_close;
		}

	} while (buf[b - 1] != '\n' && buf[b - 1] != '\0' && b < sizeof(buf));

	close(fd);
	return FPGA_OK;

out_close:
	close(fd);
	return FPGA_NOT_FOUND;
}

fpga_result sysfs_read_guid(const char *path, fpga_guid guid)
{
	int fd;
	int res;
	char buf[SYSFS_PATH_MAX];
	int b;

	int i;
	char tmp;
	unsigned octet;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		FPGA_MSG("open(%s) failed", path);
		return FPGA_NOT_FOUND;
	}

	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		FPGA_MSG("seek failed");
		goto out_close;
	}

	b = 0;

	do {
		res = read(fd, buf+b, sizeof(buf)-b);
		if (res <= 0) {
			FPGA_MSG("Read from %s failed", path);
			goto out_close;
		}
		b += res;
		if ((b > sizeof(buf)) || (b <= 0)) {
			FPGA_MSG("Unexpected size reading from %s", path);
			goto out_close;
		}
	} while (buf[b-1] != '\n' && buf[b-1] != '\0' && b < sizeof(buf));

	// erase \n
	buf[b-1] = 0;

	for (i = 0 ; i < 32 ; i += 2) {
		tmp = buf[i+2];
		buf[i+2] = 0;

		octet = 0;
		sscanf(&buf[i], "%x", &octet);
		guid[i/2] = (uint8_t) octet;

		buf[i+2] = tmp;
	}

	close(fd);
	return FPGA_OK;

out_close:
	close(fd);
	return FPGA_NOT_FOUND;
}

//
// sysfs convenience functions to access device components by device number
//

// FIXME: uses same number for device and FME (may not be true in future)
fpga_result sysfs_get_socket_id(int dev, uint8_t *socket_id)
{
	fpga_result result;
	char spath[SYSFS_PATH_MAX];
	int i;

	snprintf(spath, SYSFS_PATH_MAX,
		 SYSFS_FPGA_CLASS_PATH
		 SYSFS_FME_PATH_FMT "/"
		 FPGA_SYSFS_SOCKET_ID,
		 dev, dev);

	i = 0;
	result = sysfs_read_int(spath, &i);
	if (FPGA_OK != result)
		return result;

	*socket_id = (uint8_t) i;

	return FPGA_OK;
}

// FIXME: uses same number for device and PORT (may not be true in future)
fpga_result sysfs_get_afu_id(int dev, fpga_guid guid)
{
	char spath[SYSFS_PATH_MAX];

	snprintf(spath, SYSFS_PATH_MAX,
		 SYSFS_FPGA_CLASS_PATH
		 SYSFS_AFU_PATH_FMT "/"
		 FPGA_SYSFS_AFU_GUID,
		 dev, dev);

	return sysfs_read_guid(spath, guid);
}

fpga_result sysfs_get_pr_id(int dev, fpga_guid guid)
{
	char spath[SYSFS_PATH_MAX];

	snprintf(spath, SYSFS_PATH_MAX,
		 SYSFS_FPGA_CLASS_PATH
		 SYSFS_FME_PATH_FMT "/"
		 FPGA_SYSFS_FME_INTERFACE_ID,
		 dev, dev);

	return sysfs_read_guid(spath, guid);
}

// FIXME: uses same number for device and FME (may not be true in future)
fpga_result sysfs_get_slots(int dev, uint32_t *slots)
{
	char spath[SYSFS_PATH_MAX];

	snprintf(spath, SYSFS_PATH_MAX,
		 SYSFS_FPGA_CLASS_PATH
		 SYSFS_FME_PATH_FMT "/"
		 FPGA_SYSFS_NUM_SLOTS,
		 dev, dev);

	return sysfs_read_u32(spath, slots);
}

// FIXME: uses same number for device and FME (may not be true in future)
fpga_result sysfs_get_bitstream_id(int dev, uint64_t *id)
{
	char spath[SYSFS_PATH_MAX];

	snprintf(spath, SYSFS_PATH_MAX,
		 SYSFS_FPGA_CLASS_PATH
		 SYSFS_FME_PATH_FMT "/"
		 FPGA_SYSFS_BITSTREAM_ID,
		 dev, dev);

	return sysfs_read_u64(spath, id);
}

// Get port syfs path
fpga_result get_port_sysfs(fpga_handle handle,
				char *sysfs_port)
{

	struct _fpga_token  *_token;
	struct _fpga_handle *_handle  = (struct _fpga_handle *)handle;
	char *p                       = 0;
	int device_id                 = 0;

	if (sysfs_port == NULL) {
		FPGA_ERR("Invalid output pointer");
		return FPGA_INVALID_PARAM;
	}

	if (_handle == NULL) {
		FPGA_ERR("Invalid handle");
		return FPGA_INVALID_PARAM;
	}

	_token = (struct _fpga_token *)_handle->token;
	if (_token == NULL) {
		FPGA_ERR("Token not found");
		return FPGA_INVALID_PARAM;
	}

	p = strstr(_token->sysfspath, FPGA_SYSFS_FME);
	if (NULL == p) {
		FPGA_ERR("Invalid sysfspath in token");
		return FPGA_INVALID_PARAM;
	}
	p = strrchr(_token->sysfspath, '.');
	if (NULL == p) {
		FPGA_ERR("Invalid sysfspath in token");
		return FPGA_INVALID_PARAM;
	}

	device_id = atoi(p + 1);

	snprintf(sysfs_port, SYSFS_PATH_MAX,
		SYSFS_FPGA_CLASS_PATH SYSFS_AFU_PATH_FMT,
		device_id, device_id);

	return FPGA_OK;
}

// get fpga device id
fpga_result get_fpga_deviceid(fpga_handle handle,
				uint64_t *deviceid)
{
	struct _fpga_token  *_token      = NULL;
	struct _fpga_handle  *_handle    = (struct _fpga_handle *)handle;
	char sysfs_path[SYSFS_PATH_MAX]  = {0};
	char *p                          = NULL;
	int device_id                    = 0;
	fpga_result result               = FPGA_OK;
	int err                          = 0;

	if (_handle == NULL) {
		FPGA_ERR("Invalid handle");
		return FPGA_INVALID_PARAM;
	}

	if (deviceid == NULL) {
		FPGA_ERR("Invalid input Parameters");
		return FPGA_INVALID_PARAM;
	}

	if (pthread_mutex_lock(&_handle->lock)) {
		FPGA_MSG("Failed to lock handle mutex");
		return FPGA_EXCEPTION;
	}

	_token = (struct _fpga_token *)_handle->token;
	if (_token == NULL) {
		FPGA_ERR("Token not found");
		result = FPGA_INVALID_PARAM;
		goto out_unlock;
	}

	p = strstr(_token->sysfspath, FPGA_SYSFS_FME);
	if (p == NULL) {
		FPGA_ERR("Failed to read sysfs path");
		result = FPGA_NOT_SUPPORTED;
		goto out_unlock;
	}

	p = strrchr(_token->sysfspath, '.');
	if (p == NULL) {
		FPGA_ERR("Failed to read sysfs path");
		result = FPGA_NOT_SUPPORTED;
		goto out_unlock;
	}

	device_id = atoi(p + 1);

	snprintf(sysfs_path,
		 SYSFS_PATH_MAX,
		 SYSFS_FPGA_CLASS_PATH SYSFS_FPGA_FMT"/%s",
		 device_id,
		 FPGA_SYSFS_DEVICEID);

	result = sysfs_read_u64(sysfs_path, deviceid);
	if (result != 0) {
		FPGA_ERR("Failed to read device ID");
		goto out_unlock;
	}

out_unlock:
	err = pthread_mutex_unlock(&_handle->lock);
	if (err)
		FPGA_ERR("pthread_mutex_unlock() failed: %s", strerror(err));
	return result;
}

/*
 * The rlpath path is assumed to be of the form:
 * ../../devices/pci0000:5e/0000:5e:00.0/fpga/intel-fpga-dev.0
 */
fpga_result sysfs_bdf_from_path(const char *sysfspath, int *b, int *d, int *f)
{
	int res;
	char rlpath[SYSFS_PATH_MAX];
	char *p;

	res = readlink(sysfspath, rlpath, sizeof(rlpath));
	if (-1 == res) {
		FPGA_MSG("Can't read link %s (no driver?)", sysfspath);
		return FPGA_NO_DRIVER;
	}

	// Find the BDF from the link path.
	rlpath[res] = 0;
	p = strrchr(rlpath, '/');
	if (!p) {
		FPGA_MSG("Invalid link %s (no driver?)", rlpath);
		return FPGA_NO_DRIVER;
	}
	*p = 0;
	p = strrchr(rlpath, '/');
	if (!p) {
		FPGA_MSG("Invalid link %s (no driver?)", rlpath);
		return FPGA_NO_DRIVER;
	}
	*p = 0;
	p = strrchr(rlpath, '/');
	if (!p) {
		FPGA_MSG("Invalid link %s (no driver?)", rlpath);
		return FPGA_NO_DRIVER;
	}
	p += 6;

	// 0123456
	// bb:dd.f
	*f = (int) strtoul(p+6, NULL, 16);
	*(p + 5) = 0;

	*d = (int) strtoul(p+3, NULL, 16);
	*(p + 2) = 0;

	*b = (int) strtoul(p, NULL, 16);

	return FPGA_OK;
}

