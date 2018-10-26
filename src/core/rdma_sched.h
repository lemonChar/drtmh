#ifndef NOCC_RDMA_SCHED
#define NOCC_RDMA_SCHED

#include <deque>

#include "util/util.h"

#include "rdma_ctrl.hpp"
#include "core/logging.h"

namespace nocc {

namespace oltp {

class RScheduler {
 public:
  RScheduler();
  ~RScheduler();

  void one_read(rdmaio::RCQP *qp,int cor_id,char *local_buf,int len,uint64_t off,int flags = IBV_SEND_SIGNALED) {
    post_send(qp,cor_id,IBV_WR_RDMA_READ,local_buf,len,off,flags);
  }

  void one_write(rdmaio::RCQP *qp,int cor_id,char *local_buf,int len,uint64_t off,int flags = IBV_SEND_SIGNALED) {
    post_send(qp,cor_id,IBV_WR_RDMA_WRITE,local_buf,len,off,flags);
  }

  rdmaio::ConnStatus post_send(rdmaio::RCQP *qp,int cor_id,ibv_wr_opcode op,char *local_buf,int len,uint64_t off,int flags) {
    qp->high_watermark_ += 1;
    auto rc = qp->post_send(op,local_buf,len,off,flags,encode_wrid(cor_id,qp->high_watermark_));
    add_pending(cor_id,qp);
    return rc;
  }

  rdmaio::ConnStatus post_cas(rdmaio::RCQP *qp,int cor_id,char *local_buf,uint64_t off,uint64_t compare,uint64_t swap,int flags) {
    qp->high_watermark_ += 1;
    auto rc = qp->post_cas(local_buf,off,compare,swap,flags,encode_wrid(cor_id,qp->high_watermark_));
    add_pending(cor_id,qp);
    return rc;
  }

  void post_batch(rdmaio::RCQP *qp,int cor_id,struct ibv_send_wr *send_sr,ibv_send_wr **bad_sr_addr,int doorbell_num = 0) {
    qp->high_watermark_ += (1 + doorbell_num);
    send_sr[doorbell_num].wr_id = encode_wrid(cor_id,qp->high_watermark_);

    qp->post_batch(send_sr,bad_sr_addr,doorbell_num);
    add_pending(cor_id,qp);
  }

  /**
   * return whether polling is needed
   */
  bool post_batch_pending(rdmaio::RCQP *qp,int cor_id,struct ibv_send_wr *send_sr,ibv_send_wr **bad_sr_addr,int doorbell_num = 0) {

    if(qp->need_poll()) {
      auto temp =  send_sr[doorbell_num].send_flags;
      send_sr[doorbell_num].send_flags |= IBV_SEND_SIGNALED;
      post_batch(qp,cor_id,send_sr,bad_sr_addr,doorbell_num);
      send_sr[doorbell_num].send_flags = temp; // re-set the flag
    } else {
      qp->high_watermark_ += (1 + doorbell_num);
      send_sr[doorbell_num].wr_id = encode_wrid(cor_id,qp->high_watermark_);
      qp->post_batch(send_sr,bad_sr_addr,doorbell_num);
    }
  }


  // poll all the pending qps of the thread and schedule
  void poll_comps();

  void thread_local_init(int coroutines);

  void report();

  static __thread int  *pending_counts_; // number of pending qps per thread

  static const int  COR_ID_BIT = 8;
  static const uint  COR_ID_MASK = ::nocc::util::BitMask<uint>(COR_ID_BIT);

  // add pending qp to corresponding coroutine
  inline static uint64_t encode_wrid(int cor_id,uint64_t watermark = 0) {
    return (watermark << COR_ID_BIT) | cor_id;
  }

  inline static int decode_corid(uint64_t wrid) {
    return wrid & COR_ID_MASK;
  }

  inline static uint64_t decode_watermark(uint64_t wrid) {
    return wrid >> COR_ID_BIT;
  }

  void add_pending(int cor_id,rdmaio::RCQP *qp) {
    pending_qps_.push_back(qp);
    pending_counts_[cor_id] += 1;
  }

 private:
  std::deque<rdmaio::RCQP *> pending_qps_;
  struct ibv_wc wc_;

};

}; // namespace db

};   // namespace nocc

#endif // DB_RDMA_SCHED
