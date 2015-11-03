#include <string.h>
#include <sys/stat.h>
#include "dataobject.h"

#include "log.h"
#include "config.h"

using namespace MAPREDUCE_NS;

// FIXME: out of core design may face problems
//        when running in multi-threads environment 

DataObject::DataObject(
  DataType _datatype,
  int _blocksize,
  int _maxblock,
  int _maxmemsize,
  int _outofcore,
  std::string _filename){
  datatype = _datatype;

  blocksize = _blocksize * UNIT_SIZE;
  maxblock = _maxblock;
  maxmemsize = _maxmemsize * UNIT_SIZE;
  outofcore = _outofcore;
  filename = _filename;

  maxbuf = _maxmemsize / _blocksize;  

  nitem = nblock = nbuf = 0;

  blocks = new Block[maxblock];
  buffers = new Buffer[maxbuf];

  for(int i = 0; i < maxblock; i++){
    blocks[i].datasize = 0;
    blocks[i].bufferid = -1;
    blocks[i].infile   = 0;
    blocks[i].fileoff  = 0;
  }
  for(int i = 0; i < maxbuf; i++){
    buffers[i].buf = NULL;
    buffers[i].blockid = -1;
    buffers[i].ref = 0;
  }
 
  LOG_PRINT(DBG_DATA, "DataObject: create. (type=%d, blocksize=%d, maxblock=%d, maxmemsize=%d)\n", datatype, blocksize, maxblock, maxmemsize);
}

DataObject::~DataObject(){
  for(int i = 0; i < nbuf; i++){
    if(buffers[i].buf) free(buffers[i].buf);
  }
  delete [] blocks;
  delete [] buffers;

  LOG_PRINT(DBG_DATA, "%s", "DataObject: destory.\n");
}

/*
 * find a buffer which can bu used
 */
int DataObject::findbuffer(){
  int i;
  for(i = 0; i < nbuf; i++){
    if(buffers[i].ref == 0){
      int blockid = buffers[i].blockid;
      if(blockid != -1){
        int64_t off;
        if(blocks[blockid].infile)
          off = blocks[blockid].fileoff;
        else{
          struct stat statbuf;
          stat(filename.c_str(), &statbuf);
          off = statbuf.st_size;
        }
        FILE *fp = fopen(filename.c_str(), "wb+");
        if(!fp){
          printf("Error: cannot open data file!\n");
          return -1;
        }
        fseek(fp, off, SEEK_SET);
        fwrite(buffers[blockid].buf, blocksize, 1, fp);
        fclose(fp);
        blocks[blockid].bufferid = -1;
        blocks[blockid].infile = 1;
        blocks[blockid].fileoff = off;

        buffers[i].blockid = -1;
        buffers[i].ref     = 0;
        return i;
      }
    }
  }

  return -1;
}

/* 
 * acquire a block according to the blockid
 */
int DataObject::acquireblock(int blockid){
  if(blockid < nblock){
    int bufferid = blocks[blockid].bufferid;
    if(bufferid != -1){
      buffers[bufferid].ref++;
      return 0;
    }
    if(blocks[blockid].infile){
      int i = findbuffer();
      FILE *fp = fopen(filename.c_str(), "rb");
      if(!fp) return -1;
      fseek(fp, blocks[blockid].fileoff, SEEK_SET);
      size_t ret = fread(buffers[i].buf, blocksize, 1, fp);
      fclose(fp);
      buffers[i].blockid = blockid;
      buffers[i].ref = 1;
      blocks[blockid].bufferid = i;
      return 0;
    }
    return -1;
  }
  
  return -1;
}

/*
 * release a block according to block id
 */
void DataObject::releaseblock(int blockid){
  int bufferid = blocks[blockid].bufferid;
  if(bufferid != -1){
    buffers[bufferid].ref--;
  }
}

/*
 * get block empty space
 */
int DataObject::getblockspace(int blockid){
  //LOG_PRINT(DBG_GEN, "get block space, id=%d, blocksize=%d, datasize=%d\n", blockid, blocksize, blocks[blockid].datasize);
  return (blocksize - blocks[blockid].datasize);
}

/*
 * get offset of block tail
 */
int DataObject::getblocktail(int blockid){
  return blocks[blockid].datasize;
}

/*
 * add an empty block and return the block id
 */
int DataObject::addblock(){
  int blockid = __sync_fetch_and_add(&nblock, 1);
  if(blockid < maxblock){
    if(blockid < maxbuf){
      buffers[blockid].buf = (char*)malloc(blocksize);
      buffers[blockid].blockid = blockid;
      buffers[blockid].ref = 0;
      nbuf++;
      blocks[blockid].datasize = 0;
      blocks[blockid].bufferid = blockid;
      blocks[blockid].infile = 0;
      blocks[blockid].fileoff = 0;

      LOG_PRINT(DBG_GEN, "DataObejct: add block.(blockid=%d)\n", blockid);

      return blockid;
    }else{
      if(outofcore){
        int i = findbuffer();
        if(i == -1){
          printf("Error: cannot find empty buffer!\n");
          return -1;
        }
        buffers[i].blockid = blockid;
        buffers[i].ref     = 0;
        blocks[blockid].datasize = 0;
        blocks[blockid].bufferid = i;
        blocks[blockid].infile = 0;
        blocks[blockid].fileoff = 0;

        return blockid;
      }else{
        printf("Error: exceeds the max memory size!\n");
        return -1;
      }
    }
  }

  //printf("blockid=%d, nblock=%d, maxblock=%d\n", blockid, nblock, maxblock);
  LOG_ERROR("DataObject error in addblock exceed max block count nblock=%d, maxblock=%d!\n", nblock, maxblock);
  return -1;

}

/*
 * add a block with provided data
 *   data: data buffer
 *   datasize: bytes count of data
 * return block id if success, otherwise return -1;
 * can be used in multi-thread environment
 */
int DataObject::addblock(char *data, int datasize){
  if(datasize > blocksize){
    LOG_ERROR("Error in DataObejct::addblock: the data size exceeds one block size.(datasize=%d, blocksize=%d)\n", datasize, blocksize);
    return -1;
  }

  int blockid = addblock();
  acquireblock(blockid);
  int bufferid = blocks[blockid].bufferid;
  memcpy(buffers[bufferid].buf, data, datasize);
  blocks[blockid].datasize = datasize;
  releaseblock(blockid);

  LOG_PRINT(DBG_DATA, "DataObejct: add data into block.(blockid=%d, datasize=%d)\n", blockid, datasize);
}


int DataObject::adddata(int blockid, char *data, int datasize){
  if(blocks[blockid].datasize + datasize > blocksize){
    return -1;
  }
  int bufferid = blocks[blockid].bufferid;
  memcpy(buffers[bufferid].buf+blocks[blockid].datasize, data, datasize);
  blocks[blockid].datasize += datasize;
  
  LOG_PRINT(DBG_DATA, "DataObject: add data into block %d\n", blockid);
  return 0;
}

/*
 * get pointer of bytes
 */
int DataObject::getbytes(int blockid, int offset, char **ptr){
  int bufferid = blocks[blockid].bufferid;
  *ptr = buffers[bufferid].buf + offset;
  return 0;
}

/*
 * add bytes into a block
 */
int DataObject::addbytes(int blockid, char *buf, int len){
  int bufferid = blocks[blockid].bufferid;
  char *blockbuf = buffers[bufferid].buf;
  memcpy(blockbuf+blocks[blockid].datasize, buf, len);
  blocks[blockid].datasize += len;
  return blocks[blockid].datasize;
}

/*
 * print the bytes data in this object
 */
void DataObject::print(int type, FILE *fp, int format){
  int line = 10;
  for(int i = 0; i < nblock; i++){
    acquireblock(i);
    fprintf(stdout, "block %d, datasize=%d:", i, blocks[i].datasize);
    for(int j=0; j < blocks[i].datasize; j++){
      if(j % line == 0) fprintf(stdout, "\n");
      int bufferid = blocks[i].bufferid;
      fprintf(stdout, "  %02X", buffers[bufferid].buf[j]);
    }
    fprintf(stdout, "\n");
    releaseblock(i);
  }
}
