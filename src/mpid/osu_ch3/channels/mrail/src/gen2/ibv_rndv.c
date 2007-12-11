/* Copyright (c) 2002-2007, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#include "rdma_impl.h"
#include "ibv_impl.h"
#include "vbuf.h"
#include "dreg.h"

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
    do {                                                          \
        int rank;                                                 \
        PMI_Get_rank(&rank);                                      \
        fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
        fprintf(stderr, args);                                    \
    } while (0)
#else
#define DEBUG_PRINT(args...)
#endif

void get_sorted_index(MPIDI_VC_t *vc, int *b);

int MPIDI_CH3I_MRAIL_Prepare_rndv(MPIDI_VC_t * vc, MPID_Request * req)
{
    dreg_entry *reg_entry;
    /*
    DEBUG_PRINT
        ("[prepare cts] rput protocol, recv size %d, segsize %d, \
         io count %d, rreq ca %d\n",
         req->dev.recv_data_sz, req->dev.segment_size, req->dev.iov_count,
         req->dev.ca);
         */

    if(VAPI_PROTOCOL_RPUT == rdma_rndv_protocol) {
        req->mrail.protocol = VAPI_PROTOCOL_RPUT;
    } else if (VAPI_PROTOCOL_RGET == rdma_rndv_protocol) {
        req->mrail.protocol = VAPI_PROTOCOL_RGET;
    } else {
        req->mrail.protocol = VAPI_PROTOCOL_R3;
    }

    /* Step 1: ready for user space (user buffer or pack) */
    if (1 == req->dev.iov_count && (req->dev.OnDataAvail == NULL ||
                (req->dev.OnDataAvail == req->dev.OnFinal) ||
                (req->dev.OnDataAvail == 
                 MPIDI_CH3_ReqHandler_UnpackSRBufComplete))) {
        req->mrail.rndv_buf = req->dev.iov[0].MPID_IOV_BUF;
        req->mrail.rndv_buf_sz = req->dev.iov[0].MPID_IOV_LEN;
        req->mrail.rndv_buf_alloc = 0;
    } else {
        req->mrail.rndv_buf_sz = req->dev.segment_size;
        req->mrail.rndv_buf = MPIU_Malloc(req->mrail.rndv_buf_sz);

        if (req->mrail.rndv_buf == NULL) {

            /* fall back to r3 if cannot allocate tmp buf */

            DEBUG_PRINT("[rndv sent] set info: cannot allocate space\n");
            req->mrail.protocol = VAPI_PROTOCOL_R3;
            req->mrail.rndv_buf_sz = 0;
        } else {
            req->mrail.rndv_buf_alloc = 1;
        }
    }
    req->mrail.rndv_buf_off = 0;

    /* Step 2: try register and decide the protocol */

    if ( (VAPI_PROTOCOL_RPUT == req->mrail.protocol) ||
            (VAPI_PROTOCOL_RGET == req->mrail.protocol) ) {
        DEBUG_PRINT("[cts] size registered %d, addr %p\n",
                req->mrail.rndv_buf_sz, req->mrail.rndv_buf);
        reg_entry =
            dreg_register(req->mrail.rndv_buf, req->mrail.rndv_buf_sz);
        if (NULL == reg_entry) {
            req->mrail.protocol = VAPI_PROTOCOL_R3;
            if (1 == req->mrail.rndv_buf_alloc) {
                MPIU_Free(req->mrail.rndv_buf);
                req->mrail.rndv_buf_alloc = 0;
                req->mrail.rndv_buf_sz = 0;
                req->mrail.rndv_buf = NULL;
            }
            req->mrail.rndv_buf_alloc = 0;
            /*MRAILI_Prepost_R3(); */
        }
        DEBUG_PRINT("[prepare cts] register success\n");
    }

    if ( (VAPI_PROTOCOL_RPUT == req->mrail.protocol) ||
            (VAPI_PROTOCOL_RGET == req->mrail.protocol) ) {
        req->mrail.completion_counter = 0;
        req->mrail.d_entry = reg_entry;
        return 1;
    } else {
        return 0;
    }
}

int MPIDI_CH3I_MRAIL_Prepare_rndv_transfer(MPID_Request * sreq, 
        /* contains local info */
        MPIDI_CH3I_MRAILI_Rndv_info_t *rndv)
{
    int hca_index;

    if (rndv->protocol == VAPI_PROTOCOL_R3) {
        if (sreq->mrail.d_entry != NULL) {
            dreg_unregister(sreq->mrail.d_entry);
            sreq->mrail.d_entry = NULL;
        }
        if (1 == sreq->mrail.rndv_buf_alloc
                && NULL != sreq->mrail.rndv_buf) {
            MPIU_Free(sreq->mrail.rndv_buf);
            sreq->mrail.rndv_buf_alloc = 0;
            sreq->mrail.rndv_buf = NULL;
        }
        sreq->mrail.remote_addr = NULL;
        /* Initialize this completion counter to 0
         * required for even striping */
        sreq->mrail.completion_counter = 0;

        for (hca_index = 0; hca_index < rdma_num_hcas; hca_index ++)
            sreq->mrail.rkey[hca_index] = 0;
        sreq->mrail.protocol = VAPI_PROTOCOL_R3;
    } else {
        sreq->mrail.remote_addr = rndv->buf_addr;
        for (hca_index = 0; hca_index < rdma_num_hcas; hca_index ++)
            sreq->mrail.rkey[hca_index] = rndv->rkey[hca_index];

        DEBUG_PRINT("[add rndv list] addr %p, key %p\n",
                sreq->mrail.remote_addr,
                sreq->mrail.rkey[0]);
        if (1 == sreq->mrail.rndv_buf_alloc) {
            int mpi_errno = MPI_SUCCESS;
            int i;
            uintptr_t buf;

            buf = (uintptr_t) sreq->mrail.rndv_buf;
            for (i = 0; i < sreq->dev.iov_count; i++) {
                memcpy((void *) buf, sreq->dev.iov[i].MPID_IOV_BUF,
                        sreq->dev.iov[i].MPID_IOV_LEN);
                buf += sreq->dev.iov[i].MPID_IOV_LEN;
            }

            /* TODO: Following part is a workaround to deal with 
             * datatype with large number of segments. 
             * We check if the datatype has finished 
             * loading and reload if not.
             * May be better interface with 
             * upper layer should be considered */

            while (sreq->dev.OnDataAvail == 
                    MPIDI_CH3_ReqHandler_SendReloadIOV) {
                sreq->dev.iov_count = MPID_IOV_LIMIT;
                mpi_errno =
                    MPIDI_CH3U_Request_load_send_iov(sreq,
                            sreq->dev.iov,
                            &sreq->dev.iov_count);
                /* --BEGIN ERROR HANDLING-- */
                if (mpi_errno != MPI_SUCCESS) {
                    ibv_error_abort(IBV_STATUS_ERR, "Reload iov error");
                }
                for (i = 0; i < sreq->dev.iov_count; i++) {
                    memcpy((void *) buf, sreq->dev.iov[i].MPID_IOV_BUF,
                            sreq->dev.iov[i].MPID_IOV_LEN);
                    buf += sreq->dev.iov[i].MPID_IOV_LEN;
                }
            }
        }
    }
    return MPI_SUCCESS;
}

void MRAILI_RDMA_Put_finish(MPIDI_VC_t * vc, 
        MPID_Request * sreq, int rail)
{
    MPIDI_CH3_Pkt_rput_finish_t rput_pkt;
    MPID_IOV iov;
    int n_iov = 1;
    int nb;
    int mpi_errno = MPI_SUCCESS;

    vbuf *buf;

    rput_pkt.type = MPIDI_CH3_PKT_RPUT_FINISH;
    rput_pkt.receiver_req_id = sreq->mrail.partner_id;
    iov.MPID_IOV_BUF = &rput_pkt;
    iov.MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_rput_finish_t);

    DEBUG_PRINT("Sending RPUT FINISH\n");

    mpi_errno =
        MPIDI_CH3I_MRAILI_rput_complete(vc, &iov, n_iov, &nb, &buf, rail);
    if (mpi_errno != MPI_SUCCESS && 
            mpi_errno != MPI_MRAIL_MSG_QUEUED) {
        ibv_error_abort(IBV_STATUS_ERR,
                "Cannot send rput through send/recv path");
    }

    buf->sreq = (void *) sreq;

    /* mark MPI send complete when VIA send completes */

    DEBUG_PRINT("VBUF ASSOCIATED: %p, %08x\n", buf, buf->desc.u.sr.wr_id);
}

void MRAILI_RDMA_Get_finish(MPIDI_VC_t * vc, 
        MPID_Request * rreq, int rail)
{
    MPIDI_CH3_Pkt_rget_finish_t rget_pkt;
    MPID_IOV iov;
    int n_iov = 1;
    int nb;
    int mpi_errno = MPI_SUCCESS;

    vbuf *buf;

    rget_pkt.type = MPIDI_CH3_PKT_RGET_FINISH;
    rget_pkt.sender_req_id = rreq->dev.sender_req_id;
    iov.MPID_IOV_BUF = &rget_pkt;
    iov.MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_rget_finish_t);

    DEBUG_PRINT("Sending RGET FINISH\n");

    mpi_errno =
        MPIDI_CH3I_MRAILI_rget_finish(vc, &iov, n_iov, &nb, &buf, rail);
    if (mpi_errno != MPI_SUCCESS && 
            mpi_errno != MPI_MRAIL_MSG_QUEUED) {
        ibv_error_abort(IBV_STATUS_ERR,
                "Cannot send rput through send/recv path");
    }

    buf->sreq = (void *) rreq;

    DEBUG_PRINT("VBUF ASSOCIATED: %p, %08x\n", buf, buf->desc.u.sr.wr_id);
}

void MPIDI_CH3I_MRAILI_Rendezvous_rget_push(MPIDI_VC_t * vc,
        MPID_Request * rreq)
{
    vbuf *v;
    int rail, disp, s_total, inc;
    int nbytes, rail_index;

    int count_rail;

    int mapped[MAX_NUM_SUBRAILS], i;
    int actual_index[MAX_NUM_SUBRAILS];

    double time, myseed;

    if (rreq->mrail.rndv_buf_off != 0) {
        ibv_error_abort(GEN_ASSERT_ERR,
                "s->bytes_sent != 0 Rendezvous Push, %d",
                rreq->mrail.nearly_complete);
    }

    for(rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
        if(MPIDI_CH3I_RDMA_Process.has_apm && apm_tester) {
            perform_manual_apm(vc->mrail.rails[rail_index].qp_hndl);
        }
    }

    rreq->mrail.completion_counter = 0;

    rreq->mrail.num_rdma_read_completions = 0;

    if (rreq->mrail.rndv_buf_sz > 0) {
#ifdef DEBUG
        assert(rreq->mrail.d_entry != NULL);
        assert(rreq->mrail.remote_addr != NULL);
#endif
    }

    /* Use the HSAM Functionality */
    if(MPIDI_CH3I_RDMA_Process.has_hsam && 
            (rreq->mrail.rndv_buf_sz > striping_threshold)) {

        memset(mapped, 0, rdma_num_rails * sizeof(int));
        memset(actual_index, 0, rdma_num_rails * sizeof(int));

        get_sorted_index(vc, actual_index);
    
        /* Get the wall-time, internally defined function */
        get_wall_time(&time);

        /* Set the start time for the stripe and the 
         * finish time to be zero*/ 

        rreq->mrail.stripe_start_time = time;

        for(rail = 0; rail < rdma_num_rails; rail++) {

            rreq->mrail.initial_weight[rail] = 
                vc->mrail.rails[rail].s_weight;
            rreq->mrail.stripe_finish_time[rail] = 0;
        }

    }

    while (rreq->mrail.rndv_buf_off < 
            rreq->mrail.rndv_buf_sz) {
        nbytes = rreq->mrail.rndv_buf_sz - rreq->mrail.rndv_buf_off;

        if (nbytes > MPIDI_CH3I_RDMA_Process.maxtransfersize) {
            nbytes = MPIDI_CH3I_RDMA_Process.maxtransfersize;
        }

        DEBUG_PRINT("[buffer content]: %02x,%02x,%02x, "
                "offset %d, remote buf %p\n",
                ((char *) rreq->mrail.rndv_buf)[0],
                ((char *) rreq->mrail.rndv_buf)[1],
                ((char *) rreq->mrail.rndv_buf)[2],
                rreq->mrail.rndv_buf_off, rreq->mrail.remote_addr);
        
        if (nbytes <= striping_threshold) {
            v = get_vbuf();
            v->sreq = rreq;

            rail = MRAILI_Send_select_rail(vc);

            MRAILI_RDMA_Get(vc, v,
                    (char *) (rreq->mrail.rndv_buf) +
                    rreq->mrail.rndv_buf_off,
                    ((dreg_entry *)rreq->mrail.d_entry)->
                    memhandle[vc->mrail.rails[rail].hca_index]->lkey,
                    (char *) (rreq->mrail.remote_addr) +
                    rreq->mrail.rndv_buf_off,
                    rreq->mrail.rkey[vc->mrail.rails[rail].hca_index],
                    nbytes, rail);

            rreq->mrail.num_rdma_read_completions++;

        } else if(!MPIDI_CH3I_RDMA_Process.has_hsam) {
            inc = nbytes / rdma_num_rails;
            
            for(rail = 0; rail < rdma_num_rails - 1; rail++) {
                v = get_vbuf();
                v->sreq = rreq;
                MRAILI_RDMA_Get(vc, v,
                        (char *) (rreq->mrail.rndv_buf) +
                        rreq->mrail.rndv_buf_off + rail * inc,
                        ((dreg_entry *)rreq->mrail.d_entry)->
                        memhandle[vc->mrail.rails[rail].hca_index]->lkey,
                        (char *) (rreq->mrail.remote_addr) +
                        rreq->mrail.rndv_buf_off + rail * inc,
                        rreq->mrail.rkey[vc->mrail.rails[rail].hca_index], 
                        inc, rail);
                rreq->mrail.num_rdma_read_completions++;
                /* Send the finish message immediately after the data */  
            }
            v = get_vbuf();
            v->sreq = rreq;
            MRAILI_RDMA_Get(vc, v,
                    (char *) (rreq->mrail.rndv_buf) +
                    rreq->mrail.rndv_buf_off + inc * (rdma_num_rails - 1),
                    ((dreg_entry *)rreq->mrail.d_entry)->
                    memhandle[vc->mrail.rails[rail].hca_index]->lkey,
                    (char *) (rreq->mrail.remote_addr) +
                    rreq->mrail.rndv_buf_off + inc * (rdma_num_rails - 1),
                    rreq->mrail.rkey[vc->mrail.rails[rail].hca_index], 
                    nbytes - (rdma_num_rails - 1) * inc, rail);
            rreq->mrail.num_rdma_read_completions++;

        } else {
            rail = 0;
            count_rail = 0;
           
            s_total = 0;

            while(count_rail <( rdma_num_rails / stripe_factor)) {
                if(vc->mrail.rails[actual_index[rail]].s_weight > 0) {
                    s_total += vc->mrail.rails[actual_index[rail]].s_weight;
                    mapped[count_rail] = actual_index[rail];
                    count_rail++;
                }
                rail = (rail + 1) % rdma_num_rails;
            }
            
            disp = 0;

            for(count_rail = 0; count_rail
                    < ((rdma_num_rails / stripe_factor) - 1);
                    count_rail++) {

                inc = vc->mrail.rails[mapped[count_rail]].s_weight *
                    (nbytes / s_total);
                
                if (inc <= 0) { 
                    continue;
                }
                
                v = get_vbuf();
                v->sreq = rreq;
                MRAILI_RDMA_Get(vc, v,
                        (char *) (rreq->mrail.rndv_buf) +
                        rreq->mrail.rndv_buf_off + disp,
                        ((dreg_entry *)rreq->mrail.d_entry)->
                        memhandle[vc->mrail.
                        rails[mapped[count_rail]].hca_index]->lkey,
                        (char *) (rreq->mrail.remote_addr) +
                        rreq->mrail.rndv_buf_off + disp,
                        rreq->mrail.rkey[vc->mrail.
                        rails[mapped[count_rail]].hca_index],
                        inc, mapped[count_rail]);

                rreq->mrail.num_rdma_read_completions++;
                /* Send the finish message immediately after the data */
                disp += inc;
            }

            v = get_vbuf();
            v->sreq = rreq;
            MRAILI_RDMA_Get(vc, v,
                    (char *) (rreq->mrail.rndv_buf) +
                    rreq->mrail.rndv_buf_off + disp,
                    ((dreg_entry *)rreq->mrail.d_entry)->
                    memhandle[vc->mrail.
                    rails[mapped[count_rail]].hca_index]->lkey,
                    (char *) (rreq->mrail.remote_addr) +
                    rreq->mrail.rndv_buf_off + disp,
                    rreq->mrail.rkey[vc->mrail.
                    rails[mapped[count_rail]].hca_index],
                    nbytes - disp, mapped[count_rail]);
            rreq->mrail.num_rdma_read_completions++;

        }
        /* Send the finish message immediately after the data */  
        rreq->mrail.rndv_buf_off += nbytes; 
    }       
#ifdef DEBUG
    assert(rreq->mrail.rndv_buf_off == rreq->mrail.rndv_buf_sz);
#endif

    rreq->mrail.nearly_complete = 1;
}


/* Algorithm:
 *
 * if(size) is less than striping threshold
 *      select only one rail and send a message through this
 *  otherwise
 *      if HSAM is defined, use the best stripe_factor rails
 *      and send the message through them
 *  else
 *      stripe the message evenly through all paths 
 */

void MPIDI_CH3I_MRAILI_Rendezvous_rput_push(MPIDI_VC_t * vc,
        MPID_Request * sreq)
{
    vbuf *v;
    int rail, disp, s_total, inc;
    int nbytes, rail_index;

    int count_rail;

    int mapped[MAX_NUM_SUBRAILS], i;
    int actual_index[MAX_NUM_SUBRAILS];

    double time, myseed;

    if (sreq->mrail.rndv_buf_off != 0) {
        ibv_error_abort(GEN_ASSERT_ERR,
                "s->bytes_sent != 0 Rendezvous Push, %d",
                sreq->mrail.nearly_complete);
    }
    
    for(rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
        if(MPIDI_CH3I_RDMA_Process.has_apm && apm_tester) {
            perform_manual_apm(vc->mrail.rails[rail_index].qp_hndl);
        }
    }
    
    sreq->mrail.completion_counter = 0;

    if (sreq->mrail.rndv_buf_sz > 0) {
#ifdef DEBUG
        assert(sreq->mrail.d_entry != NULL);
        assert(sreq->mrail.remote_addr != NULL);
#endif
    }

    /* Use the HSAM Functionality */
    if(MPIDI_CH3I_RDMA_Process.has_hsam && 
            (sreq->mrail.rndv_buf_sz > striping_threshold)) {

        memset(mapped, 0, rdma_num_rails * sizeof(int));
        memset(actual_index, 0, rdma_num_rails * sizeof(int));

        get_sorted_index(vc, actual_index);
    
        /* Get the wall-time, internally defined function */
        get_wall_time(&time);

        /* Set the start time for the stripe and the 
         * finish time to be zero*/ 

        sreq->mrail.stripe_start_time = time;

        for(rail = 0; rail < rdma_num_rails; rail++) {

            sreq->mrail.initial_weight[rail] = 
                vc->mrail.rails[rail].s_weight;
            sreq->mrail.stripe_finish_time[rail] = 0;
        }

    }

    while (sreq->mrail.rndv_buf_off < sreq->mrail.rndv_buf_sz) {
        nbytes = sreq->mrail.rndv_buf_sz - sreq->mrail.rndv_buf_off;

        if (nbytes > MPIDI_CH3I_RDMA_Process.maxtransfersize) {
            nbytes = MPIDI_CH3I_RDMA_Process.maxtransfersize;
        }

        DEBUG_PRINT("[buffer content]: %02x,%02x,%02x, offset %d, remote buf %p\n",
                ((char *) sreq->mrail.rndv_buf)[0],
                ((char *) sreq->mrail.rndv_buf)[1],
                ((char *) sreq->mrail.rndv_buf)[2],
                sreq->mrail.rndv_buf_off, sreq->mrail.remote_addr);
        
        if (nbytes <= striping_threshold) {
            v = get_vbuf();
            v->sreq = sreq;

            rail = MRAILI_Send_select_rail(vc);

            MRAILI_RDMA_Put(vc, v,
                    (char *) (sreq->mrail.rndv_buf) +
                    sreq->mrail.rndv_buf_off,
                    ((dreg_entry *)sreq->mrail.d_entry)->
                    memhandle[vc->mrail.rails[rail].hca_index]->lkey,
                    (char *) (sreq->mrail.remote_addr) +
                    sreq->mrail.rndv_buf_off,
                    sreq->mrail.rkey[vc->mrail.rails[rail].hca_index],
                    nbytes, rail);
           
        } else if(!MPIDI_CH3I_RDMA_Process.has_hsam) {
            inc = nbytes / rdma_num_rails;
            
            for(rail = 0; rail < rdma_num_rails - 1; rail++) {
                v = get_vbuf();
                v->sreq = sreq;
                MRAILI_RDMA_Put(vc, v,
                        (char *) (sreq->mrail.rndv_buf) +
                        sreq->mrail.rndv_buf_off + rail * inc,
                        ((dreg_entry *)sreq->mrail.d_entry)->
                        memhandle[vc->mrail.rails[rail].hca_index]->lkey,
                        (char *) (sreq->mrail.remote_addr) +
                        sreq->mrail.rndv_buf_off + rail * inc,
                        sreq->mrail.rkey[vc->mrail.rails[rail].hca_index], 
                        inc, rail);
                /* Send the finish message immediately after the data */  
            }
            v = get_vbuf();
            v->sreq = sreq;
            MRAILI_RDMA_Put(vc, v,
                    (char *) (sreq->mrail.rndv_buf) +
                    sreq->mrail.rndv_buf_off + inc * (rdma_num_rails - 1),
                    ((dreg_entry *)sreq->mrail.d_entry)->
                    memhandle[vc->mrail.rails[rail].hca_index]->lkey,
                    (char *) (sreq->mrail.remote_addr) +
                    sreq->mrail.rndv_buf_off + inc * (rdma_num_rails - 1),
                    sreq->mrail.rkey[vc->mrail.rails[rail].hca_index], 
                    nbytes - (rdma_num_rails - 1) * inc, rail);

        } else {
            rail = 0;
            count_rail = 0;
           
            s_total = 0;

            while(count_rail <( rdma_num_rails / stripe_factor)) {
                if(vc->mrail.rails[actual_index[rail]].s_weight > 0) {
                    s_total += vc->mrail.rails[actual_index[rail]].s_weight;
                    mapped[count_rail] = actual_index[rail];
                    count_rail++;
                }
                rail = (rail + 1) % rdma_num_rails;
            }
            
            disp = 0;

            for(count_rail = 0; count_rail
                    < ((rdma_num_rails / stripe_factor) - 1);
                    count_rail++) {

                inc = vc->mrail.rails[mapped[count_rail]].s_weight *
                    (nbytes / s_total);

                if (inc <= 0) { 
                    continue;
                    
                }
                
                v = get_vbuf();
                v->sreq = sreq;
                MRAILI_RDMA_Put(vc, v,
                        (char *) (sreq->mrail.rndv_buf) +
                        sreq->mrail.rndv_buf_off + disp,
                        ((dreg_entry *)sreq->mrail.d_entry)->
                        memhandle[vc->mrail.
                        rails[mapped[count_rail]].hca_index]->lkey,
                        (char *) (sreq->mrail.remote_addr) +
                        sreq->mrail.rndv_buf_off + disp,
                        sreq->mrail.rkey[vc->mrail.
                        rails[mapped[count_rail]].hca_index],
                        inc, mapped[count_rail]);

                /* Send the finish message immediately after the data */
                disp += inc;
            }

            v = get_vbuf();
            v->sreq = sreq;
            MRAILI_RDMA_Put(vc, v,
                    (char *) (sreq->mrail.rndv_buf) +
                    sreq->mrail.rndv_buf_off + disp,
                    ((dreg_entry *)sreq->mrail.d_entry)->
                    memhandle[vc->mrail.
                    rails[mapped[count_rail]].hca_index]->lkey,
                    (char *) (sreq->mrail.remote_addr) +
                    sreq->mrail.rndv_buf_off + disp,
                    sreq->mrail.rkey[vc->mrail.
                    rails[mapped[count_rail]].hca_index],
                    nbytes - disp, mapped[count_rail]);


        }
        /* Send the finish message immediately after the data */  
        sreq->mrail.rndv_buf_off += nbytes; 
    }       
#ifdef DEBUG
    assert(sreq->mrail.rndv_buf_off == sreq->mrail.rndv_buf_sz);
#endif

    /* Send the finish message through the rails */
  

    for(rail = 0; rail < rdma_num_rails; rail++) { 
        MRAILI_RDMA_Put_finish(vc, sreq, rail);
    }

    sreq->mrail.nearly_complete = 1;
}

/* Algorithm:
 * if (message size < striping threshold)
 *     mark as complete, independent of the rendezvous protocol
 * 
 * if (rendezvous protocol == RDMA Read)
 *     only one finish is expected, mark as complete
 *
 * if (rendezvous protocol == RDMA Write)
 *     rdma_num_rails finish messages are expected
 *     check this condition and mark complete accordingly
 */

int MPIDI_CH3I_MRAIL_Finish_request(MPID_Request *rreq)
{
    rreq->mrail.completion_counter++;

    if(rreq->mrail.protocol == VAPI_PROTOCOL_RGET) {
        return 1;
    }

    if(rreq->mrail.completion_counter < rdma_num_rails) {
        return 0;
    }
    
    return 1;
}

/* Get the sorted indices for the given array */
void get_sorted_index(MPIDI_VC_t *vc, int *b)
{               
    int *taken;

    int i, j, max = -1, index = 0;

    taken = (int *)malloc(sizeof(int) * rdma_num_rails);

    /* Sanity */ 
    memset(taken, 0, sizeof(int) * rdma_num_rails);
    
    /* Sort the array */
    for(i = 0; i < rdma_num_rails; i++) {
        for(j = 0; j < rdma_num_rails; j++) {
            if((vc->mrail.rails[j].s_weight >= max)
                    && (taken[j] != -1)) {
                max = vc->mrail.rails[j].s_weight;
                index = j;
            }
        }
        taken[index] = -1;
        b[i] = index;

#ifdef DEBUG
        assert((index >= 0) && (index < rdma_num_rails));
#endif

        max = -1;
    }

    /* Free the buffer */
    free(taken);
}



#undef FUNCNAME 
#define FUNCNAME adjust_weights 
#undef FCNAME       
#define FCNAME MPIDI_QUOTE(FUNCNAME)

void adjust_weights(MPIDI_VC_t *vc, double start_time,
    double *finish_time,
    double *init_weight)
{
    int i;
    double bw[MAX_NUM_SUBRAILS];
    double bw_total = 0;
    int weight_assigned = 0;
    int count_rails_used = 0;
    int rail_used[MAX_NUM_SUBRAILS];

    memset(rail_used, 0, sizeof(int) * MAX_NUM_SUBRAILS);

    for (i = 0; i < rdma_num_rails; i++) {

        /* This rail was used at all */
        if(finish_time[i] > 0) {
            finish_time[i] -= start_time;
            assert(finish_time[i] > 0);
            finish_time[i] /= 100;
            bw[i] = (init_weight[i]) / (double)(finish_time[i]);
            bw_total += bw[i];
            rail_used[i] = 1;
            count_rails_used++;
        }
    }

    for (i = 0; i < rdma_num_rails; i++) {
        /* Only update if the path is used */
        if(rail_used[i]){
        
        /* Use a linear model for path updates to tolerate
         * jitter from the network */

            vc->mrail.rails[i].s_weight =
               (int) (alpha * ((count_rails_used * DYNAMIC_TOTAL_WEIGHT *
                        bw[i] / (bw_total * rdma_num_rails)))
                + (1 - alpha) * vc->mrail.rails[i].s_weight);
            assert(vc->mrail.rails[i].s_weight > 0);
        
        }
        if ( vc->mrail.rails[i].s_weight >= 0) {
            weight_assigned += vc->mrail.rails[i].s_weight;
        }
    }
}

/* User defined function for wall-time */
int get_wall_time(double *t)
{
    struct timeval tv;
    static int initialized = 0;
    static int sec_base;
   
    gettimeofday(&tv, NULL);
   
    if (!initialized) {
        sec_base = tv.tv_sec;
        initialized = 1;
    }
    
    *t = (double) (tv.tv_sec - sec_base) * 1.0 + 
        (double) tv.tv_usec * 1.0e-6;
}
