#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>

#include <iostream>
#include <string>
#include <list>
#include <vector>

#include <sys/stat.h>

#include <mpi.h>
#include <omp.h>

#include "mapreduce.h"
#include "dataobject.h"
#include "log.h"

#define MAXLINE 2048

using namespace MAPREDUCE_NS;

// Position in data object
struct Pos{

  Pos(
    int _off, 
    int _size,
    int _bid=0,
    int _nval=0){
    bid = _bid;
    nval = _nval;
    off = _off;
    size = _size;
  }

  int   bid;     // block id
  int   off;     // offset in block   
  int   size;    // size of data

  int   nval;    // value count
};

// used for merge
struct Unique
{
  char *key;             // unique key
  int keybytes;          // key size

  // the information of current search block
  int cur_size;
  int cur_nval;
  std::list<Pos> cur_pos;

  // the information of all blocks
  int size;
  int nval;
  std::list<Pos> pos;
};

MapReduce::MapReduce(MPI_Comm caller)
{
    comm = caller;
    MPI_Comm_rank(comm,&me);
    MPI_Comm_size(comm,&nprocs);

#pragma omp parallel
{
    tnum = omp_get_num_threads();    
}

    printf("tnum=%d\n", tnum);

    blocks = new int[tnum];   
    for(int i = 0; i < tnum; i++) blocks[i] = -1;

    data = NULL;
    myhash = NULL;
    c = new Alltoall(comm, tnum);
    c->setup(1, 2);

    addtype = -1;

    LOG_PRINT(DBG_GEN, "%s", "MapReduce: construction.\n");
}

MapReduce::~MapReduce()
{
    delete c;

    delete [] blocks;

    LOG_PRINT(DBG_GEN, "%s", "MapReduce: destroy.\n");
}

void MapReduce::disinputfiles(const char *filepath, int sharedflag, int recurse){
  getinputfiles(filepath, sharedflag, recurse);

  if(sharedflag){
        
  }
}

void MapReduce::getinputfiles(const char *filepath, int sharedflag, int recurse){
  // if shared, only process 0 read file names
  if(!sharedflag || (sharedflag && me == 0)){
    
    struct stat inpath_stat;
    int err = stat(filepath, &inpath_stat);
    if(err) LOG_ERROR("Error in get input files, err=%d\n", err);
    
    // regular file
    if(S_ISREG(inpath_stat.st_mode)){
      ifiles.push_back(std::string(filepath));
    // dir
    }else if(S_ISDIR(inpath_stat.st_mode)){
      
      struct dirent *ep;
      DIR *dp = opendir(filepath);
      if(!dp) LOG_ERROR("%s", "Error in get input files\n");
      
      while(ep = readdir(dp)){
        
        if(ep->d_name[0] == '.') continue;
       
        char newstr[MAXLINE]; 
        sprintf(newstr, "%s/%s", filepath, ep->d_name);
        err = stat(newstr, &inpath_stat);
        if(err) LOG_ERROR("Error in get input files, err=%d\n", err);
        
        // regular file
        if(S_ISREG(inpath_stat.st_mode)){
          ifiles.push_back(std::string(newstr));
        // dir
        }else if(S_ISDIR(inpath_stat.st_mode) && recurse){
          getinputfiles(newstr, sharedflag, recurse);
        }
      }
    }
  }  
}

/*
 * Map function: (input: files)
 *  filepath: input file path
 *  sharedflag: 0 for local file system, 1 for global file system
 *  recurse: 1 for recurse
 *  readmode: 0 for by word, 1 for by line
 *  mymap: map function
 *  myhash: hash function
 *  ptr: pointer
 */
uint64_t MapReduce::map(char *filepath, int sharedflag, int recurse, 
    int readmode, void (*mymap) (MapReduce *, char *, void *), void *ptr){
  // read file
  LOG_PRINT(DBG_GEN, "%s", "MapReduce: map start. (Files as input)\n");
  
  // distribute input files
  disinputfiles(filepath, sharedflag, recurse);

  addtype = 0;

  if(data) delete data;
  KeyValue *kv = new KeyValue(0);
  data = kv;
  c->init(data);

  int fcount = ifiles.size();
  for(int i = 0; i < fcount; i++){
    //std::cout << ifiles[i] << std::endl;
  }

  addtype = -1;
 
  return 0; 
}

/*
 * Map function: no communication
 */
uint64_t MapReduce::map_local(char *filepath, int sharedflag, int recurse,
    int readmode, void (*mymap)(MapReduce *, char *, void *), void *ptr){
  return 0;
}

uint64_t MapReduce::map(char *filepath, int sharedflag, int recurse, 
  void (*mymap) (MapReduce *, const char *, void *), void *ptr){
  // read file
  LOG_PRINT(DBG_GEN, "%s", "MapReduce: map start. (File name to mymap)\n");

  // distribute input files
  disinputfiles(filepath, sharedflag, recurse);

  addtype = 0;

  if(data) delete data;
  KeyValue *kv = new KeyValue(0);
  data = kv;
  c->init(data);

  int fcount = ifiles.size();

#pragma omp parallel for
  for(int i = 0; i < fcount; i++){
    //std::cout << ifiles[i] << std::endl;
    mymap(this, ifiles[i].c_str(), ptr);
  }

  addtype = -1;


  return 0;
}

uint64_t MapReduce::map_local(char *filepath, int sharedflag, int recurse, 
  void (*mymap) (MapReduce *, const char *, void *), void *ptr){

  // read file
  LOG_PRINT(DBG_GEN, "%s", "MapReduce: map local start. (File name to mymap)\n");

  // distribute input files
  disinputfiles(filepath, sharedflag, recurse);

  addtype = 1;

  if(data) delete data;
  KeyValue *kv = new KeyValue(0);
  data = kv;

  int fcount = ifiles.size();

#pragma omp parallel for
  for(int i = 0; i < fcount; i++){
    //std::cout << ifiles[i] << std::endl;
    mymap(this, ifiles[i].c_str(), ptr);
  }

  addtype = -1;


  return 0;
}

/*
 * Map function: (input: kv object)
 */
uint64_t MapReduce::map(MapReduce *mr, 
    void (*mymap)(MapReduce *, char *, int, char *, int, void*), void *ptr){

  LOG_PRINT(DBG_GEN, "%s", "MapReduce: map start. (KV as input)\n");

  addtype = 0;

  DataObject *data = mr->data;
  KeyValue *kv = new KeyValue(0);

  this->data = kv;

  c->init(kv);

  if(data->getDatatype() != KVType){
    LOG_ERROR("%s","Error in map_local: input object of map must be KV object\n");
    return -1;
  }

  KeyValue *inputkv = (KeyValue*)data;

  //inputkv->print();

#pragma omp parallel default(shared)
{
  int tid = omp_get_thread_num();
  
  char *key, *value;
  int keybytes, valuebytes;


#pragma omp for
  for(int i = 0; i < inputkv->nblock; i++){
    int offset = 0;

    inputkv->acquireblock(i);

    offset = inputkv->getNextKV(i, offset, &key, keybytes, &value, valuebytes);
  
    while(offset != -1){
      mymap(this, key, keybytes, value, valuebytes, ptr);

      offset = inputkv->getNextKV(i, offset, &key, keybytes, &value, valuebytes);
    }
    
    inputkv->releaseblock(i);
  }

  c->twait(tid);
}
  c->wait();

  if(data != NULL) delete data;
  
  addtype = -1;

  LOG_PRINT(DBG_GEN, "%s", "MapReduce: map end.\n");

  kv->print();

  return 0;
}


uint64_t MapReduce::map_local(MapReduce *mr, 
    void (*mymap)(MapReduce *, char *, int, char *, int, void *), void *ptr){

  LOG_PRINT(DBG_GEN, "%s", "MapReduce: map local start. (KV as input)\n");

  addtype = 1;

  DataObject *data = mr->data;
  KeyValue *kv = new KeyValue(0);

  this->data = kv;

  if(data->getDatatype() != KVType){
    LOG_ERROR("%s","Error in map_local: input object of map must be KV object\n");
    return -1;
  }

  KeyValue *inputkv = (KeyValue*)data;

#pragma omp parallel default(shared)
{
  int tid = omp_get_thread_num();
  
  char *key, *value;
  int keybytes, valuebytes;


#pragma omp for
  for(int i = 0; i < inputkv->nblock; i++){
    int offset = 0;

    inputkv->acquireblock(i);

    offset = inputkv->getNextKV(i, offset, &key, keybytes, &value, valuebytes);
  
    while(offset != -1){
      mymap(this, key, keybytes, value, valuebytes, ptr);

      offset = inputkv->getNextKV(i, offset, &key, keybytes, &value, valuebytes);
    }
    
    inputkv->releaseblock(i);
  }
}

  if(data != NULL) delete data;

  addtype = -1;

  LOG_PRINT(DBG_GEN, "%s", "MapReduce: map local end.\n");

  return 0;
}

/* convert KV to KMV */
uint64_t MapReduce::convert(){
  LOG_PRINT(DBG_GEN, "%s", "MapReduce: convert start.\n");

  KeyValue *kv = (KeyValue*)data;
  KeyMultiValue *kmv = new KeyMultiValue(0);

  //kv->print();

// handled by multi-threads
#pragma omp parallel
{
  int tid = omp_get_thread_num();
  int num = omp_get_num_threads();

  std::vector<std::list<Unique>> ht;

  DataObject *tmpdata = new DataObject(ByteType); 

  char *key, *value;
  int keybytes, valuebytes, ret;
  int keyoff, valoff;

  int nbucket = pow(2,20);
  ht.reserve(nbucket);
  for(int i = 0; i < nbucket; i++) ht.emplace_back();

  //((KeyValue*)(data))->print();

  // scan all kvs to gain the thread kvs
  for(int i = 0; i < data->nblock; i++){
    int offset = 0;

    kv->acquireblock(i);

    offset = kv->getNextKV(i, offset, &key, keybytes, &value, valuebytes, &keyoff, &valoff);

    while(offset != -1){
      uint32_t hid = hashlittle(key, keybytes, 0);
      if(hid % num == tid){ 
        int ibucket = hid % nbucket;
        std::list<Unique>& ul = ht[ibucket];
        std::list<Unique>::iterator u;
     
        // search to see if the key has been in the list
        for(u = ul.begin(); u != ul.end(); u++){
          if(memcmp(u->key, key, keybytes) == 0){
            u->cur_pos.push_back(Pos(valoff,valuebytes));
            u->cur_size += valuebytes;
            u->cur_nval++;
            break;
          }
        }
        // add the key to the list
        if(u==ul.end()){          
          ul.emplace_back();
          
          ul.back().key = new char[keybytes];
          memcpy(ul.back().key, key, keybytes);
          ul.back().keybytes = keybytes;
          
          ul.back().cur_pos.push_back(Pos(valoff,valuebytes));
          ul.back().cur_size=valuebytes;
          ul.back().cur_nval=1;

          ul.back().nval=0;
          ul.back().size=0;
        }
      }
      
      offset = kv->getNextKV(i, offset, &key, keybytes, &value, valuebytes, &keyoff, &valoff);
    }

    //((DataObject*)(kv))->print();

    // merge locally
    int blockid = tmpdata->addblock();
    tmpdata->acquireblock(blockid);

    std::vector<std::list<Unique>>::iterator ul;
    for(ul = ht.begin(); ul != ht.end(); ++ul){
      std::list<Unique>::iterator u;
      for(u = ul->begin(); u != ul->end(); ++u){
       // merge all values together
       int bytes = sizeof(int)*(u->cur_nval) + (u->cur_size);

       // if the block is full, add another block 
       if(tmpdata->getblockspace(blockid) < bytes){
          tmpdata->releaseblock(blockid);
          blockid = tmpdata->addblock();
          tmpdata->acquireblock(blockid);
        }
        // add sizes
        std::list<Pos>::iterator l;
        int off = tmpdata->getblocktail(blockid);
        for(l = u->cur_pos.begin(); l != u->cur_pos.end(); ++l){
          tmpdata->addbytes(blockid, (char*)&(l->size), (int)sizeof(int));
        }
        
        char *p;
        tmpdata->getbytes(blockid, off, &p);

        // add values
        for(l = u->cur_pos.begin(); l != u-> cur_pos.end(); ++l){
          char *p = NULL;
          kv->getbytes(i, l->off, &p);
          tmpdata->addbytes(blockid, p, l->size); 
        }
        u->nval += u->cur_nval;
        u->size += u->cur_size;
        u->pos.push_back(Pos(off,u->cur_size,blockid,u->nval));
        

        u->cur_nval = 0;
        u->cur_size = 0;
        u->cur_pos.clear();
      }
    }
    tmpdata->releaseblock(blockid);

    kv->releaseblock(i);
//#pragma omp barrier
  }

  //tmpdata->print();

  // merge kvs into kmv
  int blockid = -1;
  std::vector<std::list<Unique>>::iterator ul;
  for(ul = ht.begin(); ul != ht.end(); ++ul){
    std::list<Unique>::iterator u;
    for(u = ul->begin(); u != ul->end(); ++u){
      if(blockid == -1){
        blockid = kmv->addblock();
        kmv->acquireblock(blockid);
      }
      int bytes = sizeof(int)+(u->keybytes)+(u->nval+1)*sizeof(int)+(u->size);

      if(kmv->getblockspace(blockid) < bytes){
        kmv->releaseblock(blockid);
        blockid = kmv->addblock();
        kmv->acquireblock(blockid);
      }

      kmv->addbytes(blockid, (char*)&(u->keybytes), (int)sizeof(int));
      kmv->addbytes(blockid, u->key, u->keybytes);
      kmv->addbytes(blockid, (char*)&(u->nval), (int)sizeof(int));

      std::list<Pos>::iterator l;
      for(l = u->pos.begin(); l != u->pos.end(); ++l){
        char *p = NULL;
        tmpdata->getbytes(l->bid, l->off, &p);
        kmv->addbytes(blockid, p, sizeof(int)*(l->nval));
      }
      for(l = u->pos.begin(); l != u->pos.end(); ++l){
        char *p = NULL;
        tmpdata->getbytes(l->bid, l->off+sizeof(int)*(l->nval), &p);
        kmv->addbytes(blockid, p, l->size);
      }
      delete [] u->key;
    }
  }
  if(blockid != -1) kmv->releaseblock(blockid);

  //((DataObject*)(kmv))->print();

  delete tmpdata;
} 

  delete data;
  data = kmv;

  //kmv->print();

  LOG_PRINT(DBG_GEN, "%s", "MapReduce: convert end.\n");

  return 0;
}

uint64_t MapReduce::reduce(void (myreduce)(MapReduce *, char *, int, int, char *, 
    int *, void*), void* ptr){
  LOG_PRINT(DBG_GEN, "%s", "MapReduce: reduce start.\n");

  addtype = 2;

  KeyMultiValue *kmv = (KeyMultiValue*)data;
  KeyValue *kv = new KeyValue(1);
  data = kv;

  //kmv->print();

#pragma omp parallel
{
  int tid = omp_get_thread_num();
  int num = omp_get_num_threads();

  char *key, *values;
  int keybytes, nvalue, *valuebytes;
  blocks[tid] = -1;

#pragma omp for
  for(int i = 0; i < kmv->nblock; i++){
     int offset = 0;
     kmv->acquireblock(i);
     offset = kmv->getNextKMV(i, offset, &key, keybytes, nvalue, &values, &valuebytes);
     while(offset != -1){
       // apply myreudce here
       myreduce(this, key, keybytes, nvalue, values, valuebytes, ptr);        
       offset = kmv->getNextKMV(i, offset, &key, keybytes, nvalue, &values, &valuebytes);
     }
     kmv->releaseblock(i);
  }
}

  delete kmv;

  addtype = -1;

  //kv->print();
  LOG_PRINT(DBG_GEN, "%s", "MapReduce: reduce end.\n");

  return 0;
}

uint64_t MapReduce::scan(void (myscan)(char *, int, int, char *, int *,void *), void * ptr){
  KeyMultiValue *kmv = (KeyMultiValue*)data;

  char *key, *values;
  int keybytes, nvalue, *valuebytes;
  
  for(int i = 0; i < kmv->nblock; i++){
    int offset = 0;
    
    kmv->acquireblock(i);
    
    offset = kmv->getNextKMV(i, offset, &key, keybytes, nvalue, &values, &valuebytes);

    while(offset != -1){
      myscan(key, keybytes, nvalue, values, valuebytes, ptr);

      offset = kmv->getNextKMV(i, offset, &key, keybytes, nvalue, &values, &valuebytes);
    }

    kmv->releaseblock(i);
  }

}

/*
 * add a KV to this MR
 *  always invoked in mymap or myreduce function 
 *  must support multi-threads
 */
int MapReduce::add(char *key, int keybytes, char *value, int valuebytes){
  KeyValue *kv = (KeyValue*)data; 
 
  int tid = omp_get_thread_num();
 
  // invoked in map function
  if(addtype == 0){

    // get target process
    int target = 0;
    if(myhash != NULL) target=myhash(key, keybytes);
    else target = hashlittle(key, keybytes, nprocs);
    target %= nprocs;
    
    c->sendKV(tid, target, key, keybytes, value, valuebytes);

  // invoked in map_local or reduce function
  }else if(addtype == 1 || addtype == 2){
    if(blocks[tid] == -1){
      blocks[tid] = kv->addblock();
    }

    kv->acquireblock(blocks[tid]);

    while(kv->addKV(blocks[tid], key, keybytes, value, valuebytes) == -1){
      kv->releaseblock(blocks[tid]);
      blocks[tid] = kv->addblock();
      kv->acquireblock(blocks[tid]);
    }

    kv->releaseblock(blocks[tid]);
  }

  return 0;
}
