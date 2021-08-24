#include "global.h"
#include "message.h"
#include "ycsb_query.h"
#include "thread.h"

#if CC_ALG == RDMA_CALVIN
static const uint64_t max_msg_cnt = 5500;

//循环队列
class sched_queue {
public:
    void add_txnentry(uint64_t thd_id, Message * msg);
    void add_RDONE(uint64_t thd_id);
    void write_entry(uint64_t thd_id, Message * msg, uint64_t loc);
    void write_rear(uint64_t thd_id, uint64_t loc);
#if USE_COROUTINE
    void add_txnentry(yield_func_t &yield, Thread * h_thd, Message * msg, uint64_t cor_id);
    void write_rear(yield_func_t &yield, uint64_t cor_id, Thread * h_thd, uint64_t loc);
    void write_entry(yield_func_t &yield, uint64_t cor_id, Thread * h_thd, Message * msg, uint64_t loc);
#endif
    volatile uint64_t rear;
    volatile uint64_t front;
    char * msgbuf;
};


class RDMA_calvin {
public:
  void init();
  void read_remote_queue(uint64_t thd_id, sched_queue * temp_queue, uint64_t loc);
  uint64_t read_remote_front(uint64_t thd_id, uint64_t loc);
  Message * sched_dequeue(uint64_t thd_id);
  sched_queue * rdma_sched_queue[NODE_CNT];
  uint64_t queue_size;
#if USE_COROUTINE
  void read_remote_queue(yield_func_t &yield, uint64_t cor_id, Thread * h_thd, sched_queue * temp_queue, uint64_t loc);
  uint64_t read_remote_front(yield_func_t &yield, uint64_t cor_id, Thread * h_thd, uint64_t loc);
#endif

private:
  uint64_t sched_ptr;
};

#endif
