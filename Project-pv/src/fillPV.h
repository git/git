#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
// chafaaouchaou

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>


void rremplirebut(int * stat_of_game , SDL_Renderer *wajiha,SDL_Surface * surface,SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect)
{

if(*stat_of_game!=menu )
{
//int i=*stat_of_game;
//char tataka[200];sprintf(tataka,"%d",i);
surface=TTF_RenderText_Solid(font,"le but de la reunion :",color);
//surface=TTF_RenderText_Solid(font,tataka,color);
texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);



textrect.x= 0;
textrect.y=  0;  
textrect.w=10;
textrect.h=10;



SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);


SDL_DestroyTexture(texture);
}


}






void affichnext(SDL_Renderer *wajiha,SDL_Texture * next,int*stat_of_game)
{


if(*stat_of_game==but)
{

SDL_Rect rectongle;


//CHARGER EN MIMOIRE
SDL_QueryTexture(next,NULL,NULL,&rectongle.w,&rectongle.h);


rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  500;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;



//AFFICHAGE DE L'IMAGE

SDL_RenderCopy(wajiha,next,NULL,&rectongle);

}



}


void cliconnext(int*stat_of_game,int a,int b)
{
   if(*stat_of_game==but && a>500 && a<550 && b>507 && b<546)
   {
       *stat_of_game=datetheur;
   }
}



void rempliredateetheur(int * stat_of_game , SDL_Renderer *wajiha,SDL_Surface * surface,SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect)
{

if(*stat_of_game!=menu &&*stat_of_game!=but )
{

surface=TTF_RenderText_Solid(font,"La date et l'eur du pv :",color);
texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);



textrect.x= 0;
textrect.y=  40;  
textrect.w=10;
textrect.h=10;



SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);


SDL_DestroyTexture(texture);
}


}



void affichnp(SDL_Renderer *wajiha,SDL_Texture * nextpr,int*stat_of_game)
{


if(*stat_of_game==datetheur||*stat_of_game==membres||*stat_of_game==poins||*stat_of_game==desition||*stat_of_game==heurfin)
{

SDL_Rect rectongle;


//CHARGER EN MIMOIRE
SDL_QueryTexture(nextpr,NULL,NULL,&rectongle.w,&rectongle.h);


rectongle.x= 470;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  500;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=100;
rectongle.h=70;



//AFFICHAGE DE L'IMAGE

SDL_RenderCopy(wajiha,nextpr,NULL,&rectongle);

}



}


void changerlesetats(int*stat_of_game,int a ,int b){



if(*stat_of_game>but &&*stat_of_game<tach)
    {
        if(a>526 && a<568 && b>510 && b<552){
            *stat_of_game =*stat_of_game+1;
        }else if(a>471 && a<510 && b>511 && b<555){
              *stat_of_game =*stat_of_game-1;
        }
    }

if(*stat_of_game==tach && a>473 && a<509 && b>511 && b<552)
    *stat_of_game=*stat_of_game-1;

}



void affichprecident(SDL_Renderer *wajiha,SDL_Texture * precident,int*stat_of_game)
{


if(*stat_of_game==tach)
{

SDL_Rect rectongle;


//CHARGER EN MIMOIRE
SDL_QueryTexture(precident,NULL,NULL,&rectongle.w,&rectongle.h);


rectongle.x= 470;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  510;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=50;
rectongle.h=55;



//AFFICHAGE DE L'IMAGE

SDL_RenderCopy(wajiha,precident,NULL,&rectongle);

}



}
