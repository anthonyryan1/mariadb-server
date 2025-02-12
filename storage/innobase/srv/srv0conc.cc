/*****************************************************************************

Copyright (c) 2011, 2015, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2015, 2021, MariaDB Corporation.

Portions of this file contain modifications contributed and copyrighted by
Google, Inc. Those modifications are gratefully acknowledged and are described
briefly in the InnoDB documentation. The contributions by Google are
incorporated with their permission, and subject to the conditions contained in
the file COPYING.Google.

Portions of this file contain modifications contributed and copyrighted
by Percona Inc.. Those modifications are
gratefully acknowledged and are described briefly in the InnoDB
documentation. The contributions by Percona Inc. are incorporated with
their permission, and subject to the conditions contained in the file
COPYING.Percona.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file srv/srv0conc.cc

InnoDB concurrency manager

Created 2011/04/18 Sunny Bains
*******************************************************/

#include "srv0srv.h"
#include "trx0trx.h"
#include "row0mysql.h"
#include "dict0dict.h"
#include <mysql/service_thd_wait.h>
#include <mysql/service_wsrep.h>
#include "wsrep.h"
#include "log.h"

/** Number of times a thread is allowed to enter InnoDB within the same
SQL query after it has once got the ticket. */
ulong	srv_n_free_tickets_to_enter = 500;

/** Maximum sleep delay (in micro-seconds), value of 0 disables it. */
ulong	srv_adaptive_max_sleep_delay = 150000;

ulong	srv_thread_sleep_delay	= 10000;


/** We are prepared for a situation that we have this many threads waiting for
a semaphore inside InnoDB. srv_start() sets the value. */
ulint	srv_max_n_threads;

/** The following controls how many threads we let inside InnoDB concurrently:
threads waiting for locks are not counted into the number because otherwise
we could get a deadlock. Value of 0 will disable the concurrency check. */

ulong	srv_thread_concurrency	= 0;

/** Variables tracking the active and waiting threads. */
struct srv_conc_t {
	/** Number of transactions that have declared_to_be_inside_innodb */
	MY_ALIGNED(CPU_LEVEL1_DCACHE_LINESIZE) Atomic_counter<ulint> n_active;

	/** Number of OS threads waiting in the FIFO for permission to
	enter InnoDB */
	MY_ALIGNED(CPU_LEVEL1_DCACHE_LINESIZE) Atomic_counter<ulint> n_waiting;
};

/* Control variables for tracking concurrency. */
static srv_conc_t	srv_conc;

/*********************************************************************//**
Note that a user thread is entering InnoDB. */
static
void
srv_enter_innodb_with_tickets(
/*==========================*/
	trx_t*	trx)			/*!< in/out: transaction that wants
					to enter InnoDB */
{
	trx->declared_to_be_inside_innodb = TRUE;
	trx->n_tickets_to_enter_innodb = srv_n_free_tickets_to_enter;
}

/*********************************************************************//**
Handle the scheduling of a user thread that wants to enter InnoDB.  Setting
srv_adaptive_max_sleep_delay > 0 switches the adaptive sleep calibration to
ON. When set, we want to wait in the queue for as little time as possible.
However, very short waits will result in a lot of context switches and that
is also not desirable. When threads need to sleep multiple times we increment
os_thread_sleep_delay by one. When we see threads getting a slot without
waiting and there are no other threads waiting in the queue, we try and reduce
the wait as much as we can. Currently we reduce it by half each time. If the
thread only had to wait for one turn before it was able to enter InnoDB we
decrement it by one. This is to try and keep the sleep time stable around the
"optimum" sleep time. */
static
void
srv_conc_enter_innodb_with_atomics(
/*===============================*/
	trx_t*	trx)			/*!< in/out: transaction that wants
					to enter InnoDB */
{
	ulint	n_sleeps = 0;
	ibool	notified_mysql = FALSE;

	ut_a(!trx->declared_to_be_inside_innodb);

	for (;;) {
		ulint	sleep_in_us;
#ifdef WITH_WSREP
		/* We need to take `thd->LOCK_thd_data` to check WSREP thread state */
		if (trx->is_wsrep()) {
			wsrep_thd_LOCK(trx->mysql_thd);

			if (wsrep_thd_is_aborting(trx->mysql_thd)) {
				WSREP_DEBUG("srv_conc_enter due to MUST_ABORT for"
					    TRX_ID_FMT, trx->id);
			}
			wsrep_thd_UNLOCK(trx->mysql_thd);
			srv_conc_force_enter_innodb(trx);
			return;
		}
#endif /* WITH_WSREP */

		if (srv_thread_concurrency == 0) {
			if (notified_mysql) {
				srv_conc.n_waiting--;
				thd_wait_end(trx->mysql_thd);
			}

			return;
		}

		if (srv_conc.n_active < srv_thread_concurrency) {

			/* Check if there are any free tickets. */
			if (srv_conc.n_active++ < srv_thread_concurrency) {

				srv_enter_innodb_with_tickets(trx);

				if (notified_mysql) {
					srv_conc.n_waiting--;
					thd_wait_end(trx->mysql_thd);
				}

				if (srv_adaptive_max_sleep_delay > 0) {
					if (srv_thread_sleep_delay > 20
					    && n_sleeps == 1) {

						--srv_thread_sleep_delay;
					}

					if (srv_conc.n_waiting == 0) {
						srv_thread_sleep_delay >>= 1;
					}
				}

				return;
			}

			/* Since there were no free seats, we relinquish
			the overbooked ticket. */

			srv_conc.n_active--;
		}

		if (!notified_mysql) {
			srv_conc.n_waiting++;

			thd_wait_begin(trx->mysql_thd, THD_WAIT_USER_LOCK);

			notified_mysql = TRUE;
		}

		DEBUG_SYNC_C("user_thread_waiting");
		trx->op_info = "sleeping before entering InnoDB";

		sleep_in_us = srv_thread_sleep_delay;

		/* Guard against overflow when adaptive sleep delay is on. */

		if (srv_adaptive_max_sleep_delay > 0
		    && sleep_in_us > srv_adaptive_max_sleep_delay) {

			sleep_in_us = srv_adaptive_max_sleep_delay;
			srv_thread_sleep_delay = static_cast<ulong>(sleep_in_us);
		}

		os_thread_sleep(sleep_in_us);

		trx->op_info = "";

		++n_sleeps;

		if (srv_adaptive_max_sleep_delay > 0 && n_sleeps > 1) {
			++srv_thread_sleep_delay;
		}
	}
}

/*********************************************************************//**
Note that a user thread is leaving InnoDB code. */
static
void
srv_conc_exit_innodb_with_atomics(
/*==============================*/
	trx_t*	trx)		/*!< in/out: transaction */
{
	trx->n_tickets_to_enter_innodb = 0;
	trx->declared_to_be_inside_innodb = FALSE;

	srv_conc.n_active--;
}

/*********************************************************************//**
Puts an OS thread to wait if there are too many concurrent threads
(>= srv_thread_concurrency) inside InnoDB. The threads wait in a FIFO queue.
@param[in,out]	prebuilt	row prebuilt handler */
void
srv_conc_enter_innodb(
	row_prebuilt_t*	prebuilt)
{
	trx_t*	trx	= prebuilt->trx;

	ut_ad(!sync_check_iterate(sync_check()));

	srv_conc_enter_innodb_with_atomics(trx);
}

/*********************************************************************//**
This lets a thread enter InnoDB regardless of the number of threads inside
InnoDB. This must be called when a thread ends a lock wait. */
void
srv_conc_force_enter_innodb(
/*========================*/
	trx_t*	trx)	/*!< in: transaction object associated with the
			thread */
{
	ut_ad(!sync_check_iterate(sync_check()));

	if (!srv_thread_concurrency) {

		return;
	}

	srv_conc.n_active++;

	trx->n_tickets_to_enter_innodb = 1;
	trx->declared_to_be_inside_innodb = TRUE;
}

/*********************************************************************//**
This must be called when a thread exits InnoDB in a lock wait or at the
end of an SQL statement. */
void
srv_conc_force_exit_innodb(
/*=======================*/
	trx_t*	trx)	/*!< in: transaction object associated with the
			thread */
{
	if ((trx->mysql_thd != NULL
	     && thd_is_replication_slave_thread(trx->mysql_thd))
	    || trx->declared_to_be_inside_innodb == FALSE) {

		return;
	}

	srv_conc_exit_innodb_with_atomics(trx);

	ut_ad(!sync_check_iterate(sync_check()));
}

/*********************************************************************//**
Get the count of threads waiting inside InnoDB. */
ulint
srv_conc_get_waiting_threads(void)
/*==============================*/
{
	return(srv_conc.n_waiting);
}

/*********************************************************************//**
Get the count of threads active inside InnoDB. */
ulint
srv_conc_get_active_threads(void)
/*==============================*/
{
	return(srv_conc.n_active);
}

#ifdef WITH_WSREP
UNIV_INTERN
void
wsrep_srv_conc_cancel_wait(
/*=======================*/
	trx_t*	trx)	/*!< in: transaction object associated with the
			thread */
{
#ifdef HAVE_ATOMIC_BUILTINS
	/* aborting transactions will enter innodb by force in
	   srv_conc_enter_innodb_with_atomics(). No need to cancel here,
	   thr will wake up after os_sleep and let to enter innodb
	*/
	if (UNIV_UNLIKELY(wsrep_debug)) {
		ib::info() << "WSREP: conc slot cancel, no atomics";
	}
#else
	// JAN: TODO: MySQL 5.7
	//os_fast_mutex_lock(&srv_conc_mutex);
	if (trx->wsrep_event) {
		if (UNIV_UNLIKELY(wsrep_debug)) {
			ib::info() << "WSREP: conc slot cancel";
		}
		os_event_set(trx->wsrep_event);
	}
	//os_fast_mutex_unlock(&srv_conc_mutex);
#endif
}
#endif /* WITH_WSREP */

