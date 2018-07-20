// Copyright(c) 2018, Intel Corporation
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
#include "fpgainfo.h"
#include "bmcinfo.h"
#include "bmcdata.h"
#include "safe_string/safe_string.h"
#include <opae/fpga.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include "sysinfo.h"
#include <fcntl.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef DEBUG
#define DBG_PRINT(...) printf(__VA_ARGS__)
#else
#define DBG_PRINT(...)                                                         \
	fflush(stdout);                                                        \
	fflush(stderr)
#endif

#define MODEL_SIZE 64

struct bmc_info {
	fpga_guid guid;
	uint64_t object_id;
	uint8_t bus;
	uint8_t device;
	uint8_t function;
	uint8_t socket_id;
	uint32_t device_id;
	char model[MODEL_SIZE];
	uint32_t num_slots;
	uint64_t bbs_id;
	fpga_version bbs_version;
	uint64_t capabilities;
};

static fpga_result get_bmc_info(fpga_token tok, struct bmc_info *finfo)
{
	fpga_result res = FPGA_OK;
	fpga_properties props;
	res = fpgaGetProperties(tok, &props);
	ON_FPGAINFO_ERR_GOTO(res, out, "reading properties from token");

	fpgaPropertiesGetObjectID(props, &finfo->object_id);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy,
			     "reading object_id from properties");

	fpgaPropertiesGetGUID(props, &finfo->guid);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy, "reading guid from properties");

	fpgaPropertiesGetBus(props, &finfo->bus);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy, "reading bus from properties");

	fpgaPropertiesGetDevice(props, &finfo->device);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy,
			     "reading device from properties");

	fpgaPropertiesGetFunction(props, &finfo->function);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy,
			     "reading function from properties");

	fpgaPropertiesGetSocketID(props, &finfo->socket_id);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy,
			     "reading socket_id from properties");

	// TODO: Implement once device_id, model, and capabilities accessors are
	// implemented

	// fpgaPropertiesGetDeviceId(props, &finfo->device_id);
	// ON_FPGAINFO_ERR_GOTO(res, out_destroy, "reading device_id from
	// properties");

	// fpgaPropertiesGetModel(props, &finfo->model);
	// ON_FPGAINFO_ERR_GOTO(res, out_destroy, "reading model from
	// properties");

	// fpgaPropertiesGetCapabilities(props, &finfo->capabilities);
	// ON_FPGAINFO_ERR_GOTO(res, out_destroy, "reading capabilities from
	// properties");

	fpgaPropertiesGetNumSlots(props, &finfo->num_slots);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy,
			     "reading num_slots from properties");

	fpgaPropertiesGetBBSID(props, &finfo->bbs_id);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy,
			     "reading bbs_id from properties");

	fpgaPropertiesGetBBSVersion(props, &finfo->bbs_version);
	ON_FPGAINFO_ERR_GOTO(res, out_destroy,
			     "reading bbs_version from properties");

out_destroy:
	fpgaDestroyProperties(&props);
out:
	return res;
}

static void print_bmc_info(struct bmc_info *info)
{
	char guid_str[38];
	uuid_unparse(info->guid, guid_str);
	printf("//****** bmc ******//\n");
	printf("%-24s : 0x%2lX\n", "Object Id", info->object_id);
	printf("%-24s : 0x%02X\n", "Bus", info->bus);
	printf("%-24s : 0x%02X\n", "Device", info->device);
	printf("%-24s : 0x%02X\n", "Function", info->function);
	printf("%-24s : 0x%02X\n", "Socket Id", info->socket_id);
	printf("%-24s : %02d\n", "Ports Num", info->num_slots);
	printf("%-24s : 0x%lX\n", "Bitstream Id", info->bbs_id);
	printf("%-24s : 0x%lX\n", "Bitstream Metadata",
	       *(uint64_t *)&info->bbs_version);
	printf("%-24s : %s\n", "Pr Interface Id", guid_str);
	// printf("%-24s : 0x%2lX\n", "Capabilities", info->capabilities);
}

fpga_result bmc_filter(fpga_properties *filter, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	fpga_result res = FPGA_OK;
	res = fpgaPropertiesSetObjectType(*filter, FPGA_DEVICE);
	fpgainfo_print_err("setting type to FPGA_DEVICE", res);
	return res;
}

fpga_result bmc_command(fpga_token *tokens, int num_tokens, int argc,
			char *argv[])
{
	(void)tokens;
	(void)num_tokens;
	(void)argc;
	(void)argv;

	fpga_result res = FPGA_OK;
	struct bmc_info info;

	int i = 0;
	for (i = 0; i < num_tokens; ++i) {
		res = get_bmc_info(tokens[i], &info);
		ON_FPGAINFO_ERR_GOTO(res, out, 0);
		print_bmc_info(&info);
	}
out:
	return res;
}

Values *bmc_build_values(sensor_reading *reading, sdr_header *header,
			 sdr_key *key, sdr_body *body)
{
	Values *val = (Values *)calloc(1, sizeof(Values));

	(void)header;

	if (NULL == val)
		return NULL;

	memset(val, 0, sizeof(*val));

	val->is_valid = true;

	if (reading->sensor_validity.sensor_state.sensor_scanning_disabled) {
		val->annotation_1 = "scanning disabled";
		// val->is_valid = false;
	}
	if (reading->sensor_validity.sensor_state.reading_state_unavailable) {
		val->annotation_2 = "reading state unavailable";
		val->is_valid = false;
	}
	if (reading->sensor_validity.sensor_state.event_messages_disabled) {
		val->annotation_3 = "event messages disabled";
	}

	if (body->id_string_type_length_code.bits.format == ASCII_8) {
		uint8_t len =
			body->id_string_type_length_code.bits.len_in_characters;
		if ((len == 0x1f) || (len == 0)) {
			val->name = strdup("**INVALID**");
			val->is_valid = false;
		} else {
			val->name = strdup((char *)&body->string_bytes[0]);
		}
	} else {
		val->name = strdup("**String type unimplemented**");
		fprintf(stderr, "String type other than ASCII8\n");
	}

	val->sensor_number = key->sensor_number;
	val->sensor_type = body->sensor_type;

	switch (body->sensor_units_1.bits.analog_data_format) {
	case 0x0: // unsigned
	case 0x1: // 1's compliment (signed)
	case 0x2: // 2's complement (signed)
		break;
	case 0x3: // Does not return a reading
		val->is_valid = false;
		break;
	}

	if (body->sensor_units_2 < max_base_units) {
		val->units = base_units[body->sensor_units_2];
	} else {
		val->units = L"*OUT OF RANGE*";
	}

	int tmp = bmcdata_verbose;
	bmcdata_verbose = 0;
	calc_params(body, val);
	bmcdata_verbose = tmp;

	// val->value.i_val = (uint64_t)reading->sensor_reading;
	// val->val_type = SENSOR_INT;

	val->raw_value = (uint64_t)reading->sensor_reading;
	val->val_type = SENSOR_FLOAT;
	val->value.f_val = getvalue(val, val->raw_value);

	return val;
}

static int read_struct(int fd, char *data, int *offset, size_t size, char *path)
{
	int bytes_read = 0;
	DBG_PRINT("read_struct: data=%p, offset=%d, size=%d, path='%s'\n", data,
		  *offset, (int)size, path);
	int old_offset = *offset;
	do {
		bytes_read = read(fd, data, size);
		DBG_PRINT("read_struct: bytes_read=%d\n", bytes_read);
		if (bytes_read < 0) {
			FPGA_MSG("Read from %s failed", path);
			return -1;
		}
		*offset += bytes_read;
		if (((size_t)*offset > size + old_offset) || (*offset <= 0)) {
			FPGA_MSG("Unexpected size reading from %s", path);
			return -1;
		}
	} while (((size_t)*offset < (size + old_offset)) && (bytes_read != 0));

	return bytes_read;
}

static void bmc_read_sensor_data(const char *sysfspath, Values **vals)
{
	char sdr_path[SYSFS_PATH_MAX];
	char sensor_path[SYSFS_PATH_MAX];
	int sdr_fd;
	int sensor_fd;
	char sdr_data[sizeof(sdr_header) + sizeof(sdr_key) + sizeof(sdr_body)];
	char sensor_data[sizeof(sensor_reading)];
	int sdr_offset = 0;
	int sensor_offset = 0;
	int num_sensors = 0;
	Values *last_val = NULL;

	if ((NULL == sysfspath) || (NULL == vals))
		return;
	*vals = NULL;

	snprintf_s_ss(sdr_path, sizeof(sdr_path), "%s/%s", sysfspath,
		      "avmmi-bmc.3.auto/bmc_info/sdr");

	snprintf_s_ss(sensor_path, sizeof(sensor_path), "%s/%s", sysfspath,
		      "avmmi-bmc.3.auto/bmc_info/sensors");

	sensor_fd = open(sensor_path, O_RDONLY);
	if (sensor_fd < 0) {
		FPGA_MSG("open(%s) failed", sensor_path);
		return;
	}

	if ((off_t)-1 == lseek(sensor_fd, 0, SEEK_SET)) {
		FPGA_MSG("seek failed");
		close(sensor_fd);
		return;
	}

	sdr_fd = open(sdr_path, O_RDONLY);
	if (sdr_fd < 0) {
		FPGA_MSG("open(%s) failed", sdr_path);
		return;
	}

	if ((off_t)-1 == lseek(sdr_fd, 0, SEEK_SET)) {
		FPGA_MSG("seek failed");
		close(sdr_fd);
		return;
	}

	sdr_header *header = (sdr_header *)&sdr_data[0];
	sdr_key *key = (sdr_key *)&sdr_data[sizeof(sdr_header)];
	sdr_body *body =
		(sdr_body *)&sdr_data[sizeof(sdr_header) + sizeof(sdr_key)];
	sensor_reading *reading = (sensor_reading *)&sensor_data[0];

	for (num_sensors = 0;; num_sensors++) {
		int ret;

		// Read each sensor's data
		ret = read_struct(sensor_fd, (char *)reading, &sensor_offset,
				  sizeof(sensor_reading), sensor_path);

		if (0 == ret)
			break;

		if (ret < 0) {
			close(sensor_fd);
			close(sdr_fd);
			return;
		}

		// Read the SDR record for this sensor

		ret = read_struct(sdr_fd, (char *)header, &sdr_offset,
				  sizeof(sdr_header), sdr_path);

		if (0 == ret)
			break;

		if (ret < 0) {
			close(sensor_fd);
			close(sdr_fd);
			return;
		}

		ret = read_struct(sdr_fd, (char *)key, &sdr_offset,
				  sizeof(sdr_key), sdr_path);

		if (0 == ret)
			break;

		if (ret < 0) {
			close(sensor_fd);
			close(sdr_fd);
			return;
		}

		ret = read_struct(sdr_fd, (char *)body, &sdr_offset,
				  header->record_length - sizeof(sdr_key),
				  sdr_path);

		if (0 == ret)
			break;

		if (ret < 0) {
			close(sensor_fd);
			close(sdr_fd);
			return;
		}

		// Build a Values struct for display
		Values *val = bmc_build_values(reading, header, key, body);

		if (NULL == val) {
			close(sensor_fd);
			close(sdr_fd);
			return;
		}

		if (NULL == last_val) {
			*vals = val;
		} else {
			last_val->next = val;
		}
		last_val = val;

		bmc_print_detail(reading, header, key, body);
	}
}

fpga_result bmc_print_values(const char *sysfs_path, BMC_TYPE type)
{
	fpga_result res = FPGA_OK;
	Values *vals = NULL;
	Values *vptr;

	if (NULL == sysfs_path)
		return FPGA_INVALID_PARAM;

	bmc_read_sensor_data(sysfs_path, &vals);

	for (vptr = vals; NULL != vptr; vptr = vptr->next) {
		if (!(((BMC_THERMAL == type) && (SDR_SENSOR_IS_TEMP(vptr)))
		      || ((BMC_POWER == type) && (SDR_SENSOR_IS_POWER(vptr)))))
			continue;
		printf("%-24s : ", vptr->name);
		if (!vptr->is_valid) {
			printf("No reading");
		} else {

			if (vptr->val_type == SENSOR_INT) {
				printf("%" PRIu64 " %ls", vptr->value.i_val,
				       vptr->units);
			} else if (vptr->val_type == SENSOR_FLOAT) {
				printf("%.2lf %ls", vptr->value.f_val,
				       vptr->units);
			}
		}

		if (vptr->annotation_1) {
			printf(" (%s)", vptr->annotation_1);
		}
		if (vptr->annotation_2) {
			printf(" (%s)", vptr->annotation_2);
		}
		if (vptr->annotation_3) {
			printf(" (%s)", vptr->annotation_3);
		}

		// printf("Raw=%d, M=%f, B=%f, R_exp=%d, acc=%f, tol=%d\n",
		//       vptr->raw_value, vptr->M, vptr->B, vptr->result_exp,
		//       vptr->accuracy, vptr->tolerance);
		printf("\n");
	}

	return res;
}
