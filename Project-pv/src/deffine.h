#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
// chafaaouchaou

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>



#define menu 0
#define remplirePV 1
#define but 3
#define datetheur 4
#define membres 5
#define poins 6
#define desition 7
#define heurfin 8
#define tach 9 
#define gn 10



char but_de_la_reunion[81],dat_etheur_du_pv[51];
int NLetters = 0;

int stat_of_game=menu;
int NLetterdate=0;

int   a;
int  b;
int member=1;
int member1=0;
int member2=0;
int member3=0;
int member4=0;
int member5=0;

char person1[21];
char person2[21];
char person3[21];
char person4[21];
char person5[21];

int point=1;
int pa1=0;
int pa2=0;
int pa3=0;
char point1[81];
char point2[81];
char point3[81];

int desdas=1;
int des1=0;
int des2=0;
int des3=0;
char desition1[81];
char desition2[81];
char desition3[81];


int task=1;
int takos1=0;
int takos2=0;
int takos3=0;
int takos4=0;
int takos5=0;

char task1[81];
char task2[81];
char task3[81];
char task4[81];
char task5[81];

int longdate =0;
char data[21];