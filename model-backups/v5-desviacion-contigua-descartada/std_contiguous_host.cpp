#include "edge-impulse-sdk/dsp/numpy.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
void *ei_malloc(size_t n) { return malloc(n); }
void *ei_calloc(size_t n,size_t s) { return calloc(n,s); }
void ei_free(void *p) { free(p); }
void ei_printf(const char *,...) {}
uint64_t ei_read_timer_us() { return 0; }
int main() {
 for(int rows : {1,2,33,49,101}) for(int cols : {1,13,20}) for(int k=0;k<8;++k) {
  ei::matrix_t input(rows,cols), a(cols,1), b(cols,1);
  for(int i=0;i<rows*cols;++i) input.buffer[i]=k ? sinf(i*.173f*k)*100 : 0;
  assert(ei::numpy::std_axis0(&input,&a)==0);
  assert(ei::numpy::std_axis0_contiguous(&input,&b)==0);
  assert(memcmp(a.buffer,b.buffer,cols*sizeof(float))==0);
 }
 puts("PASS: 120 matrices, bit-identical standard deviations");
}
