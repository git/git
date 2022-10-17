
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
// chafaaouchaou

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

void fin (int *stat_of_game,SDL_Renderer *wajiha,SDL_Texture * pm,SDL_Texture * gdt,char toto[21] ) 
{
    
    if(*stat_of_game==tach && toto[0]!='\0')
       {
           SDL_Rect rectongle;
           
SDL_QueryTexture(gdt,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 250;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  500;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=150;
rectongle.h=100;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,gdt,NULL,&rectongle);



         
SDL_QueryTexture(pm,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 150;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  500;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=100;
rectongle.h=90;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,pm,NULL,&rectongle);





       }
}

void controlefin(int *stat_of_game,char toto[21],int a,int b){

    if(*stat_of_game==tach &&toto[0]!='\0'&& a>152 &&a<238 &&b>502 &&b<582)
    {
      *stat_of_game=menu;
      
but_de_la_reunion[0] = '\0';
dat_etheur_du_pv[0] = '\0';

person1[0]= '\0';
person2[0]= '\0';
person3[0]= '\0';
person4[0]= '\0';
person5[0]= '\0';

point1[0]= '\0';
point2[0]= '\0';
point3[0]= '\0';

desition1[0]= '\0';
desition2[0]= '\0';
desition3[0]= '\0';

task1[0]= '\0';
task2[0]= '\0';
task3[0]= '\0';
task4[0]= '\0';
task5[0]= '\0';

data[0]= '\0';

NLetters=0;
NLetterdate=0;

 member=1;
 member1=0;
 member2=0;
 member3=0;
 member4=0;
 member5=0;


 point=1;
 pa1=0;
 pa2=0;
 pa3=0;


 desdas=1;
 des1=0;
 des2=0;
 des3=0;


 task=1;
 takos1=0;
 takos2=0;
 takos3=0;
 takos4=0;
takos5=0;
longdate =0;



    }
}



void generertxt (int *stat_of_game,int a ,int b,char toto[21])
{
    if(*stat_of_game==tach && toto[0]!= '\0'&& a>255 && a<389 && b>503 && b<588 )
    {
        *stat_of_game=gn;
    }
}