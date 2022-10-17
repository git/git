
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
// chafaaouchaou

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>



void remplirdate(int * stat_of_game , SDL_Renderer *wajiha,SDL_Surface * surface,SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect)
{

if(*stat_of_game!=menu && *stat_of_game!=but &&*stat_of_game!=datetheur&&*stat_of_game!=membres &&*stat_of_game!=poins&&*stat_of_game!=desition &&*stat_of_game!=heurfin )
{

surface=TTF_RenderText_Solid(font,"la heure de fin :",color);

texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);



textrect.x= 0;
textrect.y= 480;  
textrect.w=10;
textrect.h=10;



SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);


SDL_DestroyTexture(texture);
}


}