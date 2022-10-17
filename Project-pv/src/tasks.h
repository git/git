
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
// chafaaouchaou

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>


void remplirtasks(int * stat_of_game , SDL_Renderer *wajiha,SDL_Surface * surface,SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect)
{

if(*stat_of_game!=menu && *stat_of_game!=but &&*stat_of_game!=datetheur&&*stat_of_game!=membres &&*stat_of_game!=poins&&*stat_of_game!=desition )
{

surface=TTF_RenderText_Solid(font,"les taches :",color);

texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);



textrect.x= 0;
textrect.y= 360;  
textrect.w=10;
textrect.h=10;



SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);


SDL_DestroyTexture(texture);
}


}


void affichlogoTASK(int *stat_of_game,int task,SDL_Renderer *wajiha,SDL_Texture * ptask,SDL_Texture * ntask ,SDL_Texture * nptask)
{
 if(*stat_of_game==heurfin){

   
SDL_Rect rectongle;


//CHARGER EN MIMOIRE
if(task==1){

SDL_QueryTexture(ntask,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,ntask,NULL,&rectongle);

}else if(task==5){
    
SDL_QueryTexture(ptask,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,ptask,NULL,&rectongle);
}else {
      
SDL_QueryTexture(nptask,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=80;
rectongle.h=80;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,nptask,NULL,&rectongle);


}
}


}


void changethdetasks(int *stat_of_game,int a ,int b,int*task)
{
   if(*stat_of_game==heurfin && *task==1 && a>504 && a<555 && b>410 && b<452)
   {
        *task= *task+1 ;
   }else  if(*stat_of_game==heurfin && *task==5 && a>502 && a<551 && b>402 && b<450)
   {
        *task= *task-1 ;
   }else  if(*stat_of_game==heurfin &&*task!=1  && *task!=5&& a>507 && a<565 && b>402 && b<436)
   {
        *task= *task-1 ;
   }else  if(*stat_of_game==heurfin && *task!=1  && *task!=5 && a>505 && a<565 && b>442 && b<475)
   {
        *task= *task+1 ;
   }
   
   
   


}