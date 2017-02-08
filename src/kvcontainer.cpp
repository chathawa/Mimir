#include <string.h>
#include <string>
#include "log.h"
#include "const.h"
#include "kvcontainer.h"
#include "recordformat.h"

using namespace MIMIR_NS;

KVContainer::KVContainer(int ksize, int vsize) {
    this->ksize = ksize;
    this->vsize = vsize;
    kv.set_kv_size(ksize, vsize);
    kvcount = 0;
    page = NULL;
    pageoff = 0;
}

KVContainer::~KVContainer() {
}

bool KVContainer::open() {
    page = NULL;
    return true;
}

void KVContainer::close() {
    page = NULL;
}

KVRecord* KVContainer::read(){
    char *ptr;
    int kvsize;

    printf("page = %p\n", page);

    if (page == NULL || pageoff >= page->datasize) {
        page = get_next_page();
        pageoff = 0;
        if (page == NULL)
            return NULL;
    }

    ptr = page->buffer + pageoff;
    kv.set_buffer(ptr);
    kvsize = kv.get_record_size();

    //printf("getkv: kvsize=%d, pageoff=%d\n", kvsize, pageoff);

    pageoff += kvsize;
    return &kv;
}

void KVContainer::write(BaseRecordFormat *record) {
    if (page == NULL)
        page = add_page();
    //printf("add: key=%s\n", key);
    int kvsize = record->get_record_size();
    if (kvsize > pagesize)
        LOG_ERROR("Error: KV size (%d) is larger \
                  than one page (%ld)\n", kvsize, pagesize);

    if (kvsize > (pagesize - page->datasize))
        page = add_page();

    char *ptr = page->buffer + page->datasize;
    kv.set_buffer(ptr);
    kv.convert(record);
    page->datasize += kvsize;

    kvcount += 1;
}
