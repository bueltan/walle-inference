// Experimento de PC. No forma parte del firmware.
#include "edge-impulse-sdk/dsp/speechpy/processing.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <cstring>
void *ei_malloc(size_t n) { return malloc(n); }
void *ei_calloc(size_t n,size_t s) { return calloc(n,s); }
void ei_free(void *p) { free(p); }
void ei_printf(const char *,...) {}
uint64_t ei_read_timer_us() { return 0; }
// Dos pasadas como CMVN: centrar, rellenar otra vez, normalizar.
// Acumuladores double para reducir error; no garantiza igualdad con float.
void rolling(ei::matrix_t &x, int w) {
 int cols=x.cols, rows=x.rows, pad=(w-1)/2;
 ei::matrix_t padded(rows+2*pad,cols);
 for(int pass=0;pass<2;++pass) {
  assert(ei::numpy::pad_1d_symmetric(&x,&padded,pad,pad)==0);
  for(int c=0;c<cols;++c) {
   double sum=0, squares=0;
   for(int j=0;j<w;++j) { double v=padded.buffer[j*cols+c]; sum+=v; squares+=v*v; }
   for(int row=0;row<rows;++row) {
    if(pass==0) x.buffer[row*cols+c]-=static_cast<float>(sum/w);
    else {
     double variance=squares/w-(sum/w)*(sum/w);
     float sd=std::sqrt(std::max(0.0,variance));
     x.buffer[row*cols+c]=x.buffer[row*cols+c]/(sd+1e-10);
    }
    if(row+1<rows) {
     double old=padded.buffer[row*cols+c], next=padded.buffer[(row+w)*cols+c];
     sum-=old; sum+=next; squares-=old*old; squares+=next*next;
    }
   }
  }
 }
}
int main() {
 int unequal_cases=0, unequal_values=0, nonfinite=0; double max_error=0, generated_error=0;
 for(int k=0;k<120;++k) {
  ei::matrix_t a(33,13), b(33,13);
  for(int i=0;i<429;++i) {
   float v=k==0 ? 0 : k==1 ? 1.0f : k==2 ? 10000.0f+0.01f*std::sin(i*.13f) : std::sin(i*.137f*k)*100;
   a.buffer[i]=b.buffer[i]=v;
  }
  assert(ei::speechpy::processing::cmvnw(&a,49,true,false)==0);
  rolling(b,49);
  bool different=false;
  for(int i=0;i<429;++i) {
   if(memcmp(a.buffer+i,b.buffer+i,sizeof(float))) {different=true; ++unequal_values;}
   if(!std::isfinite(b.buffer[i])) ++nonfinite;
   double error=std::abs(double(a.buffer[i])-b.buffer[i]);
   max_error=std::max(max_error,error);
   if(k>=3) generated_error=std::max(generated_error,error);
  }
  unequal_cases+=different;
 }
 printf("Cases=120 different=%d; coefficients=51480 different=%d; max_abs_error=%.9g; nonfinite=%d\n",unequal_cases,unequal_values,max_error,nonfinite);
 printf("Generated signals max_abs_error=%.9g\n",generated_error);
}
