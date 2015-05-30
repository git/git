#include <stdio.h>
#include <unistd.h>
 

void choppa_frame_1(void) 
{
        printf("\n");
	printf("        GIT TO DA CHOPPA!!!\n");
	printf("\n");
        printf("---------------+---------------\n");
        printf("          ___ /^^[___              _\n");
        printf("         /|^+----+   |#___________//\n");
        printf("        ( -+ |____|    ______-----+/\n");
        printf("         ==_________--' \n");
        printf("           ~_|___|__\n");
}

void choppa_frame_2(void)
{
        printf("\n");
        printf("        GIT TO DA CHOPPA!!!\n");
        printf("\n");
        printf("      ---------+---------\n");
        printf("          ___ /^^[___              _\n");
        printf("         /|^+----+   |#___________//\n");
        printf("        ( -+ |____|    ______-----+/\n");
        printf("         ==_________--' \n");
        printf("           ~_|___|__\n");
}

void choppa_frame_3(void)
{
        printf("\n");
        printf("        GIT TO DA CHOPPA!!!\n");
        printf("\n");
        printf("            ---+---       \n");
        printf("          ___ /^^[___              _\n");
        printf("         /|^+----+   |#___________//\n");
        printf("        ( -+ |____|    ______-----+/\n");
        printf("         ==_________--' \n");
        printf("           ~_|___|__\n");
}

void choppa_frame_4(void)
{
        printf("\n");
        printf("        GIT TO DA CHOPPA!!!\n");
        printf("\n");
        printf("              -+-        \n");
        printf("          ___ /^^[___              _\n");
        printf("         /|^+----+   |#___________//\n");
        printf("        ( -+ |____|    ______-----+/\n");
        printf("         ==_________--' \n");
        printf("           ~_|___|__\n");
}

void git_to_da_choppa(void)
{
        void (*frames[6])(void);

        frames[0] = &choppa_frame_1;
        frames[1] = &choppa_frame_2;
        frames[2] = &choppa_frame_3;
        frames[3] = &choppa_frame_4;
        frames[4] = &choppa_frame_3;
        frames[5] = &choppa_frame_2;

        int index = 0;
        while(1) {
                usleep(200000);
		system("cls");
                system("clear");
                (*frames[index])();
                index++;
                index = index % 6;
        }
}

