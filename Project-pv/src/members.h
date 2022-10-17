#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

// chafaaouchaou
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>




void remplirelesmembres(int * stat_of_game , SDL_Renderer *wajiha,SDL_Surface * surface,SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect)
{

if(*stat_of_game!=menu && *stat_of_game!=but &&*stat_of_game!=datetheur  )
{

surface=TTF_RenderText_Solid(font,"les membres :",color);

texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);



textrect.x= 0;
textrect.y=  80;  
textrect.w=10;
textrect.h=10;



SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);


SDL_DestroyTexture(texture);
}


}






void affichnember(int * stat_of_game , SDL_Renderer *wajiha,SDL_Surface * surface,SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect,char toto[3],int z)
{

if(*stat_of_game!=menu && *stat_of_game!=but &&*stat_of_game!=datetheur  )
{

surface=TTF_RenderText_Solid(font,toto,color);

texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);



textrect.x= 0;
textrect.y=  z;  
textrect.w=10;
textrect.h=10;



SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);


SDL_DestroyTexture(texture);
}


}




void affichlogos(int *stat_of_game,int member,SDL_Renderer *wajiha,SDL_Texture * pnmem,SDL_Texture * nmem ,SDL_Texture * pmem)
{
 if(*stat_of_game==membres){

   
SDL_Rect rectongle;


//CHARGER EN MIMOIRE
if(member==1){

SDL_QueryTexture(nmem,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,nmem,NULL,&rectongle);

}else if(member==5){
    
SDL_QueryTexture(pmem,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=60;
rectongle.h=60;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,pmem,NULL,&rectongle);
}else {
      
SDL_QueryTexture(pnmem,NULL,NULL,&rectongle.w,&rectongle.h);
rectongle.x= 500;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  400;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=70;
rectongle.h=80;
//AFFICHAGE DE L'IMAGE
SDL_RenderCopy(wajiha,pnmem,NULL,&rectongle);


}
}


}

void changethemember(int *stat_of_game,int a ,int b,int*member)
{
   if(*stat_of_game==membres && *member==1 && a>503 && a<557 && b>403 && b<460)
   {
        *member= *member+1 ;
   }else  if(*stat_of_game==membres && *member==5 && a>503 && a<550 && b>403 && b<453)
   {
        *member= *member-1 ;
   }else  if(*stat_of_game==membres && *member!=5 && *member!=1 && a>503 && a<563 && b>402 && b<435)
   {
        *member= *member-1 ;
   }else  if(*stat_of_game==membres && *member!=5 && *member!=1 && a>507 && a<563 && b>443 && b<479)
   {
        *member= *member+1 ;
   }
   
   


}