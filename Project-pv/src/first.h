#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
// chafaaouchaou

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>


#define menu 0
#define but 3
#define datetheur 4
void pvcreation (){

   
}

void rendermenu(SDL_Renderer *wajiha,SDL_Texture * maltato,int*stat_of_game){

if(*stat_of_game==menu)
{

SDL_Rect rectongle;


//CHARGER EN MIMOIRE
SDL_QueryTexture(maltato,NULL,NULL,&rectongle.w,&rectongle.h);


rectongle.x= 210;// *i  ;   //(600)-rectongle.w)/2;
rectongle.y=  250;//*i    ;  //(600)-rectongle.h)/2;
rectongle.w=200;
rectongle.h=200;



//AFFICHAGE DE L'IMAGE

SDL_RenderCopy(wajiha,maltato,NULL,&rectongle);

}

}

void changestatfrommenu (int *stat_of_game,int a ,int b)
{
 if(*stat_of_game==menu && a>229 && a<395 && b>257 && b<324 ){
    *stat_of_game=but;
 }



}




void write (SDL_Event event,char UserName[],int *NLetters,int limite){

printf("5555555555555555555555555555555555\n");

	switch( event.key.keysym.sym ){
					case SDLK_a:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'a';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_b:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'b';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_c:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'c';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_d:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'd';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_e:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'e';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_f:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'f';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_g:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'g';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_h:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'h';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_i:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'i';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_j:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'j';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_k:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'k';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_l:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'l';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_m:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'm';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_n:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'n';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_o:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'o';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_p:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'p';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_q:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'q';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_r:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'r';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_s:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 's';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_t:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 't';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_u:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'u';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_v:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'v';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_w:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'w';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_x:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'x';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_y:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'y';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_z:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = 'z';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_1:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '1';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_2:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '2';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_3:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '3';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_4:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '4';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_5:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '5';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_6:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '6';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_7:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '7';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_8:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '8';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_9:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '9';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_0:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '0';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_BACKSPACE:
						if (strlen(UserName) > 0){
							UserName[*NLetters-1] = '\0';
							*NLetters=*NLetters-1;
						}					
						break;
					case SDLK_SPACE:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = ' ';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;
					case SDLK_SLASH:
						if (strlen(UserName) < limite){
							UserName[*NLetters] = '/';
							UserName[*NLetters+1] = '\0';
							*NLetters=*NLetters+1;
						}
						break;	
					}
				
}





void affichtext(int *stat_of_game, SDL_Renderer *wajiha,SDL_Surface * surface,char toto[],SDL_Texture * texture,TTF_Font * font,SDL_Color color,SDL_Rect textrect,int t,int kk)
{

surface=TTF_RenderText_Solid(font,toto,color);
texture=SDL_CreateTextureFromSurface(wajiha,surface);



SDL_FreeSurface(surface);


textrect.x= kk;
textrect.y= t;


SDL_QueryTexture(texture,NULL,NULL,&textrect.w,&textrect.h);
SDL_RenderCopy(wajiha, texture, NULL,&textrect);



SDL_DestroyTexture(texture);



}


