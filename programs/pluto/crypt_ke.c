/*
 * Cryptographic helper function - calculate KE and nonce
 * Copyright (C) 2004 Michael C. Richardson <mcr@xelerance.com>
 * Copyright (C) 2009 - 2012 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2009 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2012 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2017 Andrew Cagney <cagney@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * This code was developed with the support of IXIA communications.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <signal.h>


#include "sysdep.h"
#include "constants.h"
#include "defs.h"
#include "packet.h"
#include "demux.h"
#include "crypto.h"
#include "rnd.h"
#include "state.h"
#include "pluto_crypt.h"
#include "log.h"

#include <nspr.h>
#include <prerror.h>
#include <pk11pub.h>
#include <keyhi.h>
#include "lswnss.h"
#include "test_buffer.h"
#include "ike_alg.h"
#include "crypt_dh.h"
#include "crypt_ke.h"

/* MUST BE THREAD-SAFE */
void calc_ke(struct pcr_kenonce *kn, struct logger *logger)
{
	kn->local_secret = calc_dh_local_secret(kn->group, logger);
}

/* MUST BE THREAD-SAFE */
void calc_nonce(struct pcr_kenonce *kn)
{
	kn->n = alloc_chunk(DEFAULT_NONCE_SIZE, "n");
	get_rnd_bytes(kn->n.ptr, kn->n.len);

	if (DBGP(DBG_CRYPT)) {
		DBG_dump_hunk("Generated nonce:", kn->n);
	}
}

void cancelled_ke_and_nonce(struct pcr_kenonce *kn)
{
	dh_local_secret_delref(&kn->local_secret, HERE);
	free_chunk_content(&kn->n);
}

/* Note: not all cn's are the same subtype */
void request_ke_and_nonce(const char *name,
			  struct state *st,
			  const struct dh_desc *group,
			  crypto_req_cont_func *callback)
{
	struct pluto_crypto_req_cont *cn = new_pcrc(callback, name);
	pcr_kenonce_init(cn, pcr_build_ke_and_nonce, group);
	send_crypto_helper_request(st, cn);
}

void request_nonce(const char *name,
		   struct state *st,
		   crypto_req_cont_func *callback)
{
	struct pluto_crypto_req_cont *cn = new_pcrc(callback, name);
	pcr_kenonce_init(cn, pcr_build_nonce, NULL);
	send_crypto_helper_request(st, cn);
}

struct crypto_task {
	const struct dh_desc *dh;
	chunk_t nonce;
	struct dh_local_secret *local_secret;
	ke_and_nonce_cb *cb;
};

static void compute_ke_and_nonce(struct logger *logger,
				 struct crypto_task *task,
				 int thread_unused UNUSED)
{
	if (task->dh != NULL) {
		task->local_secret = calc_dh_local_secret(task->dh, logger);
		if (DBGP(DBG_CRYPT)) {
			DBG_log("NSS: Local DH %s secret (pointer): %p",
				task->dh->common.fqn, task->local_secret);
		}
	}
	task->nonce = alloc_chunk(DEFAULT_NONCE_SIZE, "nonce");
	fill_rnd_chunk(task->nonce);
	if (DBGP(DBG_CRYPT)) {
		DBG_dump_hunk("Generated nonce:", task->nonce);
	}
}

static void free_ke_and_nonce(struct crypto_task **task)
{
	dh_local_secret_delref(&(*task)->local_secret, HERE);
	free_chunk_content(&(*task)->nonce);
	pfreeany(*task);
}

static stf_status complete_ke_and_nonce(struct state *st,
					struct msg_digest *md,
					struct crypto_task **task)
{
	stf_status status = (*task)->cb(st, md,
					(*task)->local_secret,
					&(*task)->nonce);
	free_ke_and_nonce(task);
	return status;
}

static const struct crypto_handler ke_and_nonce_handler = {
	.name = "dh",
	.cancelled_cb = free_ke_and_nonce,
	.compute_fn = compute_ke_and_nonce,
	.completed_cb = complete_ke_and_nonce,
};

void submit_ke_and_nonce(struct state *st, const struct dh_desc *dh,
			 ke_and_nonce_cb *cb, const char *name)
{
	struct crypto_task *task = alloc_thing(struct crypto_task, "dh");
	task->dh = dh;
	task->cb = cb;
	submit_crypto(st->st_logger, st, task, &ke_and_nonce_handler, name);
}
