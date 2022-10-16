#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "crc-lib-c/crcLib.h"

float freq = 10e3;

float* get_carrier(){ 
    float carrier[44100] ;
    for (int i = 0; i < 44100; i++) {
     carrier[i] = i/44100;
     carrier[i] = sin(2*M_PI*freq*carrier[i]);
    }
    return &carrier;
}

void filter(float* buf,int num) {
    char count = 0;
    int sum = 0;
    float* value_buf[num] = {0};
    int i=0;
    for (int ind=0; ind<sizeof(buf)/sizeof(float); ind++){
        value_buf[i++] = buf[ind];
        if(i == N) i = 0;
        for(count = 0; count < N; count++) {
            sum += value_buf[count];
        }
        buf[ind] = sum/num; 
    }    
}

float* m_dot2(int length, const float *x, const float *y) {
    float sum [length];
    for (int i = 0; i < length; i++)
        sum[i]= x[i] * y[i];
    return sum;
}

float sum_list(float* buf, int s,int e)[
    float sum=0;
    for(int i=s;i<=e;++i){
        sum+=buf[i]
    }
    return sum;
]

int* generate_crc_code(int* buf){
  int behind[8];
  uint8_t crc = crc8_maxim(buf, 100)
  for(int i=7;i>=0;--i){
      behind[7-i] = (crc>>i)&1;
      }
    return behind;
}

void decode(size_t* buf, int* decode_power_bit){
  if (sizeof(buf)/sizeof(size_t) == 44*108){
    float decode_remove_carrier[44*108];
    /*use smooth filter and decode*/
    decode_remove_carrier = filter(m_dot2(44*108,get_carrier(), buf),10);
    for (int j=0;j<108;++j){
      decode_power_bit[j] = sum_list(decode_remove_carrier,10+j*44,30+j*44);
    }
    /*normalize the value of bit to 1 and 0*/
    for(int i=0;i<length;++i){
        if (decode_power_bit[i]>0)decode_power_bit[i]=1;
        else decode_power_bit[i]=0;
    }
  }
}

bool crc_check(int* buf){
  int pre[100]
  int behind[8];
  memcpy(pre, buf, 100);
  uint8_t crc = crc8_maxim(pre, 100)
  for(int i=7;i>=0;--i){
      behind[7-i] = (crc>>i)&1;
      if (buf[107-i]!=behind[7-i]){
        return false;
      }
  }
  return true;

}
