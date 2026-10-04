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
 for (int n : {20,32,64,256}) {
  for (int k=0;k<12;++k) {
   ei::matrix_t a(33,n);
   std::vector<float> b(33*n);
   for(int i=0;i<33*n;++i) a.buffer[i]=b[i]=k ? sinf(i*.137f*k)*100 : 0;
   ei::numpy::ReusableFFTPlan plan;
   for(int row=0;row<33;++row) assert(ei::numpy::dct2(b.data()+row*n,n,ei::DCT_NORMALIZATION_ORTHO,&plan)==0);
   assert(ei::numpy::dct2(&a,ei::DCT_NORMALIZATION_ORTHO)==0);
   assert(memcmp(a.buffer,b.data(),b.size()*sizeof(float))==0);
  }
 }
 puts("PASS: 48 matrices, cached rotations vs original, bit-identical");
}
