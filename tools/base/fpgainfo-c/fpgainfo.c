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

#define _GNU_SOURCE
#include <string.h>

#include "fpgainfo.h"
#include "opae/fpga.h"
#include <inttypes.h>
#include <uuid/uuid.h>
#include "safe_string/safe_string.h"
#include <ctype.h>

/*
 * Print readable error message for fpga_results
 */
void fpgainfo_print_err(const char *s, fpga_result res)
{
	if (s && res)
		fprintf(stderr, "Error %s: %s\n", s, fpgaErrStr(res));
}

void fpgainfo_print_common(const char *hdr, fpga_properties props)
{
	fpga_result res = FPGA_OK;
	char guid_str[38];
	uint64_t object_id;
	uint8_t bus;
	uint16_t segment;
	uint8_t device;
	uint8_t function;
	uint16_t device_id;
	uint8_t socket_id;
	fpga_guid guid;
	uint32_t num_slots;
	uint64_t bbs_id;
	fpga_version bbs_version;
	fpga_objtype objtype;
	fpga_properties pprops = props;
	fpga_token par = NULL;

	res = fpgaPropertiesGetObjectID(props, &object_id);
	fpgainfo_print_err("reading object_id from properties", res);

	res = fpgaPropertiesGetBus(props, &bus);
	fpgainfo_print_err("reading bus from properties", res);

	res = fpgaPropertiesGetSegment(props, &segment);
	fpgainfo_print_err("reading segment from properties", res);

	res = fpgaPropertiesGetDevice(props, &device);
	fpgainfo_print_err("reading device from properties", res);

	res = fpgaPropertiesGetFunction(props, &function);
	fpgainfo_print_err("reading function from properties", res);

	res = fpgaPropertiesGetSocketID(props, &socket_id);
	fpgainfo_print_err("reading socket_id from properties", res);

	res = fpgaPropertiesGetDeviceID(props, &device_id);
	fpgainfo_print_err("reading device_id from properties", res);

	res = fpgaPropertiesGetObjectType(props, &objtype);
	fpgainfo_print_err("reading objtype from properties", res);

	// Go up the tree until we find the device
	while (objtype != FPGA_DEVICE) {
		res = fpgaPropertiesGetParent(pprops, &par);
		fpgainfo_print_err("reading objtype from properties", res);

		if (pprops != props) {
			res = fpgaDestroyProperties(pprops);
			fpgainfo_print_err("destroying parent properties", res);
			pprops = props;
		}

		res = fpgaGetProperties(par, &pprops);
		fpgainfo_print_err("reading parent properties", res);

		res = fpgaPropertiesGetObjectType(pprops, &objtype);
		fpgainfo_print_err("reading objtype from properties", res);

		res = fpgaDestroyToken(&par);
		fpgainfo_print_err("destroying parent token", res);
	};

	res = fpgaPropertiesGetDeviceID(pprops, &device_id);
	fpgainfo_print_err("reading device_id from properties", res);

	res = fpgaPropertiesGetGUID(pprops, &guid);
	fpgainfo_print_err("reading guid from properties", res);

	res = fpgaPropertiesGetNumSlots(pprops, &num_slots);
	fpgainfo_print_err("reading num_slots from properties", res);

	res = fpgaPropertiesGetBBSID(pprops, &bbs_id);
	fpgainfo_print_err("reading bbs_id from properties", res);

	res = fpgaPropertiesGetBBSVersion(pprops, &bbs_version);
	fpgainfo_print_err("reading bbs_version from properties", res);

	// TODO: Implement once model and capabilities accessors are
	// implemented

	// res = fpgaPropertiesGetModel(props, &model);
	// fpgainfo_print_err("reading model from properties", res);

	// res = fpgaPropertiesGetCapabilities(props, &capabilities);
	// fpgainfo_print_err("reading capabilities from properties", res);

	if (pprops != props) {
		res = fpgaDestroyProperties(&pprops);
		fpgainfo_print_err("destroying parent properties after use",
				   res);
		pprops = props;
	}

	printf("%s\n", hdr);
	printf("%-24s : 0x%2" PRIX64 "\n", "Object Id", object_id);
	// printf("%-24s : 0x%04X\n", "Segment", segment);
	// printf("%-24s : 0x%02X\n", "Bus", bus);
	// printf("%-24s : 0x%02X\n", "Device", device);
	// printf("%-24s : 0x%02X\n", "Function", function);
	printf("%-24s : %04X:%02X:%02X:%01X\n", "PCIe s:b:d:f", segment, bus,
	       device, function);
	printf("%-24s : 0x%04X\n", "Device Id", device_id);
	printf("%-24s : 0x%02X\n", "Socket Id", socket_id);
	printf("%-24s : %02d\n", "Ports Num", num_slots);
	printf("%-24s : 0x%" PRIX64 "\n", "Bitstream Id", bbs_id);
	printf("%-24s : 0x%lX\n", "Bitstream Version",
	       *(uint64_t *)&bbs_version);
	uuid_unparse(guid, guid_str);
	printf("%-24s : %s\n", "Pr Interface Id", guid_str);
}

// Replace occurrences of character within string
char *replace_chars(char *str, char match, char rep)
{
	char *tmp = strchr(str, match);
	while (tmp) {
		*tmp = rep;
		tmp = strchr(tmp + 1, match);
	}
	return str;
}

// Turn all "pcie" into "PCIe"
char *upcase_pci(char *str)
{
	char *tmp = strcasestr(str, "pci");
	while (tmp) {
		*tmp++ = 'P';
		*tmp++ = 'C';
		*tmp++ = 'I';
		tmp = strcasestr(tmp, "pci");
	}
	return str;
}

// Upper-case the first letter of each word in str
char *upcase_first(char *str)
{
	*str = toupper(*str);
	char *tmp = strchr(str + 1, ' ');
	while (tmp) {
		if (tmp[1] && isalpha(tmp[1])) {
			tmp[1] = toupper(tmp[1]);
		}
		tmp = strchr(tmp + 1, ' ');
	}
	return str;
}


// clang-format off
#if 0
scottgi@sj-avl-d15-mc:~$ finfo errors all
2018-07-16 19:45:42,559 [WARNING] Could not open log file: /tmp/fpgainfo.log.Logging to stderr
//****** FME ERRORS ******//
Class Path             : /sys/class/fpga/intel-fpga-dev.0/intel-fpga-fme.0/errors
Device Path            : /sys/devices/pci0000:00/0000:00:02.0/0000:05:00.0/fpga/intel-fpga-dev.0/intel-fpga-fme.0/errors
Bus                    : 0x05
Device                 : 0x00
Function               : 0x00
Device Id              : 0x09C4
Catfatal Errors        : 0x00
Errors                 : 0x00
First Error            : 0x00
Next Error             : 0x00
Nonfatal Errors        : 0x00
Pcie0 Errors           : 0x00
Revision               : 0


//****** PORT ERRORS ******//
Class Path             : /sys/class/fpga/intel-fpga-dev.0/intel-fpga-port.0/errors
Device Path            : /sys/devices/pci0000:00/0000:00:02.0/0000:05:00.0/fpga/intel-fpga-dev.0/intel-fpga-port.0/errors
Bus                    : 0x05
Device                 : 0x00
Function               : 0x00
Device Id              : 0x09C4
Errors                 : 0x00
First Error            : 0x00
Revision               : 1


scottgi@sj-avl-d15-mc:~$ finfo power
2018-07-16 19:45:59,765 [WARNING] Could not open log file: /tmp/fpgainfo.log.Logging to stderr
*** POWER NOT SUPPORTED ON HW ***
Class Path             : /sys/class/fpga/intel-fpga-dev.0/intel-fpga-fme.0/power_mgmt
Device Path            : /sys/devices/pci0000:00/0000:00:02.0/0000:05:00.0/fpga/intel-fpga-dev.0/intel-fpga-fme.0/power_mgmt
Bus                    : 0x05
Device                 : 0x00
Function               : 0x00
Device Id              : 0x09C4


scottgi@sj-avl-d15-mc:~$ finfo temp
2018-07-16 19:46:10,952 [WARNING] Could not open log file: /tmp/fpgainfo.log.Logging to stderr
//****** THERMAL ******//
Class Path             : /sys/class/fpga/intel-fpga-dev.0/intel-fpga-fme.0/thermal_mgmt
Device Path            : /sys/devices/pci0000:00/0000:00:02.0/0000:05:00.0/fpga/intel-fpga-dev.0/intel-fpga-fme.0/thermal_mgmt
Bus                    : 0x05
Device                 : 0x00
Function               : 0x00
Device Id              : 0x09C4
Temperature            : 40� C


scottgi@sj-avl-d15-mc:~$ finfo fme
2018-07-16 19:46:15,960 [WARNING] Could not open log file: /tmp/fpgainfo.log.Logging to stderr
//****** FME ******//
Class Path             : /sys/class/fpga/intel-fpga-dev.0/intel-fpga-fme.0
Device Path            : /sys/devices/pci0000:00/0000:00:02.0/0000:05:00.0/fpga/intel-fpga-dev.0/intel-fpga-fme.0
Bus                    : 0x05
Device                 : 0x00
Function               : 0x00
Device Id              : 0x09C4
Fim Version            : 1.1.2
Ports Num              : 1
Socket Id              : 0
Bitstream Id           : 0x112000200000110
Bitstream Metadata     : 0x1807151
Pr Interface Id        : 72f7d402-b5ca-5f21-80e4-bec918d6447a
Object Id              : 254803968


scottgi@sj-avl-d15-mc:~$ finfo port
2018-07-16 19:46:26,246 [WARNING] Could not open log file: /tmp/fpgainfo.log.Logging to stderr
//****** PORT ******//
Class Path             : /sys/class/fpga/intel-fpga-dev.0/intel-fpga-port.0
Device Path            : /sys/devices/pci0000:00/0000:00:02.0/0000:05:00.0/fpga/intel-fpga-dev.0/intel-fpga-port.0
Bus                    : 0x05
Device                 : 0x00
Function               : 0x00
Device Id              : 0x09C4
Afu Id                 : f7df405c-bd7a-cf72-22f1-44b0b93acd18
Object Id              : 253755392



scottgi@sj-avl-d15-mc:~$ finfo -h
usage: fpgainfo.py [-h] {errors,power,temp,fme,port} ...

optional arguments:
  -h, --help            show this help message and exit

fpga commands:
  {errors,power,temp,fme,port}
scottgi@sj-avl-d15-mc:~$ finfo errors -h
usage: fpgainfo.py errors [-h] [-B BUS] [-D DEVICE] [-F FUNCTION] [--json]
                          [-c] [--force]
                          {fme,port,all}

positional arguments:
  {fme,port,all}        specify what kind of errors to operate on

optional arguments:
  -h, --help            show this help message and exit
  -B BUS, --bus BUS     pcie bus number of resource
  -D DEVICE, --device DEVICE
                        pcie device number of resource
  -F FUNCTION, --function FUNCTION
                        pcie function number of resource
  --json                Display information as JSON string
  -c, --clear           specify whether or not to clear error registers
  --force               retry clearing errors up to 64 to clear certain error
                        conditions
scottgi@sj-avl-d15-mc:~$ finfo power -h
usage: fpgainfo.py power [-h] [-B BUS] [-D DEVICE] [-F FUNCTION] [--json]

optional arguments:
  -h, --help            show this help message and exit
  -B BUS, --bus BUS     pcie bus number of resource
  -D DEVICE, --device DEVICE
                        pcie device number of resource
  -F FUNCTION, --function FUNCTION
                        pcie function number of resource
  --json                Display information as JSON string
scottgi@sj-avl-d15-mc:~$ finfo temp -h
usage: fpgainfo.py temp [-h] [-B BUS] [-D DEVICE] [-F FUNCTION] [--json]

optional arguments:
  -h, --help            show this help message and exit
  -B BUS, --bus BUS     pcie bus number of resource
  -D DEVICE, --device DEVICE
                        pcie device number of resource
  -F FUNCTION, --function FUNCTION
                        pcie function number of resource
  --json                Display information as JSON string
scottgi@sj-avl-d15-mc:~$ finfo fme -h
usage: fpgainfo.py fme [-h] [-B BUS] [-D DEVICE] [-F FUNCTION] [--json]

optional arguments:
  -h, --help            show this help message and exit
  -B BUS, --bus BUS     pcie bus number of resource
  -D DEVICE, --device DEVICE
                        pcie device number of resource
  -F FUNCTION, --function FUNCTION
                        pcie function number of resource
  --json                Display information as JSON string
scottgi@sj-avl-d15-mc:~$ finfo port -h
usage: fpgainfo.py port [-h] [-B BUS] [-D DEVICE] [-F FUNCTION] [--json]

optional arguments:
  -h, --help            show this help message and exit
  -B BUS, --bus BUS     pcie bus number of resource
  -D DEVICE, --device DEVICE
                        pcie device number of resource
  -F FUNCTION, --function FUNCTION
                        pcie function number of resource
  --json                Display information as JSON string
scottgi@sj-avl-d15-mc:~$ 

#endif
