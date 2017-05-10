/*
 * (c) 2016 by University of Delaware, Argonne National Laboratory, San Diego 
 *     Supercomputer Center, National University of Defense Technology, 
 *     National Supercomputer Center in Guangzhou, and Sun Yat-sen University.
 *
 *     See COPYRIGHT in top-level directory.
 */
#ifndef MIMIR_COMBINE_COLLECTIVE_SHUFFLER_H
#define MIMIR_COMBINE_COLLECTIVE_SHUFFLER_H

#include <unordered_map>

#include "container.h"
#include "collectiveshuffler.h"

namespace MIMIR_NS {

class CombineCollectiveShuffler 
    : public CollectiveShuffler, public Combinable 
{
public:
    CombineCollectiveShuffler(MPI_Comm comm,
                              CombineCallback user_combine,
                              void *user_ptr,
                              Writable *out,
                              HashCallback user_hash);
    ~CombineCollectiveShuffler();

    virtual bool open();
    virtual void write(BaseRecordFormat *);
    virtual void update(BaseRecordFormat *);
    virtual void close();

    virtual void make_progress(bool issue_new = false) {
        garbage_collection();
        exchange_kv(); 
    }

private:
    void garbage_collection();

    CombineCallback user_combine;
    void *user_ptr;
    std::unordered_map<char*, int> slices;
    CombinerHashBucket *bucket;
    CombinerUnique *u;
};

}
#endif
