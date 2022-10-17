#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

// chafaaouchaou

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "first.h"
#include "deffine.h"
#include "remplirePV.h"
#include "members.h"
#include "point.h"
#include "desition.h"
#include "tasks.h"
#include "date.h"
#include "end.h"


int main( int argc, char* argv[] ){


  


SDL_Init(SDL_INIT_VIDEO);


if (TTF_Init()<0)  {printf("probleme initialisation TTF");
 exit(EXIT_FAILURE);
}
IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

SDL_Window* nafida=NULL;
nafida = SDL_CreateWindow("model de droite",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,600,600,SDL_WINDOW_OPENGL);

  



 SDL_Renderer *wajiha=NULL;
wajiha=SDL_CreateRenderer(nafida,-1,SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);





FILE * fic=fopen("doc.txt","a");



fputs("this is a test yees\n",fic );
fputs("this is a test yees\n",fic );
fputs("this is a test yees\n",fic );

fclose(fic);


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


SDL_Texture * maltato ;
SDL_Surface * image=NULL;
image=IMG_Load("img/menu.png");
 maltato= SDL_CreateTextureFromSurface(wajiha,image);
 SDL_FreeSurface(image);








  SDL_Texture * timtext;
TTF_Font * font1= TTF_OpenFont("arial.ttf",20);
SDL_Color color={182,170,170,255};
SDL_Surface * surface;
SDL_Rect textrect;
TTF_Font * font2= TTF_OpenFont("arial.ttf",15);





SDL_Texture * next ;
image=IMG_Load("img/next.png");
next = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);





SDL_Texture * nextpr ;
image=IMG_Load("img/sv.png");
nextpr = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);




SDL_Texture * precident ;
image=IMG_Load("img/previous.png");
precident = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);





SDL_Texture * pnmem ;
image=IMG_Load("img/pnmem.png");
pnmem = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);

SDL_Texture * nmem ;
image=IMG_Load("img/nmem.png");
nmem = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);


SDL_Texture * pmem ;
image=IMG_Load("img/pmem.png");
pmem = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);



SDL_Texture * ppoint ;
image=IMG_Load("img/ppoint.png");
ppoint = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);



SDL_Texture * npoint ;
image=IMG_Load("img/npoint.png");
npoint = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);



SDL_Texture * pnpoint ;
image=IMG_Load("img/pnpoint.png");
pnpoint = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);



SDL_Texture * ndes ;
image=IMG_Load("img/ndes.png");
ndes = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);



SDL_Texture * pdes ;
image=IMG_Load("img/pdes.png");
pdes = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);


SDL_Texture * pndes ;
image=IMG_Load("img/pndes.png");
pndes = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);



SDL_Texture * nptask ;
image=IMG_Load("img/nptask.png");
nptask = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);


SDL_Texture * ptask ;
image=IMG_Load("img/ptask.png");
ptask = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);


SDL_Texture * ntask ;
image=IMG_Load("img/ntask.png");
ntask = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);


SDL_Texture * pm ;
image=IMG_Load("img/pm.png");
pm = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);


SDL_Texture * gdt ;
image=IMG_Load("img/gdt.png");
gdt = SDL_CreateTextureFromSurface(wajiha,image);
SDL_FreeSurface(image);








// gcc src/pv.c -o bin/prog -I include -L lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lSDL2_ttf


printf("5555555555555555555555555555555555");



 // declaration et enisialisation des variable dans le fichier haide deffin.h

stat_of_game=menu;
SDL_Event event;
int progencour=1;

while(progencour==1){
 while(SDL_PollEvent(&event)){
  switch(event.type){

    case SDL_QUIT:
    progencour=0;
    break;

  
    case SDL_MOUSEBUTTONDOWN:

      a=event.button.x;
      b=event.button.y ;

      printf("%d---%d \n",a,b);
    
    changestatfrommenu (&stat_of_game,a ,b);
    
    changerlesetats(&stat_of_game,a ,b);
    cliconnext(&stat_of_game,a ,b);
      changethemember(&stat_of_game,a ,b,&member);
      changethpoints(&stat_of_game,a ,b,&point);
      changethdesitions(&stat_of_game,a ,b,&desdas);
      changethdetasks(&stat_of_game,a ,b,&task);
      controlefin(&stat_of_game,data,a, b);
       generertxt (&stat_of_game,a ,b,data);
      
         break;

 
 case SDL_KEYDOWN:
       if(stat_of_game==but)
        {
          write (event,but_de_la_reunion,&NLetters,80);
         
				}
        
       if(stat_of_game==datetheur){
         
          write (event,dat_etheur_du_pv,&NLetterdate,50);
 
				}
        if(stat_of_game==membres)
        {
          if(member==1)
          {
            write (event,person1,&member1,20);
          }else if(member==2)
            {  
              write (event,person2,&member2,20);
               }
           else if(member==3)
            {  
              write (event,person3,&member3,20);
               }else if(member==4)
            {  
              write (event,person4,&member4,20);
               }else if(member==5)
            {  
              write (event,person5,&member5,20);
               }
        }
          if(stat_of_game==poins)
        {
          if(point==1)
          {
            write (event,point1,&pa1,80);
          }else if(point==2)
            {  
              write (event,point2,&pa2,80);
               }
           else if(point==3)
            {  
              write (event,point3,&pa3,80);
               }
        }
         if(stat_of_game==desition)
        {
          if(desdas==1)
          {
            write (event,desition1,&des1,80);
          }else if(desdas==2)
            {  
              write (event,desition2,&des2,80);
               }
           else if(desdas==3)
            {  
              write (event,desition3,&des3,80);
               }
        }
            if(stat_of_game==heurfin)
        {
          if(task==1)
          {
            write (event,task1,&takos1,80);
          }else if(task==2)
            {  
              write (event,task2,&takos2,80);
               }
           else if(task==3)
            {  
              write (event,task3,&takos3,80);
               }else if(task==4)
            {  
              write (event,task4,&takos4,80);
               }else if(task==5)
            {  
              write (event,task5,&takos5,80);
               }
        }
             if(stat_of_game==tach)
        {
          write (event,data,&longdate,20);
				}
     
     
        break;   
     default:{}

 
  
   
  
       

   
 


      
        


  
 

}
}
 SDL_SetRenderDrawColor(wajiha,0,0,0,255);
        SDL_RenderClear(wajiha);



           rendermenu(wajiha,maltato,&stat_of_game);
            
        rremplirebut(&stat_of_game, wajiha,surface,timtext,font1,color,textrect);
        
      if(stat_of_game!=menu){  affichtext(&stat_of_game, wajiha,surface,but_de_la_reunion,timtext,font2,color,textrect,20,0);};
      
         affichnext(wajiha,next,&stat_of_game);
         rempliredateetheur(&stat_of_game , wajiha,surface,timtext,font1,color,textrect);
      if(stat_of_game!=menu &&stat_of_game!=but) {affichtext(&stat_of_game, wajiha,surface,dat_etheur_du_pv,timtext,font2,color,textrect,60,0);}
           affichnp(wajiha,nextpr,&stat_of_game);
           affichprecident(wajiha,precident,&stat_of_game);
           remplirelesmembres(&stat_of_game ,wajiha,surface,timtext,font1,color,textrect);
           affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"1-",100);
           
           affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"2-",120);
          
           affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"3-",140);
           affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"4-",160);
           affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"5-",180);
           if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur ) {affichtext(&stat_of_game, wajiha,surface,person1,timtext,font2,color,textrect,100,25);}
           if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur ) {affichtext(&stat_of_game, wajiha,surface,person2,timtext,font2,color,textrect,120,25);}
           if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur ) {affichtext(&stat_of_game, wajiha,surface,person3,timtext,font2,color,textrect,140,25);}
           if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur ) {affichtext(&stat_of_game, wajiha,surface,person4,timtext,font2,color,textrect,160,25);}
           if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur ) {affichtext(&stat_of_game, wajiha,surface,person5,timtext,font2,color,textrect,180,25);}
           
           affichlogos(&stat_of_game,member,wajiha,pnmem,nmem ,pmem);

           remplirepoints(&stat_of_game , wajiha,surface,timtext ,font1, color, textrect);
           if(stat_of_game!=membres) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"1-",220);
            if(stat_of_game!=membres) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"2-",240);
             if(stat_of_game!=membres) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"3-",260);
              if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres ) {affichtext(&stat_of_game, wajiha,surface,point1,timtext,font2,color,textrect,220,25);}
              if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres ) {affichtext(&stat_of_game, wajiha,surface,point2,timtext,font2,color,textrect,240,25);}
              if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres ) {affichtext(&stat_of_game, wajiha,surface,point3,timtext,font2,color,textrect,260,25);}
              affichlogospoints(&stat_of_game,point,wajiha,ppoint,npoint ,pnpoint);
           
           remplirdesition(&stat_of_game ,wajiha,surface,timtext,font1, color, textrect);
           if(stat_of_game!=membres&&stat_of_game!=poins) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"1-",300);
           if(stat_of_game!=membres&&stat_of_game!=poins) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"2-",320);
           if(stat_of_game!=membres&&stat_of_game!=poins) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"3-",340);
           affichlogosdes(&stat_of_game,desdas,wajiha,pdes,ndes ,pndes);
            if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins ) {affichtext(&stat_of_game, wajiha,surface,desition1,timtext,font2,color,textrect,300,25);}
              if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins ) {affichtext(&stat_of_game, wajiha,surface,desition2,timtext,font2,color,textrect,320,25);}
              if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins ) {affichtext(&stat_of_game, wajiha,surface,desition3,timtext,font2,color,textrect,340,25);}

              remplirtasks(&stat_of_game ,wajiha,surface,timtext, font1, color, textrect);
              if(stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"1-",380);
              if(stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"2-",400);
              if(stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"3-",420);
              if(stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"4-",440);
              if(stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) affichnember(&stat_of_game , wajiha,surface,timtext,font1,color,textrect,"5-",460);

               if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) {affichtext(&stat_of_game, wajiha,surface,task1,timtext,font2,color,textrect,380,25);}
               if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) {affichtext(&stat_of_game, wajiha,surface,task2,timtext,font2,color,textrect,400,25);}
               if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) {affichtext(&stat_of_game, wajiha,surface,task3,timtext,font2,color,textrect,420,25);}
               if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) {affichtext(&stat_of_game, wajiha,surface,task4,timtext,font2,color,textrect,440,25);}
               if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition) {affichtext(&stat_of_game, wajiha,surface,task5,timtext,font2,color,textrect,460,25);}
               affichlogoTASK(&stat_of_game,task,wajiha,ptask,ntask ,nptask);

               remplirdate(&stat_of_game ,wajiha,surface,timtext,font1, color, textrect);
               if(stat_of_game!=menu &&stat_of_game!=but &&stat_of_game!=datetheur&&stat_of_game!=membres&&stat_of_game!=poins&&stat_of_game!=desition&&stat_of_game!=heurfin) {affichtext(&stat_of_game, wajiha,surface,data,timtext,font2,color,textrect,500,0);}

               fin (&stat_of_game,wajiha,pm,gdt,data ) ;


        SDL_RenderPresent(wajiha);

        if(stat_of_game==gn)
        {
          FILE * fic =fopen("doc.txt","w");
          fputs("But de reunion : \n",fic);
          fputs(but_de_la_reunion,fic);fputs("\n",fic);fputs("\n",fic);
          fputs("date et heure de reunion : \n",fic);
          fputs(dat_etheur_du_pv,fic);fputs("\n",fic);fputs("\n",fic);
          fputs("les membres present a la reunion : \n",fic);
          fputs(person1,fic);fputs("\n",fic);
          fputs(person2,fic);fputs("\n",fic);
          fputs(person3,fic);fputs("\n",fic);
          fputs(person4,fic);fputs("\n",fic);
          fputs(person5,fic);fputs("\n",fic);fputs("\n",fic);
          fputs("les points abordes : \n",fic);
          fputs(point1,fic);fputs("\n",fic);
          fputs(point2,fic);fputs("\n",fic);
          fputs(point3,fic);fputs("\n",fic);fputs("\n",fic);
          fputs("les decisions prises : \n",fic);
          fputs(desition1,fic);fputs("\n",fic);
          fputs(desition2,fic);fputs("\n",fic);
          fputs(desition3,fic);fputs("\n",fic);fputs("\n",fic);
          fputs("les taches attribues : \n",fic);
          fputs(task1,fic);fputs("\n",fic);
          fputs(task2,fic);fputs("\n",fic);
          fputs(task3,fic);fputs("\n",fic);
          fputs(task4,fic);fputs("\n",fic);
          fputs(task5,fic);fputs("\n",fic);fputs("\n",fic);
          fputs("l'heur de fin de la reunion \n",fic);
          fputs(data,fic);fputs("\n",fic);

          fclose(fic);
          stat_of_game=tach;
        }

}

SDL_DestroyWindow(nafida);
SDL_DestroyRenderer(wajiha);

 TTF_Quit();
 SDL_Quit();


 return 0;


}



// gcc src/pv.c -o bin/prog -I include -L lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lSDL2_ttf


