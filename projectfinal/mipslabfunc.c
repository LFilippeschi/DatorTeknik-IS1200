/* mipslabfunc.c
   This file written 2015 by F Lundevall
   Edited by L Filippeschi 2020
   Some parts are original code written by Axel Isaksson
   For copyright and licensing, see file COPYING */

#include <stdint.h>   /* Declarations of uint_32 and the like */
#include <pic32mx.h>  /* Declarations of system-specific addresses etc */
#include "mipslab.h"  /* Declatations for these labs */
#include <stdbool.h>

const int Field_X_Min = 1;
const int Field_X_Max = 127;
const int Field_Y_Min = 1;
const int Field_Y_Max = 23;

#define ACC_SENSOR_ADDR 0xd0
#define ACC_SENSOR_REG  0x3b
#define EEP_SENSOR_ADDR  0xa0

volatile int xPos=64;
volatile int yPos=21;
volatile int xAcc;
volatile int yAcc;
int out=1;
int partial=0;
int number=0;

/* Declare a helper function which is local to this file */
static void num32asc( char * s, int ); 

#define DISPLAY_CHANGE_TO_COMMAND_MODE (PORTFCLR = 0x10)
#define DISPLAY_CHANGE_TO_DATA_MODE (PORTFSET = 0x10)

#define DISPLAY_ACTIVATE_RESET (PORTGCLR = 0x200)
#define DISPLAY_DO_NOT_RESET (PORTGSET = 0x200)

#define DISPLAY_ACTIVATE_VDD (PORTFCLR = 0x40)
#define DISPLAY_ACTIVATE_VBAT (PORTFCLR = 0x20)

#define DISPLAY_TURN_OFF_VDD (PORTFSET = 0x40)
#define DISPLAY_TURN_OFF_VBAT (PORTFSET = 0x20)

/* quicksleep:
   A simple function to create a small delay.
   Very inefficient use of computing resources,
   but very handy in some special cases. */
void quicksleep(int cyc) {
  int i;
  for(i = cyc; i > 0; i--);
}

/* tick:
   Add 1 to time in memory, at location pointed to by parameter.
   Time is stored as 4 pairs of 2 NBCD-digits.
   1st pair (most significant byte) counts days.
   2nd pair counts hours.
   3rd pair counts minutes.
   4th pair (least significant byte) counts seconds.
   In most labs, only the 3rd and 4th pairs are used. */
void tick( unsigned int * timep )
{
  /* Get current value, store locally */
  register unsigned int t = * timep;
  t += 1; /* Increment local copy */
  
  /* If result was not a valid BCD-coded time, adjust now */

  if( (t & 0x0000000f) >= 0x0000000a ) t += 0x00000006;
  if( (t & 0x000000f0) >= 0x00000060 ) t += 0x000000a0;
  /* Seconds are now OK */

  if( (t & 0x00000f00) >= 0x00000a00 ) t += 0x00000600;
  if( (t & 0x0000f000) >= 0x00006000 ) t += 0x0000a000;
  /* Minutes are now OK */

  if( (t & 0x000f0000) >= 0x000a0000 ) t += 0x00060000;
  if( (t & 0x00ff0000) >= 0x00240000 ) t += 0x00dc0000;
  /* Hours are now OK */

  if( (t & 0x0f000000) >= 0x0a000000 ) t += 0x06000000;
  if( (t & 0xf0000000) >= 0xa0000000 ) t = 0;
  /* Days are now OK */

  * timep = t; /* Store new value */
}

/////////// I2C commands

/* Wait for I2C bus to become idle */
void i2c_idle() {
	while(I2C1CON & 0x1F || I2C1STAT & (1 << 14)); //TRSTAT
}

/* Send one byte on I2C bus, return ack/nack status of transaction */
bool i2c_send(uint8_t data) {
	i2c_idle();
	I2C1TRN = data;
	i2c_idle();
	//	while((I2C1STAT & (1 << 15)));
	return !(I2C1STAT & (1 << 15)); //ACKSTAT
}

/* Receive one byte from I2C bus */
uint8_t i2c_recv() {
	i2c_idle();
	I2C1CONSET = 1 << 3; //RCEN = 1
	i2c_idle();
	while(I2C1CON & 0x8);
	PORTE |= (1<<7);
	while(!(I2C1STAT & 0x2));
	PORTE |= (1<<6);
	int var = I2C1RCV;
	//	I2C1STATCLR = 1 << 6; //I2COV = 0
	return var;
}

/* Send acknowledge conditon on the bus */
void i2c_ack() {
	i2c_idle();
	I2C1CONCLR = 1 << 5; //ACKDT = 0
	I2C1CONSET = 1 << 4; //ACKEN = 1
}

/* Send not-acknowledge conditon on the bus */
void i2c_nack() {
	i2c_idle();
	I2C1CONSET = 1 << 5; //ACKDT = 1
	I2C1CONSET = 1 << 4; //ACKEN = 1
}

/* Send start conditon on the bus */
void i2c_start() {
	i2c_idle();
	I2C1CONSET = 1 << 0; //SEN
	i2c_idle();
}

/* Send restart conditon on the bus */
void i2c_restart() {
	i2c_idle();
	I2C1CONSET = 1 << 1; //RSEN
	i2c_idle();
}

/* Send stop conditon on the bus */
void i2c_stop() {
	i2c_idle();
	I2C1CONSET = 1 << 2; //PEN
	i2c_idle();
}


/* display_debug
   A function to help debugging.

   After calling display_debug,
   the two middle lines of the display show
   an address and its current contents.

   There's one parameter: the address to read and display.

   Note: When you use this function, you should comment out any
   repeated calls to display_image; display_image overwrites
   about half of the digits shown by display_debug.
*/   
void display_debug( volatile int * const addr )
{
  display_string( 1, "Addr" );
  display_string( 2, "Data" );
  num32asc( &textbuffer[1][6], (int) addr );
  num32asc( &textbuffer[2][6], *addr );
  display_update();
}

uint8_t spi_send_recv(uint8_t data) {
  while(!(SPI2STAT & 0x08));
  SPI2BUF = data;
  while(!(SPI2STAT & 1));
  return SPI2BUF;
}


void acc_init(void){

  i2c_start();
  i2c_send(ACC_SENSOR_ADDR);
  i2c_send(0x19); //smplrate 
  i2c_send(0x07);/* 1KHz sample rate */
  i2c_stop();

 
  i2c_start();
  i2c_send(ACC_SENSOR_ADDR);
  i2c_send(0x6b); //pwr management
  i2c_send(0x01);
  i2c_stop();

 
  i2c_start();
  i2c_send(ACC_SENSOR_ADDR);
  i2c_send(0x1a);//config
  i2c_send(0x00);/* Fs = 8KHz */
  i2c_stop();

 
  i2c_start();
  i2c_send(ACC_SENSOR_ADDR);
  i2c_send(0x1b); //gyro config
  i2c_send(0x18);/* Full scale range +/- 2000 degree/C */
  i2c_stop();

 
  i2c_start();
  i2c_send(ACC_SENSOR_ADDR);
  i2c_send(0x1c); //acc config
  i2c_send(0x00);/* Full scale range +/- 2g*/
  i2c_stop();

  
  i2c_start();
  i2c_send(ACC_SENSOR_ADDR);
  i2c_send(0x38); //interrupt
  i2c_send(0x01);
  i2c_stop();
  
}

void get_acc(int* xacc, int* yacc){
 
  i2c_start();
  i2c_send(ACC_SENSOR_ADDR);
  i2c_send(ACC_SENSOR_REG);
  
  
  i2c_start();
  i2c_send(ACC_SENSOR_ADDR + 1);
  xAcc = i2c_recv() << 8;
  i2c_ack();
  xAcc |= i2c_recv();
  *xacc=xAcc;
  i2c_ack();
  yAcc = i2c_recv() << 8;
  i2c_ack();
  yAcc |= i2c_recv();
  *yacc=yAcc;
  i2c_nack();
  i2c_stop();
}

void display_init(void) {
  DISPLAY_CHANGE_TO_COMMAND_MODE;
  quicksleep(10);
  DISPLAY_ACTIVATE_VDD;
  quicksleep(1000000);
	
  spi_send_recv(0xAE); //Set display Off
  DISPLAY_ACTIVATE_RESET;
  quicksleep(10);
  DISPLAY_DO_NOT_RESET;
  quicksleep(10);
	
  spi_send_recv(0x8D);//Set charge pump 
  spi_send_recv(0x14);//VCC generated by internal DC/DC circuit
	
  spi_send_recv(0xD9);//Set pre-changed period 
  spi_send_recv(0xF1);
	
  DISPLAY_ACTIVATE_VBAT;
  quicksleep(10000000);
	
  spi_send_recv(0xA1);//Set segment re-map  
  spi_send_recv(0xC8);//Set COM output scan direction 
	
  spi_send_recv(0xDA);//Set COM pins hardware configuration 
  spi_send_recv(0x20);//Set memory address mode 
	
  spi_send_recv(0xAF);//Set display on
}


void display_clear(void){
  int i, j;
  
  //clears display 
  for(i=0; i<3; i++){
    for(j=0; j<136; j++){
      display[i*136+j]=0;
    }
  }

  //clears support array
  for(i=0; i<24; i++){
    for(j=0; j<136; j++){
      support[i][j]=0;
    }
  }
}

void move_right(void){
  int i, j;
  for(i=0; i<3; i++){
    for(j=136-1; j>0; j--){
      display[i*136 + j]=display[i*136 + j-1];
    }
    display[i*136]=0;
  }

  //support array
  for(i=0; i<24; i++){
    for(j=136-1; j>0; j--){
     support[i][j]=support[i][j-1];
    }
    support[i][0]=0;
  }
}

void move_left(void){
  pagecount++;
  int i, j, y, tmp;
  tmp=0;
  y=0;
  for(i=0; i<24; i++){
    if(support[i][0]==1)
      tmp=1;
  }
  if(tmp && number==0)
    number=pagecount;
  if(tmp)
    out++;
  if(out==7)
    y=pagecount;
  if(out%7==0 && (y-number)==6){
    update_life();
    number=0;
    out=1;
  }
  
  for(i=0; i<3; i++){
    for(j=0; j<136-1; j++){
      display[i*136 + j]=display[i*136 + j+1];
    }
    display[i*136+ j]=0;
  }

  //support array
  for(i=0; i<24; i++){
    for(j=0; j<136-1; j++){
     support[i][j]=support[i][j+1];
    }
    support[i][135]=0;
  }
}

void display_string(int line, char *s) {
  int i;
  if(line < 0 || line >= 4)
    return;
  if(!s)
    return;
	
  for(i = 0; i < 16; i++)
    if(*s) {
      textbuffer[line][i] = *s;
      s++;
    } else
      textbuffer[line][i] = ' ';
}

void display_image(int x, const uint8_t *data) {
  int i, j;
	
  for(i = 0; i < 4; i++) {
    DISPLAY_CHANGE_TO_COMMAND_MODE;

    spi_send_recv(0x22); //Set page command
    spi_send_recv(i);    //page number
		
    spi_send_recv(x & 0xF);  //set low nybble of column
    spi_send_recv(0x10 | ((x >> 4) & 0xF)); //set high nybble of column
		
    DISPLAY_CHANGE_TO_DATA_MODE;
		
    for(j = 0; j < 32; j++)
      spi_send_recv(~data[i*32 + j]);
  }
}

void display_fimage(const uint8_t *data) {
  int i, j;

  for(i = 0; i < 4; i++) {
    DISPLAY_CHANGE_TO_COMMAND_MODE;

    spi_send_recv(0x22); //Set page command
    spi_send_recv(i);    //page number
		
    spi_send_recv(0x00);  //set low nybble of column
		
    DISPLAY_CHANGE_TO_DATA_MODE;
    for(j = 0; j < 128; j++)
      spi_send_recv(data[i*136 + j]);
  }
}

void display_update(void) {
  int i, j, k;
  int c;
  for(i = 0; i < 4; i++) {
    DISPLAY_CHANGE_TO_COMMAND_MODE;
    spi_send_recv(0x22);
    spi_send_recv(i);
		
    spi_send_recv(0x0);
    spi_send_recv(0x10);
		
    DISPLAY_CHANGE_TO_DATA_MODE;
		
    for(j = 0; j < 16; j++) {
      c = textbuffer[i][j];
      if(c & 0x80)
	continue;
			
      for(k = 0; k < 8; k++)
	spi_send_recv(font[c*8 + k]);
    }
  }
}

void update_score(void){
  score=(score+1)%100;
  int i,j,d,u;
  d= score/10;
  u= score-d*10;
  for(j=111; j<128; j++)
    display[3*136+j]=0;
  
  for(j=0; j<8; j++){
    display[3*136 +111 +j]= font[48*8+d*8+j];
  }
  for(j=0; j<8; j++){
    display[3*136 +111 +j +8]= font[48*8+u*8+j];
  }
  display_fimage(display);
}

void clear_eeprom(void){
  int i;
  
  i2c_start();
  i2c_send(EEP_SENSOR_ADDR);
  i2c_send(0x00);//Hbyte 
  i2c_send(0x80);//LByte
  i2c_send(0x00); //data
  i2c_stop();
}

void update_life(void){
  life--;
  int i,j;
  for(j=0; j<30; j++){
    display[3*136+j]=0;    
  }
   
  for(i=0; i<life; i++){
    for(j=0; j<8; j++){
      display[3*136 +j+ i*10]=heart[j];
    }
  }
  display_fimage(display);
  if(life==0){

    if (score>leader){
      i2c_start();
      i2c_send(EEP_SENSOR_ADDR);
      i2c_send(0x00);//Hbyte 
      i2c_send(0x80);//LByte
      i2c_send(score);
      i2c_stop();
    }
    
    //save score to eeprom to be debugged 

    /*
    if(eepromdata>10000)
      eepromdata=0;
    eepromdata+=1;
    i2c_start();
    i2c_send(EEP_SENSOR_ADDR);
    i2c_send(0x00);//Hbyte 
    i2c_send(0x80);//LByte
    i2c_send(eepromdata>>8);//eepromdata on the memory
    i2c_send(eepromdata);
    i2c_stop();

    i2c_start();
    i2c_send(EEP_SENSOR_ADDR);
    i2c_send(0x00);//Hbyte 
    i2c_send(0x80+(eepromdata));//LByte

    //score on the memory at register 80+eeprom *2
    i2c_send(score); 
    i2c_stop();
    */
    
    while(1){
      display_clear();
      display_fimage(gameover1);
      quicksleep(1000000);
      display_clear();
      display_fimage(gameover2);
      quicksleep(1000000);
      display_update();
      
    }
  } 
}

int rand(void){
  static int next = 3251;
  next = ((next * TMR3) / 100 ) % 10000;
  return next;
}
 
int randint(int n){
  return rand() % (n+1);

}
  
void spawn(int obj, int row, int dir){
  int i,j,tmp;
  uint8_t* x;
  switch(obj) {
  case 0:
    x=(uint8_t*) pear;
    break;
  case 1:
    x=(uint8_t*) banana;
    break;
  case 2:
    x=(uint8_t*) bomb;
    break;
  case 3:
    x=(uint8_t*) watermelon;
    break;
  default:
    break;
  }
    
  for(i=0; i<8; i++){
    display[row*136+128+i]=x[i];
  }

  for(i=0; i<8; i++)
    for(j=0; j<8; j++){
      tmp = i%8; // bit in the column 
      if(pow(2,tmp) & (display[row*136 + 128 + j])){
	if(obj == 2)
	  support[8*row + i][128+j]=2;
	else 
	  support[8*row + i][128+j]=1;
      }
    }
}

void bomb_explosion(void){
  display_clear();
  display_fimage(bomb1);
  quicksleep(1000000);
  display_clear();
  display_fimage(bomb2);
  quicksleep(1000000);
  display_clear();
  display_fimage(bomb3);
  quicksleep(1000000);
  display_clear();
  display_fimage(display);
  quicksleep(1000000);
  
  update_life();
  display_clear(); 
}

/* Helper function, local to this file.
   Converts a number to hexadecimal ASCII digits. */
static void num32asc( char * s, int n ) 
{
  int i;
  for( i = 28; i >= 0; i -= 4 )
    *s++ = "0123456789ABCDEF"[ (n >> i) & 15 ];
}

/*
 * nextprime
 * 
 * Return the first prime number larger than the integer
 * given as a parameter. The integer must be positive.
 */
#define PRIME_FALSE   0     /* Constant to help readability. */
#define PRIME_TRUE    1     /* Constant to help readability. */
int nextprime( int inval )
{
  register int perhapsprime = 0; /* Holds a tentative prime while we check it. */
  register int testfactor; /* Holds various factors for which we test perhapsprime. */
  register int found;      /* Flag, false until we find a prime. */

  if (inval < 3 )          /* Initial sanity check of parameter. */
    {
      if(inval <= 0) return(1);  /* Return 1 for zero or negative input. */
      if(inval == 1) return(2);  /* Easy special case. */
      if(inval == 2) return(3);  /* Easy special case. */
    }
  else
    {
      /* Testing an even number for primeness is pointless, since
       * all even numbers are divisible by 2. Therefore, we make sure
       * that perhapsprime is larger than the parameter, and odd. */
      perhapsprime = ( inval + 1 ) | 1 ;
    }
  /* While prime not found, loop. */
  for( found = PRIME_FALSE; found != PRIME_TRUE; perhapsprime += 2 )
    {
      /* Check factors from 3 up to perhapsprime/2. */
      for( testfactor = 3; testfactor <= (perhapsprime >> 1) + 1; testfactor += 1 )
	{
	  found = PRIME_TRUE;      /* Assume we will find a prime. */
	  if( (perhapsprime % testfactor) == 0 ) /* If testfactor divides perhapsprime... */
	    {
	      found = PRIME_FALSE;   /* ...then, perhapsprime was non-prime. */
	      goto check_next_prime; /* Break the inner loop, go test a new perhapsprime. */
	    }
	}
    check_next_prime:;         /* This label is used to break the inner loop. */
      if( found == PRIME_TRUE )  /* If the loop ended normally, we found a prime. */
	{
	  return( perhapsprime );  /* Return the prime we found. */
	} 
    }
  return( perhapsprime );      /* When the loop ends, perhapsprime is a real prime. */
} 

/*
 * itoa
 * 
 * Simple conversion routine
 * Converts binary to decimal numbers
 * Returns pointer to (static) char array
 * 
 * The integer argument is converted to a string
 * of digits representing the integer in decimal format.
 * The integer is considered signed, and a minus-sign
 * precedes the string of digits if the number is
 * negative.
 * 
 * This routine will return a varying number of digits, from
 * one digit (for integers in the range 0 through 9) and up to
 * 10 digits and a leading minus-sign (for the largest negative
 * 32-bit integers).
 * 
 * If the integer has the special value
 * 100000...0 (that's 31 zeros), the number cannot be
 * negated. We check for this, and treat this as a special case.
 * If the integer has any other value, the sign is saved separately.
 * 
 * If the integer is negative, it is then converted to
 * its positive counterpart. We then use the positive
 * absolute value for conversion.
 * 
 * Conversion produces the least-significant digits first,
 * which is the reverse of the order in which we wish to
 * print the digits. We therefore store all digits in a buffer,
 * in ASCII form.
 * 
 * To avoid a separate step for reversing the contents of the buffer,
 * the buffer is initialized with an end-of-string marker at the
 * very end of the buffer. The digits produced by conversion are then
 * stored right-to-left in the buffer: starting with the position
 * immediately before the end-of-string marker and proceeding towards
 * the beginning of the buffer.
 * 
 * For this to work, the buffer size must of course be big enough
 * to hold the decimal representation of the largest possible integer,
 * and the minus sign, and the trailing end-of-string marker.
 * The value 24 for ITOA_BUFSIZ was selected to allow conversion of
 * 64-bit quantities; however, the size of an int on your current compiler
 * may not allow this straight away.
 */
#define ITOA_BUFSIZ ( 24 )
char * itoaconv( int num )
{
  register int i, sign;
  static char itoa_buffer[ ITOA_BUFSIZ ];
  static const char maxneg[] = "-2147483648";
  
  itoa_buffer[ ITOA_BUFSIZ - 1 ] = 0;   /* Insert the end-of-string marker. */
  sign = num;                           /* Save sign. */
  if( num < 0 && num - 1 > 0 )          /* Check for most negative integer */
    {
      for( i = 0; i < sizeof( maxneg ); i += 1 )
	itoa_buffer[ i + 1 ] = maxneg[ i ];
      i = 0;
    }
  else
    {
      if( num < 0 ) num = -num;           /* Make number positive. */
      i = ITOA_BUFSIZ - 2;                /* Location for first ASCII digit. */
      do {
	itoa_buffer[ i ] = num % 10 + '0';/* Insert next digit. */
	num = num / 10;                   /* Remove digit from number. */
	i -= 1;                           /* Move index to next empty position. */
      } while( num > 0 );
      if( sign < 0 )
	{
	  itoa_buffer[ i ] = '-';
	  i -= 1;
	}
    }
  /* Since the loop always sets the index i to the next empty position,
   * we must add 1 in order to return a pointer to the first occupied position. */
  return( &itoa_buffer[ i + 1 ] );
}

//totwos convert an unsigned int to a signed int 
int totwos(int num){
  
  volatile int i,tmp,count,var;
  var = num;
  tmp=0;
  for(i=15 ; i>=0; i--){
    if((var>>i) & 1){
      if(i==15){
	tmp = -pow(2,i);
        var -=pow(2,i);
      }
      else {
	tmp += pow(2,i);
	var -= pow(2,i);
      }
    }
  }
  return tmp;
}



char * toArray(int num){
  int i,tmp;
  tmp=0;
  for(i =15 ; i>-1; i--){
    if(num>>i & 1){
      if(i==15){
	tmp = -pow(2,i+1);
	num += tmp;
      } else {
	tmp += pow(2,i+1);
	num -= tmp;
      }
    }
  }
  return itoaconv(tmp);
}


int pow(int base, int exponent){
  int i;
  int power = 1;
  for(i = 0; i < exponent; i++){
    power *= base;
  }
  return power;
}
 

void clear_fruit(int page){
  int i,j;
  for(i=xPos-8;i<(xPos+9);i++){
    display[(page)*136 + i] = 0;
    for(j=page*8; j < ((page*8)+8); j++) 
      support[j][i]=0; //clears support array
  }
}
  
 
void print_cursor (int x,int y,const uint8_t *data){
  xPos= x;
  yPos= y;
  int i,u;
  int page = yPos/8; //page value that will move the cursor
  u =  y % 8; // page
  for (i = 0; i < (sizeof(cursor)/sizeof(uint8_t)); i++ ){
    //iterate through cursor bits width
    if((u==0) && (yPos> 7)){
      display[((page-1)*136 + x) + i] |= 128; //((data[i]) - 64) ;  (better implementation if bigger image needs to be printed
      display[(page*136 + x) + i] |= 1; //((data[i]) / pow(2, (7-u)));
    } else
      display [(page*136 + x) + i] |= (data[i]) / pow(2, (7-u));
  }
    //fruit case;
  if((support[yPos][xPos]==1) || (support[yPos][xPos+1]==1)){ // upper side
    clear_fruit(yPos/8);
    update_score();
  }
  if((support[yPos+1][xPos]==1) || (support[yPos+1][xPos+1]==1) && (((yPos+1)/8)!=(yPos/8)) ){ //lower side 
    clear_fruit((yPos+1)/8);
    update_score(); 
  }

    //bomb case;
  if((support[yPos][xPos]==2) || (support[yPos][xPos+1]==2)){ // upper side
    clear_fruit(yPos/8);
    bomb_explosion();
  }
  if((support[yPos+1][xPos]==2) || (support[yPos+1][xPos+1]==2) && (((yPos+1)/8)!=(yPos/8)) ){ //lower side 
    clear_fruit((yPos+1)/8);
    bomb_explosion();
  }
}

void clear_cursor(void){
  int i = 0;
  int page = yPos/8; //page value that will move the cursor
  int u = 0;
  u =  yPos % 8; // page
  for (i = 0; i <  (sizeof(cursor)/sizeof(uint8_t)); i++){
    //iterate through cursor bits width
    if((u==0) && (yPos>7)){
      display[((page-1)*136 + xPos) + i ] &= ~128;//~((cursor[i]) - 128) ;
      display[((page)*136 + xPos) + i ] &= ~1 ;
    }else {
      display [(page*136 + xPos) + i ] &= (~((cursor[i]) / pow(2, (7-u))));
    }
  }
}
 
void move_cursor(void) {// move on axis and keep cursor in the field
  volatile int button = getbtns();

  if (button/8) { // move backwards on the X axis and keep cursor in the field
    
    if ((xPos-1) > Field_Y_Min){
      xPos--;     
    }
    button -=8;
  }

  if (button/4) {  //move on Y axis and keep cursor in the field
    if ((yPos+1) < Field_Y_Max) {
      yPos++;
    }
    button -=4;
  }

  if (button/2) {  //move on Y axis and keep cursor in the field
    if ( (yPos-1) > Field_Y_Min){
      yPos--;
    }
    button -=2;
  }
  if (button/1)   {  // moving +1 on the x axis
    if ((xPos +1)  < Field_X_Max){	
      xPos++;
    }
    button -=1;
  }	
}

void move_cursorAcc(void){
  volatile int hor, ver;
  hor=xAcc;
  ver=yAcc;

  if(ver>5000)
    if ( (yPos-1) > Field_Y_Min)
      yPos--;
  
  if(ver<(-5000))
    if ((yPos+1) < Field_Y_Max)
      yPos++;
  
  if(hor>5000)
    if ((xPos +1)  < Field_X_Max)
      xPos++;
  
  if(hor< (-5000))
    if ((xPos-1) > Field_Y_Min)
      xPos--;
  

}
