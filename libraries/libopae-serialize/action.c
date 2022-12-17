// Copyright(c) 2022, Intel Corporation
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

#include <sys/eventfd.h>

#include <opae/fpga.h>

#include "props.h"
#include "mock/opae_std.h"

#include "action.h"

typedef struct {
	fpga_remote_id handle_id;
	uint64_t len;
	void *buf_addr;
	uint64_t wsid;
	int flags;
} opae_buffer_info;

STATIC opae_buffer_info *
opae_buffer_info_alloc(const fpga_remote_id *handle_id,
		       uint64_t len,
		       void *buf_addr,
		       uint64_t wsid,
		       int flags)
{
	opae_buffer_info *p =
		(opae_buffer_info *)opae_malloc(sizeof(*p));
	if (p) {
		p->handle_id = *handle_id;
		p->len = len;
		p->buf_addr = buf_addr;
		p->wsid = wsid;
		p->flags = flags;
	}
	return p;
}

STATIC void opae_buffer_info_free(opae_buffer_info *p)
{
	opae_free(p);
}

/*
STATIC uint32_t
opae_u64_key_hash(uint32_t num_buckets, uint32_t hash_seed, void *key)
{
	UNUSED_PARAM(hash_seed);
	uint64_t remote_id = (uint64_t)key;
	uint64_t hash = remote_id % 17659;
	return (uint32_t)(hash % num_buckets);
}
*/

STATIC int
opae_str_key_compare(void *keya, void *keyb)
{
	const char *stra = (const char *)keya;
	const char *strb = (const char *)keyb;
	return strncmp(stra, strb, OPAE_MAX_TOKEN_HASH);
}

STATIC void opae_str_key_cleanup(void *key)
{
	opae_free(key);
}

STATIC void opae_buffer_info_value_cleanup(void *value)
{
	opae_buffer_info_free((opae_buffer_info *)value);
}

STATIC int opae_remote_id_to_hash_key(const fpga_remote_id *rid,
				      char *buf,
				      size_t len)
{
	return snprintf(buf, len,
			"0x%016" PRIx64 "@%s",
			rid->unique_id,
			rid->hostname);
}

STATIC char *opae_remote_id_to_hash_key_alloc(const fpga_remote_id *rid)
{
	char buf[OPAE_MAX_TOKEN_HASH];

	if (opae_remote_id_to_hash_key(rid, buf, sizeof(buf)) >=
		OPAE_MAX_TOKEN_HASH) {
		OPAE_ERR("snprintf buffer overflow");
		return NULL;
	}

	return opae_strdup(buf);
}

fpga_result opae_init_remote_context(opae_remote_context *c,
				     opae_poll_server *psrv)
{
	pthread_mutexattr_t mattr;
	fpga_result res;

	c->json_to_string_flags = JSON_C_TO_STRING_SPACED |
				  JSON_C_TO_STRING_PRETTY;

	res = opae_hash_map_init(&c->remote_id_to_token_map,
				 1024, /* num_buckets   */
				 0,    /* hash_seed     */
				 murmur3_32_string_hash,
				 opae_str_key_compare,
				 opae_str_key_cleanup,
				 NULL  /* value_cleanup */);
	if (res)
		return res;

	res = opae_hash_map_init(&c->remote_id_to_handle_map,
				 1024, /* num_buckets   */
				 0,    /* hash_seed     */
				 murmur3_32_string_hash,
				 opae_str_key_compare,
				 opae_str_key_cleanup,
				 NULL  /* value_cleanup */);
	if (res)
		return res;

	res = opae_hash_map_init(&c->remote_id_to_mmio_map,
				 1024, /* num_buckets   */
				 0,    /* hash_seed     */
				 murmur3_32_string_hash,
				 opae_str_key_compare,
				 opae_str_key_cleanup,
				 NULL  /* value_cleanup */);
	if (res)
		return res;

	res = opae_hash_map_init(&c->remote_id_to_buf_info_map,
				 1024, /* num_buckets   */
				 0,    /* hash_seed     */
				 murmur3_32_string_hash,
				 opae_str_key_compare,
				 opae_str_key_cleanup,
				 opae_buffer_info_value_cleanup);
	if (res)
		return res;

	res = opae_hash_map_init(&c->remote_id_to_sysobject_map,
				 1024, /* num_buckets   */
				 0,    /* hash_seed     */
				 murmur3_32_string_hash,
				 opae_str_key_compare,
				 opae_str_key_cleanup,
				 NULL  /* value_cleanup */);
	if (res)
		return res;

	res = opae_hash_map_init(&c->remote_id_to_event_handle_map,
				 1024, /* num_buckets   */
				 0,    /* hash_seed     */
				 murmur3_32_string_hash,
				 opae_str_key_compare,
				 opae_str_key_cleanup,
				 NULL  /* value_cleanup */);
	if (res)
		return res;

	c->psrv = psrv;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&c->events_client_lock, &mattr);
	pthread_mutexattr_destroy(&mattr);

	c->client_initialized = false;
	c->events_client_registrations = 0;

	return FPGA_OK;
}

fpga_result opae_release_remote_context(opae_remote_context *c)
{
	opae_hash_map_destroy(&c->remote_id_to_token_map);
	opae_hash_map_destroy(&c->remote_id_to_handle_map);
	opae_hash_map_destroy(&c->remote_id_to_mmio_map);
	opae_hash_map_destroy(&c->remote_id_to_buf_info_map);
	opae_hash_map_destroy(&c->remote_id_to_sysobject_map);
	opae_hash_map_destroy(&c->remote_id_to_event_handle_map);

	pthread_mutex_destroy(&c->events_client_lock);

	return FPGA_OK;
}

STATIC void request_header_to_response_header(opae_request_header *reqhdr,
					      opae_response_header *resphdr,
					      const char *response_name)
{
	resphdr->request_id = reqhdr->request_id;
	memcpy(resphdr->request_name, reqhdr->request_name,
		sizeof(resphdr->request_name));
	memcpy(resphdr->response_name, response_name,
		strlen(response_name) + 1);
	resphdr->serial = reqhdr->serial;
	opae_get_host_name_buf(resphdr->from, HOST_NAME_MAX);
	memcpy(resphdr->to, reqhdr->from, sizeof(resphdr->to));
}

bool opae_handle_fpgaEnumerate_request_0(opae_remote_context *c,
					 const char *req_json,
					 char **resp_json)
{
	bool res = false;
	uint32_t i;
	fpga_token *tokens = NULL;
	opae_fpgaEnumerate_request req;
	opae_fpgaEnumerate_response resp;

	if (!opae_decode_fpgaEnumerate_request_0(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaEnumerate request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaEnumerate_response_0");

	resp.num_matches = 0;
	resp.tokens = NULL;

	if (req.max_tokens) {
		resp.tokens = opae_calloc(req.max_tokens,
				sizeof(fpga_token_header));
		if (!resp.tokens) {
			OPAE_ERR("calloc failed");
			goto out_cleanup;
		}

		tokens = opae_calloc(req.max_tokens, sizeof(fpga_token));
		if (!tokens) {
			OPAE_ERR("calloc failed");
			goto out_cleanup;
		}
	}

	resp.result = fpgaEnumerate(req.filters,
				    req.num_filters,
				    tokens,
				    req.max_tokens,
				    &resp.num_matches);
	resp.max_tokens = req.max_tokens;

	if (tokens) {
		uint32_t num_tokens = resp.num_matches;

		if (num_tokens > req.max_tokens)
			num_tokens = req.max_tokens;

		// Walk through each token, peek inside, and grab its header.
		for (i = 0 ; i < num_tokens ; ++i) {
			char *hash_key;
			fpga_result result;
			opae_wrapped_token *wt =
				(opae_wrapped_token *)tokens[i];
			fpga_token_header *hdr =
				(fpga_token_header *)wt->opae_token;

			// Place tokens[i] in a map structure keyed by
			// remote_id@hostname from the token header.
			hash_key =
				opae_remote_id_to_hash_key_alloc(
					&hdr->token_id);
			if (!hash_key) {
				OPAE_ERR("strdup failed");
				goto out_cleanup;
			}

			// If we don't have an entry already.
			if (opae_hash_map_find(&c->remote_id_to_token_map,
					       hash_key,
					       NULL)) {

				result = opae_hash_map_add(
						&c->remote_id_to_token_map,
						hash_key, 
						tokens[i]);
				if (result) {
					opae_str_key_cleanup(hash_key);
					goto out_cleanup;
				}

			} else { // tokens[i] is already mapped.
				opae_str_key_cleanup(hash_key);
			}

			resp.tokens[i] = *hdr;
		}
	}

	*resp_json = opae_encode_fpgaEnumerate_response_0(
			&resp,
			c->json_to_string_flags);

	res = true;

out_cleanup:
	if (tokens)
		opae_free(tokens);

	if (resp.tokens)
		opae_free(resp.tokens);

	if (req.filters) {
		for (i = 0 ; i < req.num_filters ; ++i)
			fpgaDestroyProperties(&req.filters[i]);
		opae_free(req.filters);
	}

	return res;
}

bool opae_handle_fpgaDestroyToken_request_1(opae_remote_context *c,
					    const char *req_json,
					    char **resp_json)
{
	bool res = false;
	opae_fpgaDestroyToken_request req;
	opae_fpgaDestroyToken_response resp;
	char hash_key[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;

	if (!opae_decode_fpgaDestroyToken_request_1(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaDestroyToken request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaDestroyToken_response_1");
	resp.result = FPGA_INVALID_PARAM;

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key,
				   sizeof(hash_key));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key);
		*resp_json = opae_encode_fpgaDestroyToken_response_1(
				&resp,
				c->json_to_string_flags);
		goto out;
	}

	// Remove token.
	opae_hash_map_remove(&c->remote_id_to_token_map, hash_key);

	// Destroy token.
	resp.result = fpgaDestroyToken(&token);

	*resp_json = opae_encode_fpgaDestroyToken_response_1(
			&resp,
			c->json_to_string_flags);

	res = true;

out:
	return res;
}

bool opae_handle_fpgaCloneToken_request_2(opae_remote_context *c,
					  const char *req_json,
					  char **resp_json)
{
	bool res = false;
	opae_fpgaCloneToken_request req;
	opae_fpgaCloneToken_response resp;
	char hash_key[OPAE_MAX_TOKEN_HASH];
	fpga_token src_token = NULL;
	fpga_token dest_token = NULL;

	if (!opae_decode_fpgaCloneToken_request_2(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaCloneToken request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaCloneToken_response_2");
	resp.result = FPGA_INVALID_PARAM;
	memset(&resp.dest_token, 0, sizeof(resp.dest_token));

	opae_remote_id_to_hash_key(&req.src_token_id,
				   hash_key,
				   sizeof(hash_key));

	// Find the source token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key,
				&src_token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key);
		*resp_json = opae_encode_fpgaCloneToken_response_2(
				&resp,
				c->json_to_string_flags);
		goto out;
	}

	resp.result = fpgaCloneToken(src_token, &dest_token);
	if (resp.result == FPGA_OK) {
		opae_wrapped_token *wt =
			(opae_wrapped_token *)dest_token;
		fpga_token_header *hdr =
			(fpga_token_header *)wt->opae_token;
		char *dest_hash_key;
		fpga_result result;

		resp.dest_token = *hdr;

		dest_hash_key =
			opae_remote_id_to_hash_key_alloc(
				&resp.dest_token.token_id);

		if (!dest_hash_key) {
			fpgaDestroyToken(&dest_token);
			OPAE_ERR("strdup failed");
			goto out;
		}

		result = opae_hash_map_add(
				&c->remote_id_to_token_map,
				dest_hash_key,
				dest_token);
		if (result) {
			fpgaDestroyToken(&dest_token);
			opae_str_key_cleanup(dest_hash_key);
			goto out;
		}

		res = true;
	} else {
		memset(&resp.dest_token, 0, sizeof(resp.dest_token));
	}

out:
	*resp_json = opae_encode_fpgaCloneToken_response_2(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaGetProperties_request_3(opae_remote_context *c,
					     const char *req_json,
					     char **resp_json)
{
	bool res = false;
	opae_fpgaGetProperties_request req;
	opae_fpgaGetProperties_response resp;
	char hash_key[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;

	if (!opae_decode_fpgaGetProperties_request_3(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetProperties request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetProperties_response_3");

	resp.result = FPGA_INVALID_PARAM;
	resp.properties = NULL;

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key,
				   sizeof(hash_key));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key);
		*resp_json = opae_encode_fpgaGetProperties_response_3(
				&resp,
				c->json_to_string_flags);
		goto out_destroy;
	}

	resp.result = fpgaGetProperties(token, &resp.properties);

	*resp_json = opae_encode_fpgaGetProperties_response_3(
			&resp,
			c->json_to_string_flags);

	res = true;

out_destroy:
	if (resp.properties)
		fpgaDestroyProperties(&resp.properties);
	return res;
}

bool opae_handle_fpgaUpdateProperties_request_4(opae_remote_context *c,
						const char *req_json,
						char **resp_json)
{
	bool res = false;
	opae_fpgaUpdateProperties_request req;
	opae_fpgaUpdateProperties_response resp;
	char hash_key[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;

	if (!opae_decode_fpgaUpdateProperties_request_4(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaUpdateProperties request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaUpdateProperties_response_4");

	resp.result = FPGA_INVALID_PARAM;
	resp.properties = NULL;

	if (fpgaGetProperties(NULL, &resp.properties) != FPGA_OK) {
		*resp_json = opae_encode_fpgaUpdateProperties_response_4(
				&resp,
				c->json_to_string_flags);
		goto out_destroy;
	}

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key,
				   sizeof(hash_key));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key);
		*resp_json = opae_encode_fpgaUpdateProperties_response_4(
				&resp,
				c->json_to_string_flags);
		goto out_destroy;
	}

	resp.result = fpgaUpdateProperties(token, resp.properties);

	*resp_json = opae_encode_fpgaUpdateProperties_response_4(
			&resp,
			c->json_to_string_flags);

	res = true;

out_destroy:
	if (resp.properties)
		fpgaDestroyProperties(&resp.properties);
	return res;
}

bool opae_handle_fpgaOpen_request_5(opae_remote_context *c,
				    const char *req_json,
				    char **resp_json)
{
	bool res = false;
	opae_fpgaOpen_request req;
	opae_fpgaOpen_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaOpen_request_5(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaOpen request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaOpen_response_5");

	resp.result = FPGA_NOT_FOUND;
	memset(&resp.handle, 0, sizeof(fpga_handle_header));

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key_buf,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaOpen(token, &handle, req.flags);

	if (resp.result == FPGA_OK) {
		opae_wrapped_handle *wh =
			(opae_wrapped_handle *)handle;
		fpga_handle_header *hdr =
			(fpga_handle_header *)wh->opae_handle;
		char *hash_key;
		fpga_result result;

		// Place handle in a map structure keyed by
		// remote_id@hostname from the handle header.

		hash_key =
			opae_remote_id_to_hash_key_alloc(
				&hdr->handle_id);
		if (!hash_key) {
			OPAE_ERR("strdup failed");
			goto out_respond;
		}

		// If we don't have an entry already.
		if (opae_hash_map_find(&c->remote_id_to_handle_map,
				       hash_key,
				       NULL)) {

			result = opae_hash_map_add(
					&c->remote_id_to_handle_map,
					hash_key, 
					handle);
			if (result) {
				opae_str_key_cleanup(hash_key);
				goto out_respond;
			}

		} else { // handle is already mapped.
			opae_str_key_cleanup(hash_key);
		}

		resp.handle = *hdr;
		res = true;
	}

out_respond:
	*resp_json = opae_encode_fpgaOpen_response_5(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaClose_request_6(opae_remote_context *c,
				     const char *req_json,
				     char **resp_json)
{
	bool res = false;
	opae_fpgaClose_request req;
	opae_fpgaClose_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaClose_request_6(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaClose request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaClose_response_6");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaClose(handle);

	// Remove the handle from our remote context object.
	opae_hash_map_remove(&c->remote_id_to_handle_map, hash_key_buf);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaClose_response_6(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaReset_request_7(opae_remote_context *c,
				     const char *req_json,
				     char **resp_json)
{
	bool res = false;
	opae_fpgaReset_request req;
	opae_fpgaReset_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaReset_request_7(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaReset request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaReset_response_7");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaReset(handle);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaReset_response_7(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaGetPropertiesFromHandle_request_8(opae_remote_context *c,
				     const char *req_json,
				     char **resp_json)
{
	bool res = false;
	opae_fpgaGetPropertiesFromHandle_request req;
	opae_fpgaGetPropertiesFromHandle_response resp;
	char hash_key[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaGetPropertiesFromHandle_request_8(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetPropertiesFromHandle request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetPropertiesFromHandle_response_8");

	resp.result = FPGA_NOT_FOUND;
	resp.properties = NULL;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key,
				   sizeof(hash_key));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key);
		goto out_respond;
	}

	resp.result = fpgaGetPropertiesFromHandle(handle, &resp.properties);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetPropertiesFromHandle_response_8(
				&resp,
				c->json_to_string_flags);
	if (resp.properties)
		fpgaDestroyProperties(&resp.properties);
	return res;
}

bool opae_handle_fpgaMapMMIO_request_9(opae_remote_context *c,
				       const char *req_json,
				       char **resp_json)
{
	bool res = false;
	opae_fpgaMapMMIO_request req;
	opae_fpgaMapMMIO_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	uint64_t *mmio_ptr = NULL;
	char *hash_key;
	fpga_result result;

	if (!opae_decode_fpgaMapMMIO_request_9(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaMapMMIO request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaMapMMIO_response_9");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaMapMMIO(handle, req.mmio_num, &mmio_ptr);

	// Reserve a unique ID for this MMIO space.
	opae_get_remote_id(&resp.mmio_id);

	hash_key = opae_remote_id_to_hash_key_alloc(&resp.mmio_id);
	if (!hash_key) {
		OPAE_ERR("strdup failed");
		resp.result = FPGA_NO_MEMORY;
		memset(&resp.mmio_id, 0, sizeof(fpga_remote_id));
		goto out_respond;
	}

	result = opae_hash_map_add(&c->remote_id_to_mmio_map,
				   hash_key, 
				   mmio_ptr);
	if (result) {
		resp.result = FPGA_EXCEPTION;
		memset(&resp.mmio_id, 0, sizeof(fpga_remote_id));
		opae_str_key_cleanup(hash_key);
		goto out_respond;
	}

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaMapMMIO_response_9(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaUnmapMMIO_request_10(opae_remote_context *c,
					  const char *req_json,
					  char **resp_json)
{
	bool res = false;
	opae_fpgaUnmapMMIO_request req;
	opae_fpgaUnmapMMIO_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaUnmapMMIO_request_10(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaUnmapMMIO request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaUnmapMMIO_response_10");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaUnmapMMIO(handle, req.mmio_num);

	if (resp.result == FPGA_OK) {
		// Remove the mapping from our remote context.
		opae_remote_id_to_hash_key(&req.mmio_id,
					   hash_key_buf,
					   sizeof(hash_key_buf));

		opae_hash_map_remove(&c->remote_id_to_mmio_map,
				     hash_key_buf);

		res = true;
	}

out_respond:
	*resp_json = opae_encode_fpgaUnmapMMIO_response_10(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaReadMMIO32_request_11(opae_remote_context *c,
					   const char *req_json,
					   char **resp_json)
{
	bool res = false;
	opae_fpgaReadMMIO32_request req;
	opae_fpgaReadMMIO32_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaReadMMIO32_request_11(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaReadMMIO32 request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaReadMMIO32_response_11");

	resp.result = FPGA_EXCEPTION;
	resp.value = 0;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaReadMMIO32(handle,
				     req.mmio_num,
				     req.offset,
				     &resp.value);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaReadMMIO32_response_11(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaWriteMMIO32_request_12(opae_remote_context *c,
					    const char *req_json,
					    char **resp_json)
{
	bool res = false;
	opae_fpgaWriteMMIO32_request req;
	opae_fpgaWriteMMIO32_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaWriteMMIO32_request_12(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaWriteMMIO32 request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaWriteMMIO32_response_12");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaWriteMMIO32(handle,
				      req.mmio_num,
				      req.offset,
				      req.value);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaWriteMMIO32_response_12(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaReadMMIO64_request_13(opae_remote_context *c,
					   const char *req_json,
					   char **resp_json)
{
	bool res = false;
	opae_fpgaReadMMIO64_request req;
	opae_fpgaReadMMIO64_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaReadMMIO64_request_13(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaReadMMIO64 request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaReadMMIO64_response_13");

	resp.result = FPGA_EXCEPTION;
	resp.value = 0;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaReadMMIO64(handle,
				     req.mmio_num,
				     req.offset,
				     &resp.value);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaReadMMIO64_response_13(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaWriteMMIO64_request_14(opae_remote_context *c,
					    const char *req_json,
					    char **resp_json)
{
	bool res = false;
	opae_fpgaWriteMMIO64_request req;
	opae_fpgaWriteMMIO64_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaWriteMMIO64_request_14(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaWriteMMIO64 request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaWriteMMIO64_response_14");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaWriteMMIO64(handle,
				      req.mmio_num,
				      req.offset,
				      req.value);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaWriteMMIO64_response_14(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaWriteMMIO512_request_15(opae_remote_context *c,
					     const char *req_json,
					     char **resp_json)
{
	bool res = false;
	opae_fpgaWriteMMIO512_request req;
	opae_fpgaWriteMMIO512_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaWriteMMIO512_request_15(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaWriteMMIO512 request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaWriteMMIO512_response_15");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaWriteMMIO512(handle,
				       req.mmio_num,
				       req.offset,
				       req.values);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaWriteMMIO512_response_15(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaPrepareBuffer_request_16(opae_remote_context *c,
					      const char *req_json,
					      char **resp_json)
{
	bool res = false;
	opae_fpgaPrepareBuffer_request req;
	opae_fpgaPrepareBuffer_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	void **buf_addr = NULL;
	void *addr = NULL;
	uint64_t wsid = 0;

	if (!opae_decode_fpgaPrepareBuffer_request_16(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaPrepareBuffer request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaPrepareBuffer_response_16");

	resp.result = FPGA_EXCEPTION;
	memset(&resp.buf_id, 0, sizeof(fpga_remote_id));

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	if (req.have_buf_addr) {
		buf_addr = &addr;
		addr = req.pre_allocated_addr;
	}

	resp.result = fpgaPrepareBuffer(handle,
					req.len,
					buf_addr,
					&wsid,
					req.flags);

	if ((resp.result == FPGA_OK) && buf_addr) {
		char *hash_key;
		opae_buffer_info *binfo;
		fpga_result result;

		// Allocate a new remote ID for the buffer.
		opae_get_remote_id(&resp.buf_id);

		hash_key = opae_remote_id_to_hash_key_alloc(&resp.buf_id);
		if (!hash_key) {
			OPAE_ERR("strdup failed");
			resp.result = FPGA_NO_MEMORY;
			goto out_respond;
		}

		binfo = opae_buffer_info_alloc(&req.handle_id,
					       req.len,
					       *buf_addr,
					       wsid,
					       req.flags);
		if (!binfo) {
			OPAE_ERR("malloc failed");
			resp.result = FPGA_NO_MEMORY;
			goto out_respond;
		}

		// Store the new buffer info in our hash map.
		result = opae_hash_map_add(&c->remote_id_to_buf_info_map,
					   hash_key, 
					   binfo);
		if (result) {
			resp.result = FPGA_EXCEPTION;
			opae_str_key_cleanup(hash_key);
			goto out_respond;
		}
	}

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaPrepareBuffer_response_16(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaReleaseBuffer_request_17(opae_remote_context *c,
					      const char *req_json,
					      char **resp_json)
{
	bool res = false;
	opae_fpgaReleaseBuffer_request req;
	opae_fpgaReleaseBuffer_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	opae_buffer_info *binfo = NULL;

	if (!opae_decode_fpgaReleaseBuffer_request_17(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaReleaseBuffer request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaReleaseBuffer_response_17");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find our buffer info struct.
	opae_remote_id_to_hash_key(&req.buf_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	if (opae_hash_map_find(&c->remote_id_to_buf_info_map,
			       hash_key_buf,
			       (void **)&binfo) != FPGA_OK) {
		OPAE_ERR("buffer info lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	if (!opae_remote_ids_match(&req.handle_id, &binfo->handle_id)) {
		OPAE_ERR("invalid handle / wsid combination");
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaReleaseBuffer(handle, binfo->wsid);

	if (resp.result == FPGA_OK) {
		// Remove the binfo from the hash map.
		opae_hash_map_remove(&c->remote_id_to_buf_info_map, hash_key_buf);

		res = true;
	}

out_respond:
	*resp_json = opae_encode_fpgaReleaseBuffer_response_17(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaGetIOAddress_request_18(opae_remote_context *c,
					     const char *req_json,
					     char **resp_json)
{
	bool res = false;
	opae_fpgaGetIOAddress_request req;
	opae_fpgaGetIOAddress_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	opae_buffer_info *binfo = NULL;

	if (!opae_decode_fpgaGetIOAddress_request_18(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetIOAddress request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetIOAddress_response_18");

	resp.result = FPGA_EXCEPTION;
	resp.ioaddr = 0;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find our buffer info struct.
	opae_remote_id_to_hash_key(&req.buf_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	if (opae_hash_map_find(&c->remote_id_to_buf_info_map,
			       hash_key_buf,
			       (void **)&binfo) != FPGA_OK) {
		OPAE_ERR("buffer info lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	if (!opae_remote_ids_match(&req.handle_id, &binfo->handle_id)) {
		OPAE_ERR("invalid handle / wsid combination");
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaGetIOAddress(handle, binfo->wsid, &resp.ioaddr);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetIOAddress_response_18(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaReadError_request_19(opae_remote_context *c,
					  const char *req_json,
					  char **resp_json)
{
	bool res = false;
	opae_fpgaReadError_request req;
	opae_fpgaReadError_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;

	if (!opae_decode_fpgaReadError_request_19(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaReadError request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaReadError_response_19");

	resp.result = FPGA_EXCEPTION;
	resp.value = 0;

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key_buf,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaReadError(token, req.error_num, &resp.value);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaReadError_response_19(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaGetErrorInfo_request_20(opae_remote_context *c,
					     const char *req_json,
					     char **resp_json)
{
	bool res = false;
	opae_fpgaGetErrorInfo_request req;
	opae_fpgaGetErrorInfo_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;

	if (!opae_decode_fpgaGetErrorInfo_request_20(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetErrorInfo request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetErrorInfo_response_20");

	resp.result = FPGA_EXCEPTION;
	memset(&resp.error_info, 0, sizeof(resp.error_info));

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key_buf,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaGetErrorInfo(token, req.error_num, &resp.error_info);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetErrorInfo_response_20(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaClearError_request_21(opae_remote_context *c,
					   const char *req_json,
					   char **resp_json)
{
	bool res = false;
	opae_fpgaClearError_request req;
	opae_fpgaClearError_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;

	if (!opae_decode_fpgaClearError_request_21(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaClearError request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaClearError_response_21");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key_buf,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaClearError(token, req.error_num);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaClearError_response_21(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaClearAllErrors_request_22(opae_remote_context *c,
					   const char *req_json,
					   char **resp_json)
{
	bool res = false;
	opae_fpgaClearAllErrors_request req;
	opae_fpgaClearAllErrors_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;

	if (!opae_decode_fpgaClearAllErrors_request_22(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaClearAllErrors request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaClearAllErrors_response_22");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key_buf,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaClearAllErrors(token);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaClearAllErrors_response_22(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaTokenGetObject_request_23(opae_remote_context *c,
					       const char *req_json,
					       char **resp_json)
{
	bool res = false;
	opae_fpgaTokenGetObject_request req;
	opae_fpgaTokenGetObject_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_token token = NULL;
	fpga_object object = NULL;

	if (!opae_decode_fpgaTokenGetObject_request_23(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaTokenGetObject request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaTokenGetObject_response_23");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.token_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the token in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_token_map,
				hash_key_buf,
				&token) != FPGA_OK) {
		OPAE_ERR("token lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaTokenGetObject(token,
					 req.name,
					 &object,
					 req.flags);

	if (resp.result == FPGA_OK) {
		char *hash_key;
		fpga_result result;

		// Allocate a new remote ID for the object.
		opae_get_remote_id(&resp.object_id);

		hash_key = opae_remote_id_to_hash_key_alloc(&resp.object_id);
		if (!hash_key) {
			OPAE_ERR("strdup failed");
			resp.result = FPGA_NO_MEMORY;
			goto out_respond;
		}

		// Store the new sysobject in our hash map.
		result = opae_hash_map_add(&c->remote_id_to_sysobject_map,
					   hash_key, 
					   object);
		if (result) {
			resp.result = FPGA_EXCEPTION;
			opae_str_key_cleanup(hash_key);
			goto out_respond;
		}
	} else {
		memset(&resp.object_id, 0, sizeof(resp.object_id));
	}

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaTokenGetObject_response_23(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaDestroyObject_request_24(opae_remote_context *c,
					      const char *req_json,
					      char **resp_json)
{
	bool res = false;
	opae_fpgaDestroyObject_request req;
	opae_fpgaDestroyObject_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object object = NULL;

	if (!opae_decode_fpgaDestroyObject_request_24(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaDestroyObject request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaDestroyObject_response_24");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the sysobject in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&object) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaDestroyObject(&object);

	if (resp.result == FPGA_OK) {
		// Remove the sysobject from the hash map.
		opae_hash_map_remove(&c->remote_id_to_sysobject_map,
				     hash_key_buf);

		res = true;
	}

out_respond:
	*resp_json = opae_encode_fpgaDestroyObject_response_24(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaObjectGetType_request_25(opae_remote_context *c,
					      const char *req_json,
					      char **resp_json)
{
	bool res = false;
	opae_fpgaObjectGetType_request req;
	opae_fpgaObjectGetType_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object object = NULL;

	if (!opae_decode_fpgaObjectGetType_request_25(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaObjectGetType request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaObjectGetType_response_25");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the sysobject in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&object) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaObjectGetType(object, &resp.type);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaObjectGetType_response_25(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaObjectGetName_request_26(opae_remote_context *c,
					      const char *req_json,
					      char **resp_json)
{
	bool res = false;
	opae_fpgaObjectGetName_request req;
	opae_fpgaObjectGetName_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object object = NULL;

	if (!opae_decode_fpgaObjectGetName_request_26(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaObjectGetName request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaObjectGetName_response_26");

	resp.result = FPGA_EXCEPTION;
	memset(resp.name, 0, sizeof(resp.name));

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the sysobject in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&object) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaObjectGetName(object, resp.name, sizeof(resp.name));

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaObjectGetName_response_26(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaObjectGetSize_request_27(opae_remote_context *c,
					      const char *req_json,
					      char **resp_json)
{
	bool res = false;
	opae_fpgaObjectGetSize_request req;
	opae_fpgaObjectGetSize_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object object = NULL;

	if (!opae_decode_fpgaObjectGetSize_request_27(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaObjectGetSize request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaObjectGetSize_response_27");

	resp.result = FPGA_EXCEPTION;
	resp.value = 0;

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the sysobject in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&object) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaObjectGetSize(object, &resp.value, req.flags);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaObjectGetSize_response_27(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaObjectRead_request_28(opae_remote_context *c,
					   const char *req_json,
					   char **resp_json)
{
	bool res = false;
	opae_fpgaObjectRead_request req;
	opae_fpgaObjectRead_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object object = NULL;

	if (!opae_decode_fpgaObjectRead_request_28(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaObjectRead request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaObjectRead_response_28");

	resp.result = FPGA_EXCEPTION;
	memset(resp.value, 0, sizeof(resp.value));

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the sysobject in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&object) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaObjectRead(object,
				     (uint8_t *)resp.value,
				     req.offset,
				     req.len,
				     req.flags);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaObjectRead_response_28(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaObjectRead64_request_29(opae_remote_context *c,
					     const char *req_json,
					     char **resp_json)
{
	bool res = false;
	opae_fpgaObjectRead64_request req;
	opae_fpgaObjectRead64_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object object = NULL;

	if (!opae_decode_fpgaObjectRead64_request_29(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaObjectRead64 request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaObjectRead64_response_29");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the sysobject in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&object) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaObjectRead64(object, &resp.value, req.flags);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaObjectRead64_response_29(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaObjectWrite64_request_30(opae_remote_context *c,
					      const char *req_json,
					      char **resp_json)
{
	bool res = false;
	opae_fpgaObjectWrite64_request req;
	opae_fpgaObjectWrite64_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object object = NULL;

	if (!opae_decode_fpgaObjectWrite64_request_30(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaObjectWrite64 request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaObjectWrite64_response_30");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the sysobject in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&object) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaObjectWrite64(object, req.value, req.flags);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaObjectWrite64_response_30(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaHandleGetObject_request_31(opae_remote_context *c,
						const char *req_json,
						char **resp_json)
{
	bool res = false;
	opae_fpgaHandleGetObject_request req;
	opae_fpgaHandleGetObject_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	fpga_object object = NULL;

	if (!opae_decode_fpgaHandleGetObject_request_31(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaHandleGetObject request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaHandleGetObject_response_31");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaHandleGetObject(handle,
					  req.name,
					  &object,
					  req.flags);

	if (resp.result == FPGA_OK) {
		char *hash_key;
		fpga_result result;

		// Allocate a new remote ID for the object.
		opae_get_remote_id(&resp.object_id);

		hash_key = opae_remote_id_to_hash_key_alloc(&resp.object_id);
		if (!hash_key) {
			OPAE_ERR("strdup failed");
			resp.result = FPGA_NO_MEMORY;
			goto out_respond;
		}

		// Store the new sysobject in our hash map.
		result = opae_hash_map_add(&c->remote_id_to_sysobject_map,
					   hash_key, 
					   object);
		if (result) {
			resp.result = FPGA_EXCEPTION;
			opae_str_key_cleanup(hash_key);
			goto out_respond;
		}
	} else {
		memset(&resp.object_id, 0, sizeof(resp.object_id));
	}

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaHandleGetObject_response_31(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaObjectGetObject_request_32(opae_remote_context *c,
						const char *req_json,
						char **resp_json)
{
	bool res = false;
	opae_fpgaObjectGetObject_request req;
	opae_fpgaObjectGetObject_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object parent = NULL;
	fpga_object child = NULL;

	if (!opae_decode_fpgaObjectGetObject_request_32(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaObjectGetObject request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaObjectGetObject_response_32");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the parent in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&parent) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaObjectGetObject(parent,
					  req.name,
					  &child,
					  req.flags);

	if (resp.result == FPGA_OK) {
		char *hash_key;
		fpga_result result;

		// Allocate a new remote ID for the child.
		opae_get_remote_id(&resp.object_id);

		hash_key = opae_remote_id_to_hash_key_alloc(&resp.object_id);
		if (!hash_key) {
			OPAE_ERR("strdup failed");
			resp.result = FPGA_NO_MEMORY;
			goto out_respond;
		}

		// Store the new sysobject in our hash map.
		result = opae_hash_map_add(&c->remote_id_to_sysobject_map,
					   hash_key, 
					   child);
		if (result) {
			resp.result = FPGA_EXCEPTION;
			opae_str_key_cleanup(hash_key);
			goto out_respond;
		}
	}

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaObjectGetObject_response_32(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaObjectGetObjectAt_request_33(opae_remote_context *c,
						  const char *req_json,
						  char **resp_json)
{
	bool res = false;
	opae_fpgaObjectGetObjectAt_request req;
	opae_fpgaObjectGetObjectAt_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_object parent = NULL;
	fpga_object child = NULL;

	if (!opae_decode_fpgaObjectGetObjectAt_request_33(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaObjectGetObjectAt request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaObjectGetObjectAt_response_33");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.object_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the parent in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_sysobject_map,
				hash_key_buf,
				&parent) != FPGA_OK) {
		OPAE_ERR("sysobject lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaObjectGetObjectAt(parent,
					    req.idx,
					    &child);

	if (resp.result == FPGA_OK) {
		char *hash_key;
		fpga_result result;

		// Allocate a new remote ID for the child.
		opae_get_remote_id(&resp.object_id);

		hash_key = opae_remote_id_to_hash_key_alloc(&resp.object_id);
		if (!hash_key) {
			OPAE_ERR("strdup failed");
			resp.result = FPGA_NO_MEMORY;
			goto out_respond;
		}

		// Store the new sysobject in our hash map.
		result = opae_hash_map_add(&c->remote_id_to_sysobject_map,
					   hash_key, 
					   child);
		if (result) {
			resp.result = FPGA_EXCEPTION;
			opae_str_key_cleanup(hash_key);
			goto out_respond;
		}
	}

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaObjectGetObjectAt_response_33(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaSetUserClock_request_34(opae_remote_context *c,
					     const char *req_json,
					     char **resp_json)
{
	bool res = false;
	opae_fpgaSetUserClock_request req;
	opae_fpgaSetUserClock_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaSetUserClock_request_34(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaSetUserClock request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaSetUserClock_response_34");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaSetUserClock(handle,
				       req.high_clk,
				       req.low_clk,
				       req.flags);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaSetUserClock_response_34(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaGetUserClock_request_35(opae_remote_context *c,
					     const char *req_json,
					     char **resp_json)
{
	bool res = false;
	opae_fpgaGetUserClock_request req;
	opae_fpgaGetUserClock_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaGetUserClock_request_35(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetUserClock request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetUserClock_response_35");

	resp.result = FPGA_EXCEPTION;
	resp.high_clk = 0;
	resp.low_clk = 0;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaGetUserClock(handle,
				       &resp.high_clk,
				       &resp.low_clk,
				       req.flags);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetUserClock_response_35(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaGetNumMetrics_request_36(opae_remote_context *c,
					      const char *req_json,
					      char **resp_json)
{
	bool res = false;
	opae_fpgaGetNumMetrics_request req;
	opae_fpgaGetNumMetrics_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaGetNumMetrics_request_36(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetNumMetrics request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetNumMetrics_response_36");

	resp.result = FPGA_EXCEPTION;
	resp.num_metrics = 0;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaGetNumMetrics(handle, &resp.num_metrics);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetNumMetrics_response_36(
			&resp,
			c->json_to_string_flags);
	return res;
}

bool opae_handle_fpgaGetMetricsInfo_request_37(opae_remote_context *c,
					       const char *req_json,
					       char **resp_json)
{
	bool res = false;
	opae_fpgaGetMetricsInfo_request req;
	opae_fpgaGetMetricsInfo_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	uint64_t num_metrics;

	if (!opae_decode_fpgaGetMetricsInfo_request_37(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetMetricsInfo request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetMetricsInfo_response_37");

	resp.result = FPGA_EXCEPTION;
	resp.info = NULL;
	resp.num_metrics = 0;
	num_metrics = req.num_metrics;

	if (req.num_metrics) {
		resp.info = opae_calloc(req.num_metrics,
				sizeof(fpga_metric_info));
		if (!resp.info) {
			OPAE_ERR("calloc failed");
			goto out_respond;
		}
	}

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaGetMetricsInfo(handle,
					 resp.info,
					 &num_metrics);

	if (resp.result == FPGA_OK)
		resp.num_metrics = num_metrics;

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetMetricsInfo_response_37(
			&resp,
			c->json_to_string_flags);

	if (resp.info)
		opae_free(resp.info);

	return res;
}

bool opae_handle_fpgaGetMetricsByIndex_request_38(opae_remote_context *c,
						  const char *req_json,
						  char **resp_json)
{
	bool res = false;
	opae_fpgaGetMetricsByIndex_request req;
	opae_fpgaGetMetricsByIndex_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaGetMetricsByIndex_request_38(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetMetricsByIndex request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetMetricsByIndex_response_38");

	resp.result = FPGA_EXCEPTION;
	resp.metrics = NULL;
	resp.num_metric_indexes = req.num_metric_indexes;

	if (req.num_metric_indexes) {
		resp.metrics = opae_calloc(req.num_metric_indexes,
				sizeof(fpga_metric));
		if (!resp.metrics) {
			OPAE_ERR("calloc failed");
			goto out_respond;
		}
	}

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaGetMetricsByIndex(handle,
					    req.metric_num,
					    req.num_metric_indexes,
					    resp.metrics);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetMetricsByIndex_response_38(
			&resp,
			c->json_to_string_flags);

	if (req.metric_num)
		opae_free(req.metric_num);

	if (resp.metrics)
		opae_free(resp.metrics);

	return res;
}

bool opae_handle_fpgaGetMetricsByName_request_39(opae_remote_context *c,
						 const char *req_json,
						 char **resp_json)
{
	bool res = false;
	opae_fpgaGetMetricsByName_request req;
	opae_fpgaGetMetricsByName_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	uint64_t i;

	if (!opae_decode_fpgaGetMetricsByName_request_39(req_json, &req)) {
		OPAE_ERR("failed to decode fpgaGetMetricsByName request");
		return false;
	}

	request_header_to_response_header(&req.header,
					  &resp.header,
					  "fpgaGetMetricsByName_response_39");

	resp.result = FPGA_EXCEPTION;
	resp.metrics = NULL;
	resp.num_metric_names = req.num_metric_names;

	if (req.num_metric_names) {
		resp.metrics = opae_calloc(req.num_metric_names,
				sizeof(fpga_metric));
		if (!resp.metrics) {
			OPAE_ERR("calloc failed");
			goto out_respond;
		}
	}

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaGetMetricsByName(handle,
					   req.metrics_names,
					   req.num_metric_names,
					   resp.metrics);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetMetricsByName_response_39(
			&resp,
			c->json_to_string_flags);

	if (req.metrics_names) {
		for (i = 0 ; i < req.num_metric_names ; ++i)
			opae_free(req.metrics_names[i]);

		opae_free(req.metrics_names);
	}

	if (resp.metrics)
		opae_free(resp.metrics);

	return res;
}

bool opae_handle_fpgaGetMetricsThresholdInfo_request_40(opae_remote_context *c,
							const char *req_json,
							char **resp_json)
{
	bool res = false;
	opae_fpgaGetMetricsThresholdInfo_request req;
	opae_fpgaGetMetricsThresholdInfo_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	uint32_t num_thresholds;

	if (!opae_decode_fpgaGetMetricsThresholdInfo_request_40(req_json,
								&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaGetMetricsThresholdInfo request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaGetMetricsThresholdInfo_response_40");

	resp.result = FPGA_EXCEPTION;
	resp.metric_threshold = NULL;
	num_thresholds = req.num_thresholds;
	resp.num_thresholds = 0;

	if (req.num_thresholds) {
		resp.metric_threshold = opae_calloc(req.num_thresholds,
				sizeof(metric_threshold));
		if (!resp.metric_threshold) {
			OPAE_ERR("calloc failed");
			goto out_respond;
		}
	}

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaGetMetricsThresholdInfo(handle,
				resp.metric_threshold,
				&num_thresholds);

	if (resp.result == FPGA_OK)
		resp.num_thresholds = num_thresholds;

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetMetricsThresholdInfo_response_40(
			&resp,
			c->json_to_string_flags);

	if (resp.metric_threshold)
		opae_free(resp.metric_threshold);

	return res;
}

bool opae_handle_fpgaReconfigureSlotByName_request_41(opae_remote_context *c,
						      const char *req_json,
						      char **resp_json)
{
	bool res = false;
	opae_fpgaReconfigureSlotByName_request req;
	opae_fpgaReconfigureSlotByName_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;

	if (!opae_decode_fpgaReconfigureSlotByName_request_41(req_json,
							      &req)) {
		OPAE_ERR("failed to decode "
			 "fpgaReconfigureSlotByName request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaReconfigureSlotByName_response_41");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		goto out_respond;
	}

	resp.result = fpgaReconfigureSlotByName(handle,
						req.slot,
						req.path,
						req.flags);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaReconfigureSlotByName_response_41(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaBufMemSet_request_42(opae_remote_context *c,
					  const char *req_json,
					  char **resp_json)
{
	bool res = false;
	opae_fpgaBufMemSet_request req;
	opae_fpgaBufMemSet_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	opae_buffer_info *binfo = NULL;

	if (!opae_decode_fpgaBufMemSet_request_42(req_json, &req)) {
		OPAE_ERR("failed to decode "
			 "fpgaBufMemSet request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaBufMemSet_response_42");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find our buffer info struct.
	opae_remote_id_to_hash_key(&req.buf_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	if (opae_hash_map_find(&c->remote_id_to_buf_info_map,
			       hash_key_buf,
			       (void **)&binfo) != FPGA_OK) {
		OPAE_ERR("buffer info lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	if (!opae_remote_ids_match(&req.handle_id, &binfo->handle_id)) {
		OPAE_ERR("invalid handle / wsid combination");
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaBufMemSet(handle,
				    binfo->wsid,
				    req.offset,
				    req.c,
				    req.n);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaBufMemSet_response_42(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaBufMemCpyToRemote_request_43(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaBufMemCpyToRemote_request req;
	opae_fpgaBufMemCpyToRemote_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	opae_buffer_info *binfo = NULL;

	if (!opae_decode_fpgaBufMemCpyToRemote_request_43(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaBufMemCpyToRemote request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaBufMemCpyToRemote_response_43");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find our buffer info struct.
	opae_remote_id_to_hash_key(&req.dest_buf_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	if (opae_hash_map_find(&c->remote_id_to_buf_info_map,
			       hash_key_buf,
			       (void **)&binfo) != FPGA_OK) {
		OPAE_ERR("buffer info lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	if (!opae_remote_ids_match(&req.handle_id, &binfo->handle_id)) {
		OPAE_ERR("invalid handle / wsid combination");
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaBufMemCpyToRemote(handle,
					    binfo->wsid,
					    req.dest_offset,
					    req.src,
					    req.n);

	res = true;

out_respond:
	if (req.src)
		opae_free(req.src);

	*resp_json = opae_encode_fpgaBufMemCpyToRemote_response_43(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaBufPoll_request_44(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaBufPoll_request req;
	opae_fpgaBufPoll_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	opae_buffer_info *binfo = NULL;

	if (!opae_decode_fpgaBufPoll_request_44(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaBufPoll request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaBufPoll_response_44");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find our buffer info struct.
	opae_remote_id_to_hash_key(&req.buf_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	if (opae_hash_map_find(&c->remote_id_to_buf_info_map,
			       hash_key_buf,
			       (void **)&binfo) != FPGA_OK) {
		OPAE_ERR("buffer info lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	if (!opae_remote_ids_match(&req.handle_id, &binfo->handle_id)) {
		OPAE_ERR("invalid handle / wsid combination");
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaBufPoll(handle,
				  binfo->wsid,
				  req.offset,
				  req.width,
				  req.mask,
				  req.expected_value,
				  req.sleep_interval,
				  req.loops_timeout);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaBufPoll_response_44(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaBufMemCmp_request_45(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaBufMemCmp_request req;
	opae_fpgaBufMemCmp_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	opae_buffer_info *binfo_a = NULL;
	opae_buffer_info *binfo_b = NULL;

	if (!opae_decode_fpgaBufMemCmp_request_45(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaBufMemCmp request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaBufMemCmp_response_45");

	resp.result = FPGA_EXCEPTION;
	resp.cmp_result = -1;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find our buffer info struct for bufa.
	opae_remote_id_to_hash_key(&req.bufa_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	if (opae_hash_map_find(&c->remote_id_to_buf_info_map,
			       hash_key_buf,
			       (void **)&binfo_a) != FPGA_OK) {
		OPAE_ERR("buffer info lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	if (!opae_remote_ids_match(&req.handle_id, &binfo_a->handle_id)) {
		OPAE_ERR("invalid handle / wsid combination");
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find our buffer info struct for bufb.
	opae_remote_id_to_hash_key(&req.bufb_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	if (opae_hash_map_find(&c->remote_id_to_buf_info_map,
			       hash_key_buf,
			       (void **)&binfo_b) != FPGA_OK) {
		OPAE_ERR("buffer info lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	if (!opae_remote_ids_match(&req.handle_id, &binfo_b->handle_id)) {
		OPAE_ERR("invalid handle / wsid combination");
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaBufMemCmp(handle,
				    binfo_a->wsid,
				    req.bufa_offset,
				    binfo_b->wsid,
				    req.bufb_offset,
				    req.n,
				    &resp.cmp_result);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaBufMemCmp_response_45(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaBufWritePattern_request_46(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaBufWritePattern_request req;
	opae_fpgaBufWritePattern_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	opae_buffer_info *binfo = NULL;

	if (!opae_decode_fpgaBufWritePattern_request_46(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaBufWritePattern request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaBufWritePattern_response_46");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find our buffer info struct.
	opae_remote_id_to_hash_key(&req.buf_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	if (opae_hash_map_find(&c->remote_id_to_buf_info_map,
			       hash_key_buf,
			       (void **)&binfo) != FPGA_OK) {
		OPAE_ERR("buffer info lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	if (!opae_remote_ids_match(&req.handle_id, &binfo->handle_id)) {
		OPAE_ERR("invalid handle / wsid combination");
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaBufWritePattern(handle,
					  binfo->wsid,
					  req.pattern_name);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaBufWritePattern_response_46(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaCreateEventHandle_request_47(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaCreateEventHandle_request req;
	opae_fpgaCreateEventHandle_response resp;
	fpga_event_handle event_handle = NULL;

	if (!opae_decode_fpgaCreateEventHandle_request_47(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaCreateEventHandle request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaCreateEventHandle_response_47");

	memset(&resp.eh_id, 0, sizeof(fpga_remote_id));

	resp.result = fpgaCreateEventHandle(&event_handle);

	if (resp.result == FPGA_OK) {
		char *hash_key;
		fpga_result result;

		// Allocate a new remote ID for the event handle.
		opae_get_remote_id(&resp.eh_id);

		hash_key = opae_remote_id_to_hash_key_alloc(&resp.eh_id);
		if (!hash_key) {
			OPAE_ERR("strdup failed");
			resp.result = FPGA_NO_MEMORY;
			goto out_respond;
		}

		// Store the new event handle in our hash map.
		result = opae_hash_map_add(&c->remote_id_to_event_handle_map,
					   hash_key,
					   event_handle);
		if (result) {
			resp.result = FPGA_EXCEPTION;
			opae_str_key_cleanup(hash_key);
			goto out_respond;
		}
	}

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaCreateEventHandle_response_47(
			&resp,
			c->json_to_string_flags);

	return res;
}

fpga_result opae_server_send_and_receive(
	opae_remote_context *c,
	char *req_json,
	char **resp_json)
{
	static char recvbuf[OPAE_RECEIVE_BUF_MAX];
	size_t len;
	ssize_t slen;

	if (!req_json)
		return FPGA_NO_MEMORY;

	len = strlen(req_json);

	slen = c->events_client_ifc.send(c->events_client_ifc.connection,
					 req_json,
					 len + 1);

	if (slen < 0) {
		opae_free(req_json);
		return FPGA_EXCEPTION;
	}

	opae_free(req_json);

	slen = c->events_client_ifc.receive(c->events_client_ifc.connection,
					    recvbuf,
					    sizeof(recvbuf));
	if (slen < 0)
		return FPGA_EXCEPTION;

	*resp_json = recvbuf;

	return FPGA_OK;
}

bool opae_handle_fpgaRegisterEvent_request_48(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaRegisterEvent_request req;
	opae_fpgaRegisterEvent_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	fpga_event_handle event_handle = NULL;
	int server_event_fd = -1;

	fpga_result result;
	char *events_req_json;
	char *events_resp_json = NULL;
	opae_get_remote_event_id_request events_req;
	opae_get_remote_event_id_response events_resp;

	req.events_data = NULL;

	if (!opae_decode_fpgaRegisterEvent_request_48(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaRegisterEvent request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaRegisterEvent_response_48");

	resp.result = FPGA_EXCEPTION;
	resp.client_event_fd = -1;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	opae_remote_id_to_hash_key(&req.eh_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the event_handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_event_handle_map,
				hash_key_buf,
				&event_handle) != FPGA_OK) {
		OPAE_ERR("event_handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaRegisterEvent(handle,
					req.event_type,
					event_handle,
					req.flags);

	fpgaGetOSObjectFromEventHandle(event_handle, &server_event_fd);

	if (resp.result == FPGA_OK) {
		// Initialize and open our client connection to the
		// events server, if it has not already been.
		int ires = 0;

		opae_mutex_lock(ires, &c->events_client_lock);

		if (!c->client_initialized) {

			if (req.client_type == OPAE_CLIENT_UDS) {
				opae_events_uds_data *uds_data =
					(opae_events_uds_data *)req.events_data;

				ires = opae_uds_ifc_init(&c->events_client_ifc,
							 uds_data->events_socket,
							 0,
							 0);

				if (ires) {
					OPAE_ERR("failed to initialize "
						 "connection to events server");
				} else {
					ires = c->events_client_ifc.open(
						c->events_client_ifc.connection);
					if (ires) {
						OPAE_ERR("failed to open "
							 "connection to "
							 "events server");
					} else {
						c->client_initialized = true;
					}

				}
			} else if (req.client_type == OPAE_CLIENT_INET) {
				opae_events_inet_data *inet_data =
					(opae_events_inet_data *)req.events_data;
				char ip_addr[INET_ADDRSTRLEN];
				struct sockaddr_in sain;

				memset(&sain, 0, sizeof(sain));

				sain.sin_addr.s_addr = c->client_address;
				sain.sin_port = htons(inet_data->events_port);

				inet_ntop(AF_INET, &sain.sin_addr, ip_addr, sizeof(ip_addr));

				ires = opae_inet_ifc_init(&c->events_client_ifc,
							  ip_addr,
							  inet_data->events_port,
							  0,
							  0);
							
				if (ires) {
					OPAE_ERR("failed to initialize "
						 "connection to events server");
				} else {
					ires = c->events_client_ifc.open(
						c->events_client_ifc.connection);
					if (ires) {
						OPAE_ERR("failed to open "
							 "connection to "
							 "events server");
					} else {
						c->client_initialized = true;
					}

				}
			}

		}

		c->events_client_registrations++;
		opae_mutex_unlock(ires, &c->events_client_lock);

		events_req_json = opae_encode_get_remote_event_id_request_0(
					&events_req, 
					c->json_to_string_flags);

		result = opae_server_send_and_receive(c,
						      events_req_json,
						      &events_resp_json);
		if (result)
			goto out_respond;

		if (!opae_decode_get_remote_event_id_response_0(
			events_resp_json, &events_resp))
			goto out_respond;

		if (events_resp.result == FPGA_OK) {
			opae_server_event_context *event_context;
			nfds_t i;

			resp.client_event_fd = events_resp.client_event_fd;

			event_context = opae_malloc(sizeof(*event_context));
			if (!event_context) {
				OPAE_ERR("malloc() failed");
				goto out_respond;
			}

			event_context->remote_context = c;
			event_context->event_id = events_resp.event_id;

			i = c->psrv->num_fds;

			c->psrv->pollfds[i].fd = server_event_fd;
			c->psrv->pollfds[i].events = POLLIN | POLLPRI;

			c->psrv->init_event(c->psrv,
					    &c->psrv->handlers[i],
					    i,
					    event_context,
					    sizeof(opae_server_event_context));

			++c->psrv->num_fds;
		}
	}

	res = true;

out_respond:
	if (req.events_data)
		opae_free(req.events_data);

	*resp_json = opae_encode_fpgaRegisterEvent_response_48(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaUnregisterEvent_request_49(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaUnregisterEvent_request req;
	opae_fpgaUnregisterEvent_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_handle handle = NULL;
	fpga_event_handle event_handle = NULL;

	int server_event_fd = -1;
	nfds_t i;
	fpga_result result;
	char *events_req_json;
	char *events_resp_json = NULL;
	opae_release_remote_event_request events_req;
	opae_release_remote_event_response events_resp;
	opae_server_event_context *event_context = NULL;
	int ires;

	if (!opae_decode_fpgaUnregisterEvent_request_49(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaUnregisterEvent request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaUnregisterEvent_response_49");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.handle_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_handle_map,
				hash_key_buf,
				&handle) != FPGA_OK) {
		OPAE_ERR("handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	opae_remote_id_to_hash_key(&req.eh_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the event_handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_event_handle_map,
				hash_key_buf,
				&event_handle) != FPGA_OK) {
		OPAE_ERR("event_handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Find the server-side event fd.

	result = fpgaGetOSObjectFromEventHandle(event_handle, &server_event_fd);
	if ((result != FPGA_OK) || (server_event_fd < 0))
		goto unregister; // no fd.

	for (i = OPAE_POLLSRV_FIRST_CLIENT_SOCKET ;
		i < c->psrv->num_fds ;
			++i) {
		if (c->psrv->pollfds[i].fd == server_event_fd)
			break; // found it
	}

	if (i == c->psrv->num_fds)
		goto unregister; // not found

	event_context = (opae_server_event_context *)
		c->psrv->handlers[i].client_or_event_context;

	if (!event_context) // strange..
		goto unregister;

	// Tell the events server side to remove its tracking for
	// the requested event.
	events_req.event_id = event_context->event_id;

	events_req_json = opae_encode_release_remote_event_request_2(
				&events_req,
				c->json_to_string_flags);

	result = opae_server_send_and_receive(c,
					      events_req_json,
					      &events_resp_json);

	if (result)
		goto unregister;

	if (!opae_decode_release_remote_event_response_2(
		events_resp_json, &events_resp))
		goto unregister;

	// Remove the client side event.
	opae_poll_server_close_client(c->psrv, server_event_fd);

	opae_mutex_lock(ires, &c->events_client_lock);

	if (c->events_client_registrations > 0) {
		--c->events_client_registrations;

		if (!c->events_client_registrations) {
			c->events_client_ifc.close(
				c->events_client_ifc.connection);
			c->events_client_ifc.release(
				c->events_client_ifc.connection);

			c->events_client_ifc.connection = NULL;

			c->client_initialized = false;
		}
	}

	opae_mutex_unlock(ires, &c->events_client_lock);

unregister:
	resp.result = fpgaUnregisterEvent(handle,
					  req.event_type,
					  event_handle);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaUnregisterEvent_response_49(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaGetOSObjectFromEventHandle_request_50(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaGetOSObjectFromEventHandle_request req;
	opae_fpgaGetOSObjectFromEventHandle_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_event_handle event_handle = NULL;

	if (!opae_decode_fpgaGetOSObjectFromEventHandle_request_50(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaGetOSObjectFromEventHandle request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaGetOSObjectFromEventHandle_response_50");

	resp.result = FPGA_EXCEPTION;
	resp.fd = -1;

	opae_remote_id_to_hash_key(&req.eh_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the event_handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_event_handle_map,
				hash_key_buf,
				&event_handle) != FPGA_OK) {
		OPAE_ERR("event_handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaGetOSObjectFromEventHandle(event_handle, &resp.fd);

	res = true;

out_respond:
	*resp_json = opae_encode_fpgaGetOSObjectFromEventHandle_response_50(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_fpgaDestroyEventHandle_request_51(
	opae_remote_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_fpgaDestroyEventHandle_request req;
	opae_fpgaDestroyEventHandle_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	fpga_event_handle event_handle = NULL;

	if (!opae_decode_fpgaDestroyEventHandle_request_51(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "fpgaDestroyEventHandle request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"fpgaDestroyEventHandle_response_51");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.eh_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the event_handle in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_event_handle_map,
				hash_key_buf,
				&event_handle) != FPGA_OK) {
		OPAE_ERR("event_handle lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	resp.result = fpgaDestroyEventHandle(&event_handle);

	if (resp.result == FPGA_OK) {
		// Remove the event_handle from our remote context object.
		opae_hash_map_remove(
			&c->remote_id_to_event_handle_map, hash_key_buf);

		res = true;
	}

out_respond:
	*resp_json = opae_encode_fpgaDestroyEventHandle_response_51(
			&resp,
			c->json_to_string_flags);

	return res;
}

/******************************************************************************/

typedef bool (*client_handler)(opae_remote_context *c,
			       const char *req_json,
			       char **resp_json);

STATIC client_handler client_handlers[] = {
	opae_handle_fpgaEnumerate_request_0,
	opae_handle_fpgaDestroyToken_request_1,
	opae_handle_fpgaCloneToken_request_2,
	opae_handle_fpgaGetProperties_request_3,
	opae_handle_fpgaUpdateProperties_request_4,
	opae_handle_fpgaOpen_request_5,
	opae_handle_fpgaClose_request_6,
	opae_handle_fpgaReset_request_7,
	opae_handle_fpgaGetPropertiesFromHandle_request_8,
	opae_handle_fpgaMapMMIO_request_9,
	opae_handle_fpgaUnmapMMIO_request_10,
	opae_handle_fpgaReadMMIO32_request_11,
	opae_handle_fpgaWriteMMIO32_request_12,
	opae_handle_fpgaReadMMIO64_request_13,
	opae_handle_fpgaWriteMMIO64_request_14,
	opae_handle_fpgaWriteMMIO512_request_15,
	opae_handle_fpgaPrepareBuffer_request_16,
	opae_handle_fpgaReleaseBuffer_request_17,
	opae_handle_fpgaGetIOAddress_request_18,
	opae_handle_fpgaReadError_request_19,
	opae_handle_fpgaGetErrorInfo_request_20,
	opae_handle_fpgaClearError_request_21,
	opae_handle_fpgaClearAllErrors_request_22,
	opae_handle_fpgaTokenGetObject_request_23,
	opae_handle_fpgaDestroyObject_request_24,
	opae_handle_fpgaObjectGetType_request_25,
	opae_handle_fpgaObjectGetName_request_26,
	opae_handle_fpgaObjectGetSize_request_27,
	opae_handle_fpgaObjectRead_request_28,
	opae_handle_fpgaObjectRead64_request_29,
	opae_handle_fpgaObjectWrite64_request_30,
	opae_handle_fpgaHandleGetObject_request_31,
	opae_handle_fpgaObjectGetObject_request_32,
	opae_handle_fpgaObjectGetObjectAt_request_33,
	opae_handle_fpgaSetUserClock_request_34,
	opae_handle_fpgaGetUserClock_request_35,
	opae_handle_fpgaGetNumMetrics_request_36,
	opae_handle_fpgaGetMetricsInfo_request_37,
	opae_handle_fpgaGetMetricsByIndex_request_38,
	opae_handle_fpgaGetMetricsByName_request_39,
	opae_handle_fpgaGetMetricsThresholdInfo_request_40,
	opae_handle_fpgaReconfigureSlotByName_request_41,
	opae_handle_fpgaBufMemSet_request_42,
	opae_handle_fpgaBufMemCpyToRemote_request_43,
	opae_handle_fpgaBufPoll_request_44,
	opae_handle_fpgaBufMemCmp_request_45,
	opae_handle_fpgaBufWritePattern_request_46,
	opae_handle_fpgaCreateEventHandle_request_47,
	opae_handle_fpgaRegisterEvent_request_48,
	opae_handle_fpgaUnregisterEvent_request_49,
	opae_handle_fpgaGetOSObjectFromEventHandle_request_50,
	opae_handle_fpgaDestroyEventHandle_request_51
};

bool opae_remote_handle_client_request(opae_remote_context *c,
				       const char *req_json,
				       char **resp_json)
{
	struct json_object *root = NULL;
	enum json_tokener_error j_err = json_tokener_success;
	opae_request_header header;

	root = json_tokener_parse_verbose(req_json, &j_err);
	if (!root) {
		OPAE_ERR("JSON parse failed: %s",
			 json_tokener_error_desc(j_err));
		return false;
	}

	if (!opae_decode_request_header_obj(root, &header)) {
		OPAE_ERR("request header decode failed");
		json_object_put(root);
		return false;
	}

	json_object_put(root);

	if (header.request_id >=
		(sizeof(client_handlers) / sizeof(client_handlers[0]))) {
		// Deal with out-of-bounds request_id.

		return false;
	}

	return client_handlers[header.request_id](c, req_json, resp_json);
}

/******************************************************************************/

int opae_poll_server_handle_client(opae_poll_server *psrv,
				   void *client_ctx,
				   int client_socket)
{
	char buf[OPAE_RECEIVE_BUF_MAX];
	ssize_t n;
	opae_remote_context *remc = (opae_remote_context *)client_ctx;
	char *response_json = NULL;
	bool res;

	n = chunked_recv(client_socket, buf, sizeof(buf), 0);
	if (n == -2) {
		OPAE_DBG("peer closed connection");
		opae_poll_server_close_client(psrv, client_socket);
		return 0;
	} else if (n < 0) {
		OPAE_ERR("recv() failed: %s", strerror(errno));
		return (int)n;
	}

	res = opae_remote_handle_client_request(remc, buf, &response_json);
	if (res && response_json) {
		chunked_send(client_socket,
			     response_json,
			     strlen(response_json) + 1,
			     0);
		opae_free(response_json);
		return 0;
	}

	if (response_json)
		opae_free(response_json);

	return 1;
}

int opae_poll_server_init_client(opae_poll_server *psrv,
				 opae_poll_server_handler *handler,
				 nfds_t i,
				 void *extra,
				 socklen_t len)
{
	UNUSED_PARAM(i);
	UNUSED_PARAM(extra);
	UNUSED_PARAM(len);
	fpga_result res;
	opae_remote_context *remc;

	remc = opae_malloc(sizeof(opae_remote_context));
	if (!remc) {
		OPAE_ERR("malloc() failed");
		return 1;
	}

	res = opae_init_remote_context(remc, psrv);
	if (res) {
		OPAE_ERR("failed to init remote context");
		return 2;
	}

	remc->client_address = INADDR_ANY;
	remc->client_port = 0;

	if (psrv->type == OPAE_SERVER_INET) {
		struct sockaddr *sa =
			(struct sockaddr *)extra;

		if (sa->sa_family == AF_INET) {
			struct sockaddr_in *sain =
				(struct sockaddr_in *)sa;
			char addr[INET_ADDRSTRLEN];
			uint16_t port;

			remc->client_address = sain->sin_addr.s_addr;
			remc->client_port = 0;

			inet_ntop(AF_INET, &sain->sin_addr, addr, sizeof(addr));
			port = ntohs(sain->sin_port);

			printf("accepting connection from %s:%u\n", addr, port);
		}
	}

	handler->client_or_event_handler = psrv->handle_client_message;
	handler->client_or_event_release = psrv->release_client;
	handler->client_or_event_context = remc;

	return 0;
}

int opae_poll_server_release_client(opae_poll_server *psrv,
				    opae_poll_server_handler *handler,
				    nfds_t i)
{
	UNUSED_PARAM(psrv);
	UNUSED_PARAM(i);
	opae_remote_context *remc =
		(opae_remote_context *)handler->client_or_event_context;

	opae_release_remote_context(remc);

	opae_free(remc);

	memset(handler, 0, sizeof(*handler));

	return 0;
}

int opae_poll_server_handle_event(opae_poll_server *psrv,
                                  void *event_ctx,
                                  int event_socket)
{
	UNUSED_PARAM(psrv);
	UNUSED_PARAM(event_socket);
	int ires = 0;
	opae_server_event_context *event_context =
		(opae_server_event_context *)event_ctx;
	opae_signal_remote_event_request req;
	opae_signal_remote_event_response resp;
	uint64_t event_count = 0;
	ssize_t slen;

	// Consume the server-side event.
	slen = read(event_socket, &event_count, sizeof(uint64_t));
	if (slen == sizeof(uint64_t)) {
		// Signal the client that an event occurred.
		char *req_json;
		char *resp_json = NULL;
		fpga_result res;

		req.event_id = event_context->event_id;

		req_json = opae_encode_signal_remote_event_request_1(
				&req,
				event_context->remote_context->json_to_string_flags);

		res = opae_server_send_and_receive(
			event_context->remote_context,
			req_json,
			&resp_json);
		if (res != FPGA_OK) {
			ires = 1;
			goto out;
		} 

		if (!opae_decode_signal_remote_event_response_1(
			resp_json, &resp)) {
			ires = 2;
			goto out;
		}

		if (resp.result != FPGA_OK)
			ires = 3;
	}
out:
	return ires;
}

int opae_poll_server_init_event(opae_poll_server *psrv,
				opae_poll_server_handler *handler,
				nfds_t i,
				void *extra,
				socklen_t len)
{
	UNUSED_PARAM(psrv);
	UNUSED_PARAM(i);
	UNUSED_PARAM(len);

	handler->client_or_event_handler = psrv->handle_event;
	handler->client_or_event_release = psrv->release_event;
	handler->client_or_event_context = extra;

	return 0;
}

int opae_poll_server_release_event(opae_poll_server *psrv,
				   opae_poll_server_handler *handler,
				   nfds_t i)
{
	UNUSED_PARAM(psrv);
	UNUSED_PARAM(i);

	opae_free(handler->client_or_event_context);
	memset(handler, 0, sizeof(*handler));

	return 1;
}

/******************************************************************************/

fpga_result opae_init_remote_events_context(opae_remote_events_context *c,
					    opae_server_type type)
{
	fpga_result res;

	c->json_to_string_flags = JSON_C_TO_STRING_SPACED |
				  JSON_C_TO_STRING_PRETTY;

	res = opae_hash_map_init(&c->remote_id_to_eventreg_map,
				 1024, /* num_buckets   */
				 0,    /* hash_seed     */
				 murmur3_32_string_hash,
				 opae_str_key_compare,
				 opae_str_key_cleanup,
				 NULL  /* value_cleanup */);
	if (res)
		return res;

	c->type = type;

	return FPGA_OK;
}

fpga_result opae_release_remote_events_context(opae_remote_events_context *c)
{
	opae_hash_map_destroy(&c->remote_id_to_eventreg_map);
	return FPGA_OK;
}

int opae_poll_server_init_remote_events_client(
	struct _opae_poll_server *psrv,
	opae_poll_server_handler *handler,
	nfds_t i,
	void *extra,
	socklen_t len)
{
	UNUSED_PARAM(i);
	UNUSED_PARAM(extra);
	UNUSED_PARAM(len);
	fpga_result res;
	opae_remote_events_context *remec;

	remec = opae_malloc(sizeof(opae_remote_events_context));
	if (!remec) {
		OPAE_ERR("malloc() failed");
		return 1;
	}

	res = opae_init_remote_events_context(remec, psrv->type);
	if (res) {
		OPAE_ERR("failed to init remote events context");
		return 2;
	}

	handler->client_or_event_handler = psrv->handle_client_message;
	handler->client_or_event_release = psrv->release_client;
	handler->client_or_event_context = remec;

	return 0;
}

int opae_poll_server_release_remote_events_client(
	struct _opae_poll_server *psrv,
	opae_poll_server_handler *handler,
	nfds_t i)
{
	UNUSED_PARAM(psrv);
	UNUSED_PARAM(i);
	opae_remote_events_context *remec =
		(opae_remote_events_context *)handler->client_or_event_context;

	opae_release_remote_events_context(remec);

	opae_free(remec);

	memset(handler, 0, sizeof(*handler));

	return 0;
}

typedef struct _opae_client_event_registration {
	int client_event_fd;
	uint64_t event_count;
} opae_client_event_registration;

STATIC opae_client_event_registration *
opae_client_event_registration_alloc(int client_event_fd,
				     uint64_t event_count)
{
	opae_client_event_registration *r =
		opae_malloc(sizeof(*r));
	if (r) {
		r->client_event_fd = client_event_fd;
		r->event_count = event_count;
	}
	return r;
}

STATIC void opae_client_event_registration_free(
	opae_client_event_registration *r)
{
	close(r->client_event_fd);
	opae_free(r);
}

bool opae_handle_get_remote_event_id_request_0(
	opae_remote_events_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_get_remote_event_id_request req;
	opae_get_remote_event_id_response resp;
	char *hash_key;
	fpga_result result;
	opae_client_event_registration *reg;

	if (!opae_decode_get_remote_event_id_request_0(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "get_remote_event_id request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"get_remote_event_id_response_0");

	resp.result = FPGA_EXCEPTION;
	resp.client_event_fd = eventfd(0, 0);

	// Allocate a new remote ID for the server-side event.
	opae_get_remote_id(&resp.event_id);

	hash_key = opae_remote_id_to_hash_key_alloc(&resp.event_id);
	if (!hash_key) {
		OPAE_ERR("strdup failed");
		close(resp.client_event_fd);
		resp.client_event_fd = -1;
		resp.result = FPGA_NO_MEMORY;
		goto out_respond;
	}

	reg = opae_client_event_registration_alloc(resp.client_event_fd, 1);
	if (!reg) {
		OPAE_ERR("malloc() failed");
		close(resp.client_event_fd);
		resp.client_event_fd = -1;
		resp.result = FPGA_NO_MEMORY;
		goto out_respond;
	}

	// Store the client event registration in our hash map.
	result = opae_hash_map_add(&c->remote_id_to_eventreg_map,
				   hash_key,
				   reg);
	if (result) {
		resp.result = FPGA_EXCEPTION;
		resp.client_event_fd = -1;
		opae_client_event_registration_free(reg);
		opae_str_key_cleanup(hash_key);
		goto out_respond;
	}

	resp.result = FPGA_OK;
	res = true;

out_respond:
	*resp_json = opae_encode_get_remote_event_id_response_0(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_signal_remote_event_request_1(
	opae_remote_events_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_signal_remote_event_request req;
	opae_signal_remote_event_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	opae_client_event_registration *reg = NULL;
	ssize_t slen;

	if (!opae_decode_signal_remote_event_request_1(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "signal_remote_event request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"signal_remote_event_response_1");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.event_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the client event registration in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_eventreg_map,
				hash_key_buf,
				(void **)&reg) != FPGA_OK) {
		OPAE_ERR("client_event_registration "
			 "lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Signal the client-side event.
	slen = write(reg->client_event_fd, &reg->event_count, sizeof(uint64_t));
	if (slen == sizeof(uint64_t)) {
		++reg->event_count;
		if (reg->event_count == 0xfffffffffffffffe)
			reg->event_count = 1;
		resp.result = FPGA_OK;
	}

	res = true;

out_respond:
	*resp_json = opae_encode_signal_remote_event_response_1(
			&resp,
			c->json_to_string_flags);

	return res;
}

bool opae_handle_release_remote_event_request_2(
	opae_remote_events_context *c,
	const char *req_json,
	char **resp_json)
{
	bool res = false;
	opae_release_remote_event_request req;
	opae_release_remote_event_response resp;
	char hash_key_buf[OPAE_MAX_TOKEN_HASH];
	opae_client_event_registration *reg = NULL;

	if (!opae_decode_release_remote_event_request_2(req_json,
		&req)) {
		OPAE_ERR("failed to decode "
			 "release_remote_event request");
		return false;
	}

	request_header_to_response_header(
		&req.header,
		&resp.header,
		"release_remote_event_response_2");

	resp.result = FPGA_EXCEPTION;

	opae_remote_id_to_hash_key(&req.event_id,
				   hash_key_buf,
				   sizeof(hash_key_buf));

	// Find the client event registration in our remote context.
	if (opae_hash_map_find(&c->remote_id_to_eventreg_map,
				hash_key_buf,
				(void **)&reg) != FPGA_OK) {
		OPAE_ERR("client_event_registration "
			 "lookup failed for %s", hash_key_buf);
		resp.result = FPGA_NOT_FOUND;
		goto out_respond;
	}

	// Remove the client event registration from our remote context object.
	opae_hash_map_remove(&c->remote_id_to_eventreg_map, hash_key_buf);

	opae_client_event_registration_free(reg);

	resp.result = FPGA_OK;
	res = true;

out_respond:
	*resp_json = opae_encode_release_remote_event_response_2(
			&resp,
			c->json_to_string_flags);

	return res;
}

typedef bool (*events_handler)(opae_remote_events_context *c,
			       const char *req_json,
			       char **resp_json);

STATIC events_handler events_handlers[] = {
	opae_handle_get_remote_event_id_request_0,
	opae_handle_signal_remote_event_request_1,
	opae_handle_release_remote_event_request_2
};

bool opae_remote_handle_events_client_request(opae_remote_events_context *c,
					      const char *req_json,
					      char **resp_json)
{
	struct json_object *root = NULL;
	enum json_tokener_error j_err = json_tokener_success;
	opae_request_header header;

	root = json_tokener_parse_verbose(req_json, &j_err);
	if (!root) {
		OPAE_ERR("JSON parse failed: %s",
			 json_tokener_error_desc(j_err));
		return false;
	}

	if (!opae_decode_request_header_obj(root, &header)) {
		OPAE_ERR("request header decode failed");
		json_object_put(root);
		return false;
	}

	json_object_put(root);

	if (header.request_id >=
		(sizeof(events_handlers) / sizeof(events_handlers[0]))) {
		// Deal with out-of-bounds request_id.

		return false;
	}

	return events_handlers[header.request_id](c, req_json, resp_json);
}

int opae_poll_server_handle_events_client(opae_poll_server *psrv,
					  void *client_ctx,
					  int client_socket)
{
	char buf[OPAE_RECEIVE_BUF_MAX];
	ssize_t n;
	opae_remote_events_context *remec =
		(opae_remote_events_context *)client_ctx;
	char *response_json = NULL;
	bool res;

	n = chunked_recv(client_socket, buf, sizeof(buf), 0);
	if (n == -2) {
		OPAE_DBG("peer closed connection");
		opae_poll_server_close_client(psrv, client_socket);
		return 0;
	} else if (n < 0) {
		OPAE_ERR("recv() failed: %s", strerror(errno));
		return (int)n;
	}

	res = opae_remote_handle_events_client_request(remec,
						       buf,
						       &response_json);
	if (res && response_json) {
		chunked_send(client_socket,
			     response_json,
			     strlen(response_json) + 1,
			     0);
		opae_free(response_json);
		return 0;
	}

	if (response_json)
		opae_free(response_json);

	return 1;
}
