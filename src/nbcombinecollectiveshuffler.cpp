/*
 * (c) 2016 by University of Delaware, Argonne National Laboratory, San Diego 
 *     Supercomputer Center, National University of Defense Technology, 
 *     National Supercomputer Center in Guangzhou, and Sun Yat-sen University.
 *
 *     See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include "stat.h"
#include "config.h"
#include "memory.h"
#include "hash.h"
#include "recordformat.h"
#include "kvcontainer.h"
#include "nbcombinecollectiveshuffler.h"

using namespace MIMIR_NS;

NBCombineCollectiveShuffler::NBCombineCollectiveShuffler(MPI_Comm comm,
                                                         CombineCallback user_combine,
                                                         void *user_ptr,
                                                         Writable *out,
                                                         HashCallback user_hash)
    : NBCollectiveShuffler(comm, out, user_hash)
{
    this->user_combine = user_combine;
    this->user_ptr = user_ptr;
    bucket = NULL;
}

NBCombineCollectiveShuffler::~NBCombineCollectiveShuffler()
{
}

bool NBCombineCollectiveShuffler::open() {
    NBCollectiveShuffler::open();
    bucket = new CombinerHashBucket();

    return true;
}

void NBCombineCollectiveShuffler::close() {
    garbage_collection();
    delete bucket;

    NBCollectiveShuffler::close();
}

void NBCombineCollectiveShuffler::write(BaseRecordFormat *record)
{
    int target = get_target_rank(((KVRecord*)record)->get_key(),
                                 ((KVRecord*)record)->get_key_size());
    if (target == shuffle_rank) {
        out->write(record);
        return;
    }

    int kvsize = record->get_record_size();
    if (kvsize > buf_size)
        LOG_ERROR("Error: KV size (%d) is larger than buf_size (%ld)\n", 
                  kvsize, buf_size);

    u = bucket->findElem(((KVRecord*)record)->get_key(), 
                         ((KVRecord*)record)->get_key_size());

    if (u == NULL) {
        CombinerUnique tmp;
        tmp.next = NULL;

        std::unordered_map < char *, int >::iterator iter;
        char *range_start = msg_buffers[cur_idx].send_buffer + target * (int64_t)buf_size;
        char *range_end = msg_buffers[cur_idx].send_buffer + target * (int64_t)buf_size 
            + msg_buffers[cur_idx].send_offset[target];
        for (iter = slices.begin(); iter != slices.end(); iter++) {
            char *sbuf = iter->first;
            int ssize = iter->second;

            if (sbuf >= range_start && sbuf < range_end && ssize >= kvsize) {
                tmp.kv = sbuf + (ssize - kvsize);
                kv.set_buffer(tmp.kv);
                kv.convert((KVRecord*)record);

                if (iter->second == kvsize)
                    slices.erase(iter);
                else
                    slices[iter->first] -= kvsize;

                bucket->insertElem(&tmp);

                break;
            }
        }

        if (iter == slices.end()) {
            if ((int64_t)msg_buffers[cur_idx].send_offset[target] + (int64_t) kvsize > buf_size) {
                //while (!done_kv_exchange()) {
                //    push_kv_exchange();
                //}
                garbage_collection();
                start_kv_exchange();
            }
            tmp.kv = msg_buffers[cur_idx].send_buffer + target * (int64_t)buf_size 
                + msg_buffers[cur_idx].send_offset[target];
            kv.set_buffer(tmp.kv);
            kv.convert((KVRecord*)record);
            msg_buffers[cur_idx].send_offset[target] += kvsize;
        }

        bucket->insertElem(&tmp);
        kvcount ++;
    }
    else {
        kv.set_buffer(u->kv);
        user_combine(this, &kv, (KVRecord*)record, user_ptr);
    }

    return;
}

void NBCombineCollectiveShuffler::update(BaseRecordFormat *record)
{
    int target = get_target_rank(((KVRecord*)record)->get_key(),
                                 ((KVRecord*)record)->get_key_size());

    int kvsize = record->get_record_size();
    int ukvsize = kv.get_record_size();
    int ksize = kv.get_key_size();

    if (kvsize > buf_size)
        LOG_ERROR("Error: KV size (%d) is larger than buf_size (%ld)\n", 
                  kvsize, buf_size);

    if (((KVRecord*)record)->get_key_size() != kv.get_key_size()
        || memcmp(((KVRecord*)record)->get_key(), kv.get_key(), ksize) != 0)
        LOG_ERROR("Error: the result key of combiner is different!\n");

    if (kvsize <= ukvsize) {
        kv.convert((KVRecord*)record);
        if (kvsize < ukvsize)
            slices.insert(std::make_pair(kv.get_record() + kvsize, 
                                         ukvsize - kvsize));
    }
    else {
        slices.insert(std::make_pair(kv.get_record(), ukvsize));
        if ((int64_t)msg_buffers[cur_idx].send_offset[target] + (int64_t) kvsize > buf_size) {
            //while (!done_kv_exchange()) {
            //    push_kv_exchange();
            //}
            garbage_collection();
            start_kv_exchange();
            u = NULL;
        }
        char *gbuf = msg_buffers[cur_idx].send_buffer + target * (int64_t) buf_size 
            + msg_buffers[cur_idx].send_offset[target];
        kv.set_buffer(gbuf);
        kv.convert((KVRecord*)record);
        msg_buffers[cur_idx].send_offset[target] += kvsize;
        if (u != NULL) u->kv=gbuf;
    }

    return;
}

void NBCombineCollectiveShuffler::garbage_collection()
{
    if (!slices.empty()) {

        LOG_PRINT(DBG_GEN, "NBCollectiveShuffler garbage collection: slices=%ld\n",
                  slices.size());

        int dst_off = 0, src_off = 0;
        char *dst_buf = NULL, *src_buf = NULL;

        for (int k = 0; k < shuffle_size; k++) {
            src_buf = msg_buffers[cur_idx].send_buffer + k * (int64_t)buf_size;
            dst_buf = msg_buffers[cur_idx].send_buffer + k * (int64_t)buf_size;

            dst_off = src_off = 0;
            while (src_off < msg_buffers[cur_idx].send_offset[k]) {

                char *tmp_buf = src_buf + src_off;
                std::unordered_map < char *, int >::iterator iter = slices.find(tmp_buf);
                if (iter != slices.end()) {
                    src_off += iter->second;
                }
                else {
                    kv.set_buffer(tmp_buf);
                    int kvsize = kv.get_record_size();
                    if (src_off != dst_off) {
                        for (int kk = 0; kk < kvsize; kk++)
                            dst_buf[dst_off + kk] = src_buf[src_off + kk];
                    }
                    dst_off += kvsize;
                    src_off += kvsize;
                }
            }
            msg_buffers[cur_idx].send_offset[k] = dst_off;
        }
        slices.clear();
    }
    bucket->clear();
}
