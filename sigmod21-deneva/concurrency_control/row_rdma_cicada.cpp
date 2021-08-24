/*
Copyright 2016 Massachusetts Institute of Technology

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

	http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "row.h"
#include "txn.h"
#include "row_rdma_cicada.h"
#include "mem_alloc.h"
#include "manager.h"
#include "helper.h"
#include "rdma_cicada.h"
#include "qps/op.hh"
#include "rdma.h"
#include "routine.h"
#if CC_ALG == RDMA_CICADA

void Row_rdma_cicada::init(row_t * row) {
	_row = row;
}
bool Row_rdma_cicada::local_cas_lock(TxnManager * txnMng , uint64_t info, uint64_t new_info){
   // INC_STATS(txnMng->get_thd_id(), cas_cnt, 1);
    uint64_t loc = g_node_id;
	uint64_t thd_id = txnMng->get_thd_id();
	uint64_t *tmp_buf2 = (uint64_t *)Rdma::get_row_client_memory(thd_id);
	auto mr = client_rm_handler->get_reg_attr().value();

	rdmaio::qp::Op<> op;
	op.set_atomic_rbuf((uint64_t*)(remote_mr_attr[loc].buf + ((char *)_row - rdma_global_buffer)), remote_mr_attr[loc].key).set_cas(info,new_info);
	assert(op.set_payload(tmp_buf2, sizeof(uint64_t), mr.key) == true);
	auto res_s2 = op.execute(rc_qp[loc][thd_id], IBV_SEND_SIGNALED);

	RDMA_ASSERT(res_s2 == IOCode::Ok);
	auto res_p2 = rc_qp[loc][thd_id]->wait_one_comp();
	RDMA_ASSERT(res_p2 == IOCode::Ok);

    if(*tmp_buf2 != info) return false;
    return true;
  // return 0;
}

RC Row_rdma_cicada::access(yield_func_t &yield ,access_t type, TxnManager * txn, row_t * local_row, uint64_t cor_id) {
	uint64_t starttime = get_sys_clock();
	
	RC rc = RCOK;
	uint64_t mtx_wait_starttime = get_sys_clock();
	//while (!ATOM_CAS(_row->_tid_word, 0, 1)) {
	// if(!local_cas_lock(txn, 0, txn->get_txn_id() + 1)){
	// 	return Abort;
	// }
	DEBUG("READ %ld -- %ld\n", txn->get_txn_id(), _row->get_primary_key());
	uint64_t version = 0;	
	if(type == RD) {
		for(int cnt = _row->version_cnt; cnt >= _row->version_cnt - HIS_CHAIN_NUM && cnt >= 0; cnt--) {
			int i = cnt % HIS_CHAIN_NUM;
			if(_row->cicada_version[i].state == Cicada_ABORTED) {
				continue;
			}
			if(_row->cicada_version[i].Wts > txn->get_timestamp()) {
				rc = Abort;
				break;
			}
			if(_row->cicada_version[i].state == Cicada_PENDING) {
				// --todo !---pendind need wait //
				
				rc = WAIT;
				while(rc == WAIT && !simulation->is_done()) {
					// local_cas_lock(txn, txn->get_txn_id(), 0);
					// if(!local_cas_lock(txn, 0, txn->get_txn_id())){
					// 	return Abort;
					// }
#if USE_COROUTINE
					txn->h_thd->last_yield_time = get_sys_clock();
					// printf("do\n");
					yield(txn->h_thd->_routines[((cor_id) % COROUTINE_CNT) + 1]);
					uint64_t yield_endtime = get_sys_clock();
					INC_STATS(txn->get_thd_id(), worker_yield_cnt, 1);
					INC_STATS(txn->get_thd_id(), worker_yield_time, yield_endtime - txn->h_thd->last_yield_time);
					INC_STATS(txn->get_thd_id(), worker_idle_time, yield_endtime - txn->h_thd->last_yield_time);
					INC_STATS(txn->get_thd_id(), worker_proto_wait_time, yield_endtime - txn->h_thd->last_yield_time);
#endif
					if(_row->cicada_version[i].state == Cicada_PENDING) {
						rc = WAIT;
					} else if (_row->cicada_version[i].state == Cicada_ABORTED) {
						rc = Abort;
						break;
					} else {
						rc = RCOK;
						version = _row->cicada_version[i].key;
					}
				}
				//rc = Abort;
				// rc = RCOK;
				// version = _row->cicada_version[i].key;
			} else {
				rc = RCOK;
				version = _row->cicada_version[i].key;
			}
			
		}
	} else if(type == WR) {
		assert(_row->version_cnt >= 0);
		for(int cnt = _row->version_cnt; cnt >= _row->version_cnt - HIS_CHAIN_NUM && cnt >= 0; cnt--) {
			int i = cnt % HIS_CHAIN_NUM;
			if(_row->cicada_version[i].state == Cicada_ABORTED) {
				continue;
			}
			if(_row->cicada_version[i].Wts > txn->get_timestamp() || _row->cicada_version[i].Rts > txn->get_timestamp()) {
				rc = Abort;
				break;
			}
			if(_row->cicada_version[i].state == Cicada_PENDING) {
				// --todo !---pendind need wait //
				
				rc = WAIT;
				while(rc == WAIT && !simulation->is_done()) {
					// local_cas_lock(txn, txn->get_txn_id(), 0);
					// if(!local_cas_lock(txn, 0, txn->get_txn_id())){
					// 	return Abort;
					// }
#if USE_COROUTINE
					txn->h_thd->last_yield_time = get_sys_clock();
					// printf("do\n");
					yield(txn->h_thd->_routines[((cor_id) % COROUTINE_CNT) + 1]);
					uint64_t yield_endtime = get_sys_clock();
					INC_STATS(txn->get_thd_id(), worker_yield_cnt, 1);
					INC_STATS(txn->get_thd_id(), worker_yield_time, yield_endtime - txn->h_thd->last_yield_time);
					INC_STATS(txn->get_thd_id(), worker_idle_time, yield_endtime - txn->h_thd->last_yield_time);
					INC_STATS(txn->get_thd_id(), worker_proto_wait_time, yield_endtime - txn->h_thd->last_yield_time);
#endif
					if(_row->cicada_version[i].state == Cicada_PENDING) {
						rc = WAIT;
					} else if (_row->cicada_version[i].state == Cicada_ABORTED) {
						break;
					} else {
						rc = RCOK;
						version = _row->cicada_version[i].key;
					}
					// rc = Abort;
				}
				// rc = Abort;
				// rc = RCOK;
				// version = _row->cicada_version[i].key;
			} else {
				if(_row->cicada_version[i].Wts > txn->get_timestamp() || _row->cicada_version[i].Rts > txn->get_timestamp()) {
					rc = Abort;
				} else {
					rc = RCOK;
					version = _row->cicada_version[i].key;
				}
			}
		}
	}
	txn->version_num.push_back(version);
	uint64_t timespan = get_sys_clock() - starttime;
	txn->txn_stats.cc_time += timespan;
	txn->txn_stats.cc_time_short += timespan;
	//ATOM_CAS(_row->_tid_word,1,0);
	// local_cas_lock(txn, txn->get_txn_id() + 1, 0);
	return rc;
}

RC Row_rdma_cicada::abort(uint64_t num, TxnManager * txn) {
	uint64_t mtx_wait_starttime = get_sys_clock();
	//while (!ATOM_CAS(_row->_tid_word, 0, 1)) {
	// if(!local_cas_lock(txn, 0, txn->get_txn_id() + 1)){
	// 	return Abort;
	// }
	INC_STATS(txn->get_thd_id(),mtx[32],get_sys_clock() - mtx_wait_starttime);
	DEBUG("CICADA Abort %ld: %d -- %ld\n",txn->get_txn_id(),num,_row->get_primary_key());
	_row->cicada_version[num % HIS_CHAIN_NUM].state = Cicada_ABORTED;
	//ATOM_CAS(_row->_tid_word,1,0);
	// local_cas_lock(txn, txn->get_txn_id() + 1, 0);
}

RC Row_rdma_cicada::commit(uint64_t num, TxnManager * txn, row_t * data) {
	//printf("the first txn will commit %d\n", txn->get_txn_id());
	uint64_t mtx_wait_starttime = get_sys_clock();
	//while (!ATOM_CAS(_row->_tid_word, 0, 1)) {
	// if(!local_cas_lock(txn, 0, txn->get_txn_id() + 1)){
	// 	return Abort;
	// }
	INC_STATS(txn->get_thd_id(),mtx[33],get_sys_clock() - mtx_wait_starttime);
	DEBUG("CICADA Commit %ld: %d,%lu -- %ld\n", txn->get_txn_id(), num, txn->get_commit_timestamp(),
			_row->get_primary_key());
	_row->cicada_version[num % HIS_CHAIN_NUM].state = Cicada_COMMITTED;

	uint64_t txn_commit_ts = txn->get_commit_timestamp();
	// local_cas_lock(txn, txn->get_txn_id() + 1, 0);
	//ATOM_CAS(_row->_tid_word,1,0);
 	return RCOK;
}
void Row_rdma_cicada::write(row_t* data) {
    assert(data != NULL);
     _row->copy(data); }
#endif

