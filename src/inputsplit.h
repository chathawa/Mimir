#ifndef MIMIR_INPUT_SPLIT_H
#define MIMIR_INPUT_SPLIT_H

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace MIMIR_NS {

struct FileSeg{
    std::string filename;
    uint64_t    filesize;
    uint64_t    startpos;
    uint64_t    segsize;
    int         start_rank;
    int         end_rank;
};

class InputSplit{
  public:
    InputSplit(const char *filepath){
        _get_file_list(filepath, 1);
        fileidx = 0;
    }

    InputSplit(){
        fileidx = 0;
    }

    ~InputSplit(){
    }

    FileSeg *get_next_file() {
        if( fileidx >= filesegs.size() ) {
            fileidx = 0;
            return NULL;
        }

        return &filesegs[fileidx++];
    }

    uint64_t get_max_fsize() {

        uint64_t max_fsize = 0;
        FileSeg *fileseg = NULL;

        while((fileseg = get_next_file()) != NULL){
            if(fileseg->segsize > max_fsize)
                max_fsize = fileseg->segsize;
        }

        return max_fsize;
    }

    void add(const char*filepath) { _get_file_list(filepath, 1); }

    uint64_t get_file_count() { return filesegs.size(); }
    void add_seg_file(FileSeg *seg) { filesegs.push_back(*seg); }
    void clear() { filesegs.clear(); }

    void print();

  private:
    void _get_file_list(const char*, int);

    std::vector<FileSeg> filesegs;
    size_t               fileidx;
};

}

#endif