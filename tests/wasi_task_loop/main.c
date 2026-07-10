#include "os.h"
#include "hal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct{uint8_t*d;uint32_t n;}Bin;static FILE*l;static int f;static void ok(int c,const char*s){printf("%s %s\n",c?"PASS":"FAIL",s);if(l){fprintf(l,"%s %s\n",c?"PASS":"FAIL",s);fflush(l);}if(!c)f++;}static int load(Bin*b){FILE*x=fopen("wasi_task_loop.wasm","rb");long n;if(!x)return 0;fseek(x,0,SEEK_END);n=ftell(x);rewind(x);if(n<=0)return fclose(x),0;b->d=malloc((size_t)n);b->n=(uint32_t)n;if(!b->d||fread(b->d,1,(size_t)n,x)!=(size_t)n){fclose(x);free(b->