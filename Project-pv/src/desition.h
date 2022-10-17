
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

// chafaaouchaou
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>




void remplirdesition(int * stat_of_game , SDL_Renderer *wajiha,SDL_Surface * surface,SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect)
{

if(*stat_of_game!=menu && *stat_of_game!=but &&*stat_of_game!=datetheur&&*stat_of_game!=membres &&*stat_of_game!=poins )
{

surface=TTF_RenderText_Solid(font,"les decisions :",color);

texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);



textrect.x= 0;
textrect.y=  280;  
textrect.w=10;
textrect.h=10;



SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);


SDL_DestroyTexture(texture);
}


}



void affichlogosdes(int *stat_of_game,int desdas,SDL_Renderer *wajiha,SDL_Texture * pdes,SDL_Texture * ndes ,SDL_Texture * pndes)
{
 if(*stat_of_game==desition){

   
SDL_Rect rectongle;


//CHARGER EN MIMOIRE
if(desdas==1){

SDL_QueryTexture(ndes,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,ndes,NULL,&rectongle);

}else if(desdas==2){
    
SDL_QueryTexture(pndes,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=80;
rectongle.h=80;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,pndes,NULL,&rectongle);
}else {
      
SDL_QueryTexture(pdes,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,pdes,NULL,&rectongle);


}
}


}




void changethdesitions(int *stat_of_game,int a ,int b,int*desdas)
{
   if(*stat_of_game==desition && *desdas==1 && a>502 && a<557 && b>401 && b<458)
   {
        *desdas= *desdas+1 ;
   }else  if(*stat_of_game==desition && *desdas==2 && a>506 && a<565 && b>401 && b<436)
   {
        *desdas= *desdas-1 ;
   }else  if(*stat_of_game==desition && *desdas==2 && a>505 && a<567 && b>442 && b<476)
   {
        *desdas= *desdas+1 ;
   }else  if(*stat_of_game==desition && *desdas==3 && a>502 && a<556 && b>400 && b<456)
   {
        *desdas= *desdas-1 ;
   }
   
   
   


}