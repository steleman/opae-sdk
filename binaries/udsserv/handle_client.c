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

#include <sys/types.h>
#include <sys/socket.h>

#include "mock/opae_std.h"
#include "rmt-ifc.h"
#include "action.h"
#include "udsserv.h"

int handle_client(uds_server_context *c, void *remote_ctx, int sock)
{
	char buf[OPAE_RECEIVE_BUF_MAX];
	ssize_t n;
	opae_remote_context *remc = (opae_remote_context *)remote_ctx;
	char *response_json = NULL;
	bool res;

	n = chunked_recv(sock, buf, sizeof(buf), 0);
	if (n == -2) {
		OPAE_DBG("peer closed connection");
		uds_server_close_client(c, sock);
		return 0;
	} else if (n < 0) {
		OPAE_ERR("recv() failed");
		return (int)n;
	}

	res = opae_remote_handle_client_request(remc, buf, &response_json);
	if (res && response_json) {
		chunked_send(sock,
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
