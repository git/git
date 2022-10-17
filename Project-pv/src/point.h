#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

// chafaaouchaou
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>


void remplirepoints(int * stat_of_game , SDL_Renderer *wajiha,SDL_Surface * surface,SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect)
{

if(*stat_of_game!=menu && *stat_of_game!=but &&*stat_of_game!=datetheur&&*stat_of_game!=membres  )
{

surface=TTF_RenderText_Solid(font,"les points :",color);

texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);



textrect.x= 0;
textrect.y=  200;  
textrect.w=10;
textrect.h=10;



SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);


SDL_DestroyTexture(texture);
}


}



void affichlogospoints(int *stat_of_game,int member,SDL_Renderer *wajiha,SDL_Texture * ppoint,SDL_Texture * npoint ,SDL_Texture * pnpoint)
{
 if(*stat_of_game==poins){

   
SDL_Rect rectongle;


//CHARGER EN MIMOIRE
if(point==1){

SDL_QueryTexture(npoint,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,npoint,NULL,&rectongle);

}else if(point==2){
    
SDL_QueryTexture(pnpoint,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=80;
rectongle.h=80;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,pnpoint,NULL,&rectongle);
}else {
      
SDL_QueryTexture(ppoint,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,ppoint,NULL,&rectongle);


}
}


}




void changethpoints(int *stat_of_game,int a ,int b,int*point)
{
   if(*stat_of_game==poins && *point==1 && a>502 && a<557 && b>401 && b<458)
   {
        *point= *point+1 ;
   }else  if(*stat_of_game==poins && *point==2 && a>503 && a<573 && b>400 && b<434)
   {
        *point= *point-1 ;
   }else  if(*stat_of_game==poins && *point==2 && a>501 && a<573 && b>441 && b<478)
   {
        *point= *point+1 ;
   }else  if(*stat_of_game==poins && *point==3 && a>503 && a<555 && b>404 && b<455)
   {
        *point= *point-1 ;
   }
   
   
   


}