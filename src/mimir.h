#ifndef MIMIR_H
#define MIMIR_H

#include "interface.h"
#include "memory.h"
#include "recordformat.h"
#include "inputsplit.h"
#include "filesplitter.h"
#include "filereader.h"
#include "kvcontainer.h"
#include "mimircontext.h"

void Mimir_Init(int*, char ***, MPI_Comm);
void Mimir_Finalize();


#endif
