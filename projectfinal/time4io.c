/* time4io.c

   Written by 2020-02-09 by L Filippeschi 

   For copyright and licensing, see file COPYING */



#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pic32mx.h>
#include "mipslab.h"

int getsw(void){
  volatile int button;
  return button=(PORTD>>8)&0xf;
}

int getbtns(void){
  volatile int rtrn=0;
  TRISF &= 0x2;
  rtrn |= (PORTD>>4)&0xe | (PORTF>>1)&0x1;
  //  TRISF |= 0x2;
  return  rtrn;
}

