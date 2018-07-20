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
/*
 * @file errors.c
 *
 * @brief fpga error reporting
 *
 */
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
//#include <sysexits.h>

#include "fpgainfo.h"
#include "safe_string/safe_string.h"
#include <opae/properties.h>

#include "errors.h"
#include "sysinfo.h"
#include "bmcinfo.h"


const char *supported_verbs[] = {"all", "fme", "port"};
enum verbs_index { VERB_ALL = 0, VERB_FME, VERB_PORT, VERB_MAX };

/*
 * errors command configuration, set during parse_args()
 */
static struct errors_config {
	bool clear;
	enum verbs_index which;
} errors_config = {.clear = false, .which = VERB_ALL};

// TODO: Move this to a common file for reuse in other fpgainfo files
static int str_in_list(const char *key, const char *list[], size_t size)
{
	size_t i = 0;
	for (; i < size; ++i) {
		if (!strcmp(key, list[i])) {
			return i;
		}
	}
	return VERB_MAX;
}

#define MAX_VERB_LENGTH 8
#define GETOPT_STRING ":c"
int parse_error_args(int argc, char *argv[])
{
	optind = 0;
	struct option longopts[] = {{"clear", no_argument, NULL, 'c'},
				    {0, 0, 0, 0}};

	int getopt_ret;
	int option_index;

	while (-1
	       != (getopt_ret = getopt_long(argc, argv, GETOPT_STRING, longopts,
					    &option_index))) {
		const char *tmp_optarg = optarg;

		if ((optarg) && ('=' == *tmp_optarg)) {
			++tmp_optarg;
		}

		switch (getopt_ret) {
		case 'c': /* clear */
			errors_config.clear = true;
			break;

		case ':': /* missing option argument */
			fprintf(stderr, "Missing option argument\n");
			return -1;

		case '?':
		default: /* invalid option */
			fprintf(stderr, "Invalid cmdline options\n");
			return -1;
		}
	}

	// The word after 'errors' should be what to operate on ("all", "fme",
	// or "port")
	optind++;
	if (argc < optind + 1) {
		fprintf(stderr, "Not enough parameters\n");
		return -1;
	}

	if (optind < argc && !strcmp(argv[optind], "errors")) {
		char *verb = argv[optind + 1];
		size_t idx = str_in_list(verb, supported_verbs, VERB_MAX);
		if (idx < VERB_MAX) {
			errors_config.which = idx;
		} else {
			fprintf(stderr,
				"Not a valid errors resource spec: %s\n", verb);
			return -1;
		}
	}

	return 0;
}

fpga_result errors_filter(fpga_properties *filter, int argc, char *argv[])
{
	fpga_result res = FPGA_OK;
	if (0 == parse_error_args(argc, argv)) {
		switch (errors_config.which) {
		case VERB_FME:
			res = fpgaPropertiesSetObjectType(*filter, FPGA_DEVICE);
			ON_FPGAINFO_ERR_GOTO(res, out,
					     "setting type to FPGA_DEVICE");
			break;
		case VERB_PORT:
			res = fpgaPropertiesSetObjectType(*filter,
							  FPGA_ACCELERATOR);
			ON_FPGAINFO_ERR_GOTO(
				res, out, "setting type to FPGA_ACCELERATOR");
			break;
		case VERB_ALL:
		default:
			break;
		}
	}
out:
	return res;
}

void print_errors_info(fpga_token token, fpga_properties props,
		       struct fpga_error_info *errinfos, uint32_t num_errors)
{
	int i;
	fpga_result res = FPGA_OK;
	fpga_objtype objtype;

	if ((NULL == errinfos) || (0 == num_errors)) {
		return;
	}

	if (errors_config.clear) {
		fpgaClearAllErrors(token);
	}

	res = fpgaPropertiesGetObjectType(props, &objtype);
	fpgainfo_print_err("reading objtype from properties", res);

	if (((VERB_ALL == errors_config.which)
	     || (VERB_FME == errors_config.which))
	    && (FPGA_DEVICE == objtype)) {
		fpgainfo_print_common("//****** FME ERRORS ******//", props);

		for (i = 0; i < (int)num_errors; i++) {
			uint64_t error_value = 0;
			res = fpgaReadError(token, i, &error_value);
			fpgainfo_print_err("reading error for FME", res);

			printf("%-24s : 0x%" PRIX64 "\n", errinfos[i].name,
			       error_value);
		}
	} else if (((VERB_ALL == errors_config.which)
		    || (VERB_PORT == errors_config.which))
		   && (FPGA_ACCELERATOR == objtype)) {
		fpgainfo_print_common("//****** PORT ERRORS ******//", props);

		for (i = 0; i < (int)num_errors; i++) {
			uint64_t error_value = 0;
			res = fpgaReadError(token, i, &error_value);
			fpgainfo_print_err("reading error for FME", res);

			printf("%-24s : 0x%" PRIX64 "\n", errinfos[i].name,
			       error_value);
		}
	}
}

fpga_result errors_command(fpga_token *tokens, int num_tokens, int argc,
			   char *argv[])
{
	(void)tokens;
	(void)num_tokens;
	(void)argc;
	(void)argv;
	fpga_result res = FPGA_OK;
	fpga_properties props;
	struct fpga_error_info *errinfos = NULL;

	int i = 0;
	for (i = 0; i < num_tokens; ++i) {
		uint32_t num_errors;

		res = fpgaGetProperties(tokens[i], &props);
		ON_FPGAINFO_ERR_GOTO(res, out_destroy,
				     "reading properties from token");

		res = fpgaPropertiesGetNumErrors(props, &num_errors);
		fpgainfo_print_err("reading segment from properties", res);

		if (0 != num_errors) {
			int j;
			errinfos = (struct fpga_error_info *)calloc(
				num_errors, sizeof(*errinfos));
			if (NULL == errinfos) {
				fprintf(stderr, "Error allocating memory");
				goto destroy_and_free;
			}

			for (j = 0; j < (int)num_errors; j++) {
				res = fpgaGetErrorInfo(tokens[i], j,
						       &errinfos[j]);
				fpgainfo_print_err(
					"reading error info structure", res);
				(void)replace_chars(errinfos[j].name, '_', ' ');
				(void)upcase_pci(errinfos[j].name);
				(void)upcase_first(errinfos[j].name);
			}
		}

		print_errors_info(tokens[i], props, errinfos, num_errors);
	destroy_and_free:
		fpgaDestroyProperties(&props);
		free(errinfos);
		errinfos = NULL;
	}

	return res;

out_destroy:
	fpgaDestroyProperties(&props);

	return res;
}
