/*
  print-prime.c
  By David Broman.
  Last modified: 2015-09-15
  Modified by Leonardo Filippeschi: 2020-02-09
  This file is in the public domain.
*/


#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define COLUMNS 6
int print=0;

int is_prime(int n){
  for(int i=2; i<=(pow(n,0.5)); i++){
    if((n%i)==0){
      return 0;
    }
  }
  return 1;
}

void print_number(int n){
  if((print%COLUMNS)==0){
    printf("\n");
    printf("%10d ",n);
  } else
    printf("%10d ",n);
  print++;
}


void print_primes(int n){
  // Should print out all prime numbers less than 'n'
  // with the following formatting. Note that
  // the number of columns is stated in the define
  // COLUMNS

  printf("%10d ", 2);
  printf("%10d ", 3);
  printf("%10d ", 5);
  printf("%10d ", 7);
  printf("%10d ", 11);
  printf("%10d ", 13);
  for(int i=14;i<n;i++){
    if(is_prime(i))
      print_number(i);
	}
}

  // 'argc' contains the number of program arguments, and
  // 'argv' is an array of char pointers, where each
  // char pointer points to a null-terminated string.
  int main(int argc, char *argv[]){
    if(argc == 2)
      print_primes(atoi(argv[1]));
    else
      printf("Please state an interger number.\n");
    return 0;
  }

 
