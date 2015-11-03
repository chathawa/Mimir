#ifndef LOG_H
#define LOG_H

#define _DEBUG

#define DBG_GEN     1
#define DBG_DATA    2
#define DBG_COMM    4

#define DBG_LEVEL  0
//#define DBG_LEVEL  DBG_DATA
//#define DBG_LEVEL    DBG_GEN
//#define DBG_LEVEL  DBG_COMM
//#define DBG_LEVEL (DBG_GEN|DBG_DATA|DBG_COMM)

#ifdef _DEBUG
#define LOG_PRINT(type, format, ...) \
  {\
    if(DBG_LEVEL & (type)){\
      fprintf(stdout, (format), __VA_ARGS__);\
    }\
  }
#else
#define LOG_PRINT(type, format, ...)
#endif

#define LOG_WARNING(format, ...) \
  {\
    fprintf(stderr, format, __VA_ARGS__);\
  }

#define LOG_ERROR(format,...) \
  {\
    fprintf(stderr, format, __VA_ARGS__);\
    exit(1);\
  };

#endif
