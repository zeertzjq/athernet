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

