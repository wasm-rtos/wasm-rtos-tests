#include "os.h"
#include "hal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct{uint8_t*d;uint32_t n;}Bin;static FILE*l;static int f;static void ok(int c,const char*s){printf("%s %s\n",c?"PASS":"FAIL",s);if(l){fprintf(l,"%s %s\n",c?"PASS":"FAIL",s);fflush(l);}if(!c)f++;}static int load(Bin*b){FILE*x=fopen("wasi_exit.wasm","rb");long